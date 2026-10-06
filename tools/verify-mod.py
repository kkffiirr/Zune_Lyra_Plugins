"""Verify every deployable file of one or more mod folders matches what is on the Zune.
usage: python verify-mod.py <zune-ip> <mod_dir> [<mod_dir> ...]"""
import sys
from pathlib import Path
HERE = Path(__file__).resolve().parents[1]   # repo root
sys.path.insert(0, str(HERE / "lyra-src" / "tools" / "general"))
sys.path.insert(0, str(HERE / "lyra-src" / "modkit"))
from zune_repl import ZuneREPL
from modkit import Mod
from modkit.payload import deployable_files

ip = sys.argv[1]
r = ZuneREPL(ip, timeout=60.0)

def pull(p):
    out = b""; off = 0
    while True:
        c = r.pull_file(p, off, 0x1F00)
        if not c: break
        out += c; off += len(c)
        if len(c) < 0x1F00: break
    return out

bad = 0
for d in sys.argv[2:]:
    mod = Mod.from_dir(Path(d))
    base = "\\flash2\\automation\\mods\\" + mod.mod_id + "\\"
    for f in sorted(deployable_files(mod)):
        rel = f.relative_to(mod.source_dir).as_posix()
        try: dev = pull(base + rel.replace("/", "\\"))
        except Exception as e: print("MISSING", mod.mod_id, rel); bad += 1; continue
        ok = dev == f.read_bytes(); bad += 0 if ok else 1
        print("%-22s %-40s %8d %s" % (mod.mod_id, rel, len(dev), "MATCH" if ok else "DIFFERENT"))
try: print("enabled.json:", pull("\\flash2\\automation\\mods\\enabled.json").decode())
except Exception: print("enabled.json: (not present)")
print("ALL MATCH" if not bad else "%d problem(s)" % bad)
