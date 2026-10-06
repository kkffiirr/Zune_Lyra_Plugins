"""Print the latest fontload.log run and fontd.log from the Zune. usage: python read-fontlog.py <ip>"""
import sys
from pathlib import Path
HERE = Path(__file__).resolve().parents[1]   # repo root
sys.path.insert(0, str(HERE / "lyra-src" / "tools" / "general"))
from zune_repl import ZuneREPL

ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.178"
r = ZuneREPL(ip, timeout=60.0)

def pull(p):
    out = b""; off = 0
    while True:
        c = r.pull_file(p, off, 0x1F00)
        if not c: break
        out += c; off += len(c)
        if len(c) < 0x1F00: break
    return out.decode("utf-8", "replace")

base = "\\flash2\\automation\\"
t = pull(base + "fontload.log").splitlines()
idx = [i for i, l in enumerate(t) if "FontLoadInstall" in l]
print("== fontload.log: runs =", len(idx))
print("\n".join(t[idx[-1]:] if idx else t[-5:]))
try:
    d = pull(base + "fontd.log").splitlines()
    print("== fontd.log (last 8)"); print("\n".join(d[-8:]))
except Exception as e:
    print("fontd.log:", e)
b = pull(base + "mods\\boot.log").splitlines()
k = [i for i, l in enumerate(b) if "boot level" in l]
print("== boots:", len(k), b[k[-1]] if k else "")
