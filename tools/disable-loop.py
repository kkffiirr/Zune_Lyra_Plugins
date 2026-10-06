"""Crash-loop rescue: poll the Zune's Lyra port and, the moment it answers, remove mod ids from enabled.json.
usage: python disable-loop.py <zune-ip> <mod_id> [<mod_id>...]   (stops when the change is confirmed)"""
import json, socket, sys, tempfile, time
from pathlib import Path
HERE = Path(__file__).resolve().parents[1]   # repo root
sys.path.insert(0, str(HERE / "lyra-src" / "modkit"))
sys.path.insert(0, str(HERE / "lyra-src" / "tools" / "general"))
from zune_repl import ZuneREPL
from modkit.deploy import _push_file_chunked

ip, ids = sys.argv[1], set(sys.argv[2:])
PATH = r"\flash2\automation\mods\enabled.json"

def up():
    try:
        socket.create_connection((ip, 1337), timeout=0.4).close(); return True
    except OSError:
        return False

def read():
    r = ZuneREPL(ip, timeout=6.0); data = b""; off = 0
    while True:
        c = r.pull_file(PATH, off, 0x1F00)
        if not c: break
        data += c; off += len(c)
        if len(c) < 0x1F00: break
    return json.loads(data.decode("utf-8"))

t0 = time.time(); attempts = 0
while True:
    if not up():
        time.sleep(0.15); continue
    attempts += 1
    try:
        cur = read()
        if not (ids & set(cur["enabled"])):
            print("already clear:", cur["enabled"], flush=True); break
        cur["enabled"] = [m for m in cur["enabled"] if m not in ids]
        tmp = Path(tempfile.gettempdir()) / "enabled.json"
        tmp.write_text(json.dumps(cur, separators=(",", ":")), encoding="utf-8")
        _push_file_chunked(ip, tmp, PATH)
        chk = read()
        if not (ids & set(chk["enabled"])):
            print("DONE after %d attempt(s), %.0fs: enabled=%s" % (attempts, time.time() - t0, chk["enabled"]), flush=True); break
        print("written but still present?", chk["enabled"], flush=True)
    except Exception as e:
        print("attempt %d failed: %r" % (attempts, e), flush=True)
        time.sleep(0.3)
