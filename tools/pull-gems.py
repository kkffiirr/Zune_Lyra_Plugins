"""Pull the UI resource files from the Zune's \\Windows folder to ./gems/ and report where font names appear.
usage: python pull-gems.py <ip>"""
import re, sys
from pathlib import Path
HERE = Path(__file__).resolve().parents[1]   # repo root
sys.path.insert(0, str(HERE / "lyra-src" / "tools" / "general"))
from zune_repl import ZuneREPL

ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.178"
r = ZuneREPL(ip, timeout=60.0)
out = HERE / "gems"; out.mkdir(exist_ok=True)

def pull(p):
    data = b""; off = 0
    while True:
        c = r.pull_file(p, off, 0x1F00)
        if not c: break
        data += c; off += len(c)
        if len(c) < 0x1F00: break
    return data

NAMES = ["skin_default.gem", "scenes_standard.gem", "HudScenes.gem", "strings.gem", "xnascenes.gem", "ZIE_Skins.gem"]
for n in NAMES:
    try:
        d = pull("\\Windows\\" + n)
    except Exception as e:
        print(n, "-> not readable:", e); continue
    (out / n).write_bytes(d)
    u16 = [m.start() for m in re.finditer("Zegoe".encode("utf-16-le"), d)]
    a8 = [m.start() for m in re.finditer(b"Zegoe", d)]
    print(f"{n}: {len(d)} bytes; 'Zegoe' utf-16 hits={len(u16)} ascii hits={len(a8)}")
