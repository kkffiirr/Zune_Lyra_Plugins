"""Show the last boot's lines about a mod (default hebrew-font) from the Zune's boot.log, plus flush lines. usage: python read-bootlog.py <ip> [mod]"""
import sys
from pathlib import Path
HERE = Path(__file__).resolve().parents[1]   # repo root
sys.path.insert(0, str(HERE / "lyra-src" / "tools" / "general"))
from zune_repl import ZuneREPL

ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.178"
mod = sys.argv[2] if len(sys.argv) > 2 else "hebrew-font"
r = ZuneREPL(ip, timeout=60.0)

def pull(p):
    out = b""; off = 0
    while True:
        c = r.pull_file(p, off, 0x1F00)
        if not c: break
        out += c; off += len(c)
        if len(c) < 0x1F00: break
    return out.decode("utf-8", "replace")

base = "\\flash2\\automation\\mods\\"
b = pull(base + "boot.log").splitlines()
k = [i for i, l in enumerate(b) if "boot level" in l]
last = b[k[-1]:]
print("boots recorded:", len(k)); print(last[0])
on = False
for l in last:
    if "[" in l and "/" in l and "v" in l and "action(s)" in l:
        on = mod in l
    if on or "flush" in l or "failed" in l.lower() or "fault" in l.lower() or "total action" in l:
        print(l)
try:
    print("failed.ids:", pull(base + "failed.ids"))
except Exception as e:
    print("failed.ids: (none)")
