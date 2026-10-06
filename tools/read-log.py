"""Print a log file from the Zune's automation folder. usage: python read-log.py <ip> <filename> [tail_lines]"""
import sys
from pathlib import Path
HERE = Path(__file__).resolve().parents[1]   # repo root
sys.path.insert(0, str(HERE / "lyra-src" / "tools" / "general"))
from zune_repl import ZuneREPL

ip, name = sys.argv[1], sys.argv[2]
tail = int(sys.argv[3]) if len(sys.argv) > 3 else 0
r = ZuneREPL(ip, timeout=60.0)
out = b""; off = 0
while True:
    c = r.pull_file("\\flash2\\automation\\" + name, off, 0x1F00)
    if not c: break
    out += c; off += len(c)
    if len(c) < 0x1F00: break
t = out.decode("utf-8", "replace").splitlines()
if name == "regprobe.log":
    idx = [i for i, l in enumerate(t) if "RegProbeInstall" in l]
    t = t[idx[-1]:] if idx else t
print("\n".join(t[-tail:] if tail else t))
