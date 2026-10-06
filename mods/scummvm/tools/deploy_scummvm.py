#!/usr/bin/env python3
"""Deploy ScummVM to a Zune HD running Project Lyra. Standard library only; run it on Windows.

  find                      find the Zune on the local network (Lyra daemon, port 1337)
  kit   --exe E --thumb T   build the XNA deploy kit folder (application.cfg + payload)
  wifi  --ip IP --bin DIR   create \\flash2\\scummvm\\..., upload the programs and game data (resumable)
  usb   --kit DIR           install the Apps tile over USB (usbipd + WSL + zune-deploy's zcli)
  all   --ip IP --bin DIR --kit DIR   wifi, then pause for you to plug USB, then usb

--bin must hold scummvmapp.exe, scummvmrun.exe and scummvm-zune.dll. Games: --game tentacle=DIR
--game samnmax=DIR (the folder with the game's own data files). The Zune cannot be on Wi-Fi and USB at once, so
`all` stops and asks you to switch. Nothing here is destructive: it only creates \\flash2\\scummvm and writes
files there (an existing file of the same name is overwritten).
"""
import argparse, concurrent.futures as cf, json, os, shutil, socket, struct, subprocess, sys, time

PORT, CHUNK = 1337, 16384
ROOT = "\\flash2\\scummvm"
PROGRAMS = ["scummvmapp.exe", "scummvmrun.exe", "scummvm-zune.dll"]
GAMES = {  # id -> data files the launcher looks for / the engine needs
    "tentacle": ["TENTACLE.000", "TENTACLE.001", "MONSTER.SOU"],
    "samnmax": ["samnmax.000", "samnmax.001", "monster.sou"],
}
APP_CFG = ("guid: 5c077a11-0000-0000-0000-000000000000\nname: ScummVM\n"
           "description: ScummVM for the Zune HD (Day of the Tentacle, Sam and Max).\n"
           "exec: exploiter.exe\nsrc: payload\nthumbnail: GameThumbnail.png\ncompatibility: hd\n")
ZCLI = "/opt/lyra/zune-deploy/src/ZuneDeploy.CLI/bin/Debug/net10.0/ZuneDeploy.CLI.dll"
ZUNE_USB = "045e:063e"

# ---------------------------------------------------------------- Lyra RPC
def read_exact(s, n):
    out = bytearray()
    while len(out) < n:
        c = s.recv(n - len(out))
        if not c: raise ConnectionError("socket closed")
        out.extend(c)
    return bytes(out)

def connect(ip, timeout=8.0):
    s = socket.create_connection((ip, PORT), timeout=timeout); s.settimeout(timeout)
    if read_exact(s, 6) != b"Hello\n": raise ConnectionError("not a Lyra daemon")
    return s

def mkdir(ip, path):
    """Opcode 13 (CreateDirectoryW); 'already exists' counts as success."""
    p = path.encode()
    with connect(ip) as s:
        h = bytearray(32); h[0] = 13; h[1:5] = struct.pack("<I", len(p))
        s.sendall(h); s.sendall(p); r = read_exact(s, 32)
    return r[5] == 1 or struct.unpack("<I", r[1:5])[0] == 183

def write_chunk(s, path, off, data):
    h = bytearray(32); h[0] = 12
    h[1:5] = struct.pack("<I", len(path)); h[5:9] = struct.pack("<I", off); h[9:13] = struct.pack("<I", len(data))
    s.sendall(h); s.sendall(path); s.sendall(data); r = read_exact(s, 32)
    return struct.unpack("<I", r[1:5])[0], r[9] != 0

