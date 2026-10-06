"""Switch mods off on the Zune from the PC (recovery if the UI misbehaves).
usage: python disable-mod.py <zune-ip> <mod_id> [<mod_id>...]
Reads \\flash2\\automation\\mods\\enabled.json, removes the ids, writes it back; reboot the Zune afterwards."""
import json, sys, tempfile, os
from pathlib import Path
HERE = Path(__file__).resolve().parents[1]   # repo root
sys.path.insert(0, str(HERE / "lyra-src" / "modkit"))
sys.path.insert(0, str(HERE / "lyra-src" / "tools" / "general"))
from zune_repl import ZuneREPL
from modkit.deploy import _push_file_chunked

ip, ids = sys.argv[1], set(sys.argv[2:])
PATH = r"\flash2\automation\mods\enabled.json"
r = ZuneREPL(ip, timeout=60.0)
data = b""; off = 0
while True:
    c = r.pull_file(PATH, off, 0x1F00)
    if not c: break
    data += c; off += len(c)
    if len(c) < 0x1F00: break
cur = json.loads(data.decode("utf-8"))
print("before:", cur["enabled"])
cur["enabled"] = [m for m in cur["enabled"] if m not in ids]
tmp = Path(tempfile.gettempdir()) / "enabled.json"
tmp.write_text(json.dumps(cur, separators=(",", ":")), encoding="utf-8")
_push_file_chunked(ip, tmp, PATH)
print("after: ", cur["enabled"], "- reboot the Zune to apply")
