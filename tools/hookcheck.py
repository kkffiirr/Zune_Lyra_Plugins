#!/usr/bin/env python3
"""Offline hook-safety check against the live gemstone dump (base 0x10000).

For each target VA:
  1. static check: do the first 2 instructions (the ones ModHookInstall relocates) depend on PC or branch?
  2. emulation: run the target unhooked for 2 instructions, then run it hooked (entry patch ->
     pass-through replacement -> trampoline built exactly like mods_hook.c) until it resumes at
     target+8, and compare r0-r12/sp/lr/memory. Equal state == hook is transparent.
Usage: hookcheck.py [0xVA ...]   (default: 0x38434)
"""
import struct, sys, random
import capstone
from capstone import arm_const as A
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_WRITE, UcError
from unicorn.arm_const import *

BASE = 0x10000
IMG = open(__import__("os").environ.get("GEM_DUMP", __file__.replace("hookcheck.py", "gem_live.bin")), "rb").read()
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
md.detail = True
REGS = [UC_ARM_REG_R0 + i for i in range(13)] + [UC_ARM_REG_SP, UC_ARM_REG_LR]

def rd32(va): return struct.unpack_from("<I", IMG, va - BASE)[0]

def static_check(va):
    """Return list of problems for the 2 relocated instructions."""
    bad = []
    for k in range(2):
        a = va + 4 * k
        ins = next(md.disasm(IMG[a - BASE:a - BASE + 4], a))
        reads_pc = any(o.type == A.ARM_OP_REG and o.reg == A.ARM_REG_PC for o in ins.operands)
        is_branch = ins.group(A.ARM_GRP_JUMP) or ins.group(A.ARM_GRP_CALL) or ins.mnemonic.startswith(("b", "bl"))
        writes_pc = ins.reg_write(A.ARM_REG_PC) if hasattr(ins, "reg_write") else False
        if reads_pc or is_branch or writes_pc or "pc" in ins.op_str:
            bad.append(f"  insn {k} @ {a:#x}: {ins.mnemonic} {ins.op_str}  <- position-dependent")
    return bad

def build_uc():
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x10000, 0x90000)           # gemstone image (code+data)
    uc.mem_write(0x10000, IMG)
    uc.mem_map(0x00A00000, 0x10000)        # stack
    uc.mem_map(0x02000000, 0x1000)         # trampoline + replacement stub
    uc.mem_map(0x03000000, 0x10000)        # scratch the args point at
    return uc

def run(uc, start, stop_pc, regs, max_ins=50):
    for r, v in zip(REGS, regs): uc.reg_write(r, v)
    writes = []
    h = uc.hook_add(UC_HOOK_MEM_WRITE, lambda u, t, a, s, v, d: writes.append((a, s, v & ((1 << 8 * s) - 1))))
    try:
        uc.emu_start(start, stop_pc, count=max_ins)
    except UcError as e:
        uc.hook_del(h)
        return None, f"emulation error {e}"
    uc.hook_del(h)
    return ([uc.reg_read(r) for r in REGS], sorted(writes)), uc.reg_read(UC_ARM_REG_PC)

def emulate(va):
    random.seed(va)
    regs = [0x03000100, 0x03000200, 0x03000300, 0x03000400] + [random.getrandbits(32) for _ in range(9)] + [0x00A08000, 0x00012345]
    # --- unhooked: run exactly the first 2 instructions
    uc = build_uc()
    base, _ = run(uc, va, va + 8, regs, 2)
    if base is None: return f"  baseline emulation failed: {_}"
    # --- hooked, built like mods_hook.c
    uc = build_uc()
    orig0, orig1 = rd32(va), rd32(va + 4)
    TR, REPL = 0x02000000, 0x02000100
    uc.mem_write(TR, struct.pack("<4I", orig0, orig1, 0xe51ff004, va + 8))        # build_original_tramp
    uc.mem_write(REPL, struct.pack("<2I", 0xe51ff004, TR))                         # pass-through replacement: jump to "next"
    uc.mem_write(va, struct.pack("<2I", 0xe51ff004, REPL))                         # entry patch
    hooked, pc = run(uc, va, va + 8, regs, 20)
    if hooked is None: return f"  hooked run failed: {pc}"
    diffs = [f"{n}: base={b:#x} hooked={h:#x}" for n, b, h in
             zip([f"r{i}" for i in range(13)] + ["sp", "lr"], base[0], hooked[0]) if b != h]
    if base[1] != hooked[1]: diffs.append(f"memory writes differ: {base[1]} vs {hooked[1]}")
    return ("  EQUIVALENT: hooked run resumes at target+8 with identical regs/stores" if not diffs
            else "  DIFFERS:\n    " + "\n    ".join(diffs))

def check(va):
    print(f"== {va:#x}")
    bad = static_check(va)
    print("  static:", "OK, relocatable" if not bad else "REJECT")
    for b in bad: print(b)
    print(emulate(va))
    return not bad

if __name__ == "__main__":
    targets = [int(a, 0) for a in sys.argv[1:]] or [0x38434]
    ok = all([check(t) for t in targets])
    sys.exit(0 if ok else 1)