def upload(ip, local, remote, ckpt_dir):
    """Resumable upload (opcode 12 writes at explicit offsets; offset 0 creates/truncates). Reconnects forever."""
    size = os.path.getsize(local); path = remote.encode()
    ck = os.path.join(ckpt_dir, remote.replace("\\", "_").replace(":", "") + ".json")
    off = 0
    if os.path.exists(ck):
        try:
            j = json.load(open(ck))
            if j.get("size") == size and j.get("local") == os.path.abspath(local): off = j["offset"]
        except Exception: pass
    print(f"  {os.path.basename(local)} -> {remote}  ({size} bytes, from {off})", flush=True)
    s, last, f = None, -1, open(local, "rb")
    while off < size:
        try:
            if s is None: s = connect(ip)
            f.seek(off); bw, ok = write_chunk(s, path, off, f.read(CHUNK))
            if not ok or bw == 0: raise ConnectionError("write failed")
            off += bw
            if (off // CHUNK) % 64 == 0:
                json.dump({"size": size, "local": os.path.abspath(local), "offset": off}, open(ck, "w"))
            pct = int(100 * off / size)
            if size > 5_000_000 and pct % 10 == 0 and pct != last: print(f"    {pct}%", flush=True); last = pct
        except (OSError, ConnectionError):
            print("    link lost, retrying (is the Zune awake and on Wi-Fi?)", flush=True)
            try: s and s.close()
            except OSError: pass
            s = None; time.sleep(3)
    json.dump({"size": size, "local": os.path.abspath(local), "offset": off}, open(ck, "w"))

# ---------------------------------------------------------------- commands
def local_prefix():
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try: u.connect(("10.255.255.255", 1)); return u.getsockname()[0].rsplit(".", 1)[0]
    finally: u.close()

def cmd_find(a):
    prefix = a.subnet or local_prefix()
    def probe(i):
        ip = f"{prefix}.{i}"
        try:
            s = socket.create_connection((ip, PORT), timeout=2); s.settimeout(2); b = s.recv(16); s.close()
            return ip if b.startswith(b"Hello") else None
        except OSError: return None
    with cf.ThreadPoolExecutor(64) as ex: found = [x for x in ex.map(probe, range(1, 255)) if x]
    print("Lyra daemon at:", ", ".join(found) if found else f"none on {prefix}.x (same Wi-Fi? Zune awake?)")
    return 0 if found else 1

def cmd_kit(a):
    os.makedirs(os.path.join(a.out, "payload"), exist_ok=True)
    shutil.copy(a.exe, os.path.join(a.out, "payload", "exploiter.exe"))
    shutil.copy(a.thumb, os.path.join(a.out, "GameThumbnail.png"))
    open(os.path.join(a.out, "application.cfg"), "w", newline="\n").write(APP_CFG)
    print("kit written to", a.out); return 0

def cmd_wifi(a):
    ip = a.ip
    for p in [f"{ROOT}", f"{ROOT}\\games", f"{ROOT}\\saves"] + [f"{ROOT}\\games\\{g.split('=')[0]}" for g in a.game or []]:
        if not mkdir(ip, p): print("could not create", p); return 1
    ck = os.path.join(a.bin, ".upload-state"); os.makedirs(ck, exist_ok=True)
    for f in PROGRAMS:
        src = os.path.join(a.bin, f)
        if not os.path.exists(src): print("missing", src); return 1
        upload(ip, src, f"{ROOT}\\{f}", ck)
    for spec in a.game or []:
        gid, folder = spec.split("=", 1)
        names = {n.lower(): n for n in os.listdir(folder)}
        for want in GAMES.get(gid, []):
            real = names.get(want.lower())
            if not real: print(f"  {gid}: {want} not found in {folder}"); return 1
            upload(ip, os.path.join(folder, real), f"{ROOT}\\games\\{gid}\\{want}", ck)
    print("wifi part done"); return 0

def run(cmd, **kw): return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", **kw)

def cmd_usb(a):
    out = run(["usbipd", "list"]).stdout
    line = next((l for l in out.splitlines() if ZUNE_USB in l), None)
    if not line: print("Zune not seen on USB. Plug it in (data cable), unlock it, close the Zune desktop software."); return 1
    busid = a.busid or line.split()[0]
    if "Attached" not in line:
        r = run(["usbipd", "attach", "--wsl", "--busid", busid])
        if r.returncode: print("usbipd attach failed (try once: usbipd bind --force --busid %s, as admin):" % busid, r.stderr.strip()); return 1
    keep = subprocess.Popen(["wsl", "sleep", "1500"], creationflags=0x08000000)   # WSL needs a live process or the attach drops
    time.sleep(3)
    kit = os.path.abspath(a.kit).replace("\\", "/")          # C:/x/y -> /mnt/c/x/y (wslpath loses the backslashes)
    if len(kit) > 1 and kit[1] == ":": kit = "/mnt/" + kit[0].lower() + kit[2:]
    sh = f"export HOME=/root; cd /root; timeout 280 dotnet {a.zcli} deploy '{kit}' 2>&1 | tr '\\r' '\\n'"
    r = run(["wsl", "-u", "root", "bash", "-c", sh]); keep.terminate()
    ok = [l.strip().split("\x1b")[-1] for l in r.stdout.splitlines() if "✓" in l or "✗" in l or "rror" in l]
    print("\n".join(sorted(set(l[-70:] for l in ok))))
    good = "Uploaded Container Metadata" in r.stdout and "Closed Game Container" in r.stdout
    if not good:                                            # never fail silently: show what zcli / WSL said
        tail = (r.stdout + r.stderr).strip().splitlines()[-12:]
        print("--- last output ---\n" + "\n".join(l[-160:] for l in tail))
    print("USB install:", "OK - unplug the cable, the ScummVM tile is in Apps" if good else "FAILED (see messages above)")
    return 0 if good else 1

def cmd_all(a):
    if cmd_wifi(a): return 1
    input("\nNow plug the Zune into the PC with USB (it will leave Wi-Fi). Press Enter when plugged in... ")
    if cmd_usb(a): return 1
    print("Done. Unplug the cable and reconnect the Zune to Wi-Fi if you want to read logs."); return 0

def main():
    try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # zcli prints check marks; Windows consoles choke
    except Exception: pass
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("find"); p.add_argument("--subnet", help="e.g. 192.168.10 (default: this PC's /24)"); p.set_defaults(fn=cmd_find)
    p = sp.add_parser("kit"); p.add_argument("--exe", required=True); p.add_argument("--thumb", required=True); p.add_argument("--out", default="deploykit"); p.set_defaults(fn=cmd_kit)
    for name, fn in (("wifi", cmd_wifi), ("all", cmd_all)):
        p = sp.add_parser(name); p.add_argument("--ip", required=True); p.add_argument("--bin", required=True)
        p.add_argument("--game", action="append", metavar="ID=DIR"); p.set_defaults(fn=fn)
        if name == "all": p.add_argument("--kit", required=True); p.add_argument("--busid"); p.add_argument("--zcli", default=ZCLI)
    p = sp.add_parser("usb"); p.add_argument("--kit", required=True); p.add_argument("--busid"); p.add_argument("--zcli", default=ZCLI); p.set_defaults(fn=cmd_usb)
    a = ap.parse_args(); sys.exit(a.fn(a))

if __name__ == "__main__":
    main()
