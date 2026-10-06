"""Verify every file of the hebrew-font mod on the Zune matches the local fontload/ folder; print enabled.json and boot.state."""
import sys
from pathlib import Path
HERE = Path(__file__).resolve().parents[1]   # repo root
sys.path.insert(0, str(HERE / "lyra-src" / "tools" / "general"))
sys.path.insert(0, str(HERE / "lyra-src" / "modkit"))
from zune_repl import ZuneREPL
from modkit import Mod
from modkit.payload import deployable_files

ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.178"
r = ZuneREPL(ip, timeout=60.0)

def pull(p):
    out = b""; off = 0
    while True:
        c = r.pull_file(p, off, 0x1F00)
        if not c: break
        out += c; off += len(c)
        if len(c) < 0x1F00: break
    return out

mod = Mod.from_dir(HERE / "mods" / "hebrew-font")
base = "\\flash2\\automation\\mods\\hebrew-font\\"
bad = 0
for f in sorted(deployable_files(mod)):
    rel = f.relative_to(mod.source_dir).as_posix()
    local = f.read_bytes()
    try:
        dev = pull(base + rel.replace("/", "\\"))
    except Exception as e:
        print("MISSING", rel, e); bad += 1; continue
    ok = dev == local
    bad += 0 if ok else 1
    print("%-48s %8d %8d %s" % (rel, len(local), len(dev), "MATCH" if ok else "DIFFERENT"))
print("ALL FILES MATCH" if not bad else "%d problem(s)" % bad)
for n in ("mods\\enabled.json", "boot.state"):
    try: print(n, ":", pull("\\flash2\\automation\\" + n).decode())
    except Exception: print(n, ": (not present)")
