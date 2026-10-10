#!/usr/bin/env python3
"""Load SCUMM games onto a Zune HD running the ScummVM tile. Plain Python 3, no extra packages.

  python load_games.py                      interactive: drag a folder / disc image / .zip in, press Enter
  python load_games.py PATH [PATH ...]      PATH = game folder, disc image (.iso .bin .img .cue) or .zip
                                            (give every disc of a multi-disc game in the same run)
  options: --ip ADDR   skip the search for the Zune        --list       only show what was found
           --yes       do not ask for confirmation         --force      upload again even if done before
           --temp DIR  where to unpack .zip files (needs free disk space)

Any SCUMM game ScummVM can run in this build is recognised the way ScummVM does it (file names + MD5 of the main data
file, from scumm_detect.json): Maniac Mansion, Zak McKracken, Indiana Jones 3 and 4, Loom, Monkey Island 1 and 2,
Day of the Tentacle, Sam & Max, Full Throttle, The Dig, The Curse of Monkey Island and more.
The Zune must be awake on the same Wi-Fi as this PC and running Project Lyra (its daemon listens on port 1337).
Each game goes to \\flash2\\scummvm\\games\\<ScummVM game id>\\ ; the ScummVM tile lists every folder there.
Uploads resume if the link drops.
"""
import argparse, concurrent.futures as cf, hashlib, json, os, re, shutil, socket, struct, sys, tempfile, time, zipfile

PORT, CHUNK = 1337, 16384
BS = chr(92)
ROOT = BS + "flash2" + BS + "scummvm" + BS + "games"
HERE = os.path.dirname(os.path.abspath(__file__))
STATE_FILE = os.path.join(HERE, "load_games_state.json")
MD5_LIMIT = 1024 * 1024                     # ScummVM hashes the first 1 MB of the detection file

# Files a game cannot run without (warn if absent). Anything else is optional (speech, music, videos).
REQUIRED = {
    "tentacle": ["TENTACLE.000", "TENTACLE.001"], "samnmax": ["SAMNMAX.000", "SAMNMAX.001"],
    "comi": ["COMI.LA0", "COMI.LA1", "COMI.LA2"], "ft": ["FT.LA0", "FT.LA1"], "dig": ["DIG.LA0", "DIG.LA1"],
    "monkey": ["MONKEY.000", "MONKEY.001"], "monkey2": ["MONKEY2.000", "MONKEY2.001"],
    "atlantis": ["ATLANTIS.000", "ATLANTIS.001"],
}
NOTES = {"tentacle": "speech needs MONSTER.SOU (MONSTER.SOF, the FLAC version, is not supported)",
         "samnmax": "speech needs MONSTER.SOU", "comi": "needs both discs (COMI.LA1 is on disc 1, COMI.LA2 on disc 2)"}
# game data worth sending; everything else on a disc (installers, DirectX, manuals...) is left behind
DATA_EXT = {".lfl", ".sou", ".sog", ".sof", ".bun", ".san", ".nut", ".tab", ".d64", ".dsk", ".prg", ".mp3", ".ogg", ".flac"}
DATA_RE = re.compile(r"[.](\d{3}|la\d)$", re.I)             # .000 .001 .LA0 .LA1
SKIP_DIRS = {"directx", "drivers", "install", "setup", "support", "manual", "docs", "extras", "bonus", "autorun"}

DETECT = json.load(open(os.path.join(HERE, "scumm_detect.json")))
MD5 = {}
for row in DETECT["md5"]:
    MD5.setdefault(row[0], row)


def is_data(rel):
    parts = rel.replace(chr(92), "/").split("/")
    if any(p.lower() in SKIP_DIRS for p in parts[:-1]):
        return False
    name = parts[-1]
    return os.path.splitext(name)[1].lower() in DATA_EXT or bool(DATA_RE.search(name))


# ------------------------------------------------------------------ sources: folders, disc images, zips
class Entry:
    def __init__(self, src, rel, size, reader):
        self.src, self.rel, self.size, self.read = src, rel.replace(BS, "/"), size, reader   # read(offset, n) -> bytes


class DiscImage:
    """ISO 9660 in a raw image; handles 2048-byte, 2336 (mode 2) and 2352 (raw) sectors."""
    LAYOUTS = ((2048, 0), (2352, 16), (2352, 24), (2336, 8), (2336, 0))

    def __init__(self, path):
        self.f = open(path, "rb")
        for ss, off in self.LAYOUTS:
            self.f.seek(16 * ss + off)
            if self.f.read(6) == b"\x01CD001":
                self.ss, self.off = ss, off
                break
        else:
            raise ValueError("not an ISO 9660 disc image")

    def sector(self, n):
        self.f.seek(n * self.ss + self.off)
        return self.f.read(2048)

    def read(self, lba, length, offset, n):
        out, end = bytearray(), min(offset + n, length)
        while offset < end:
            s, so = divmod(offset, 2048)
            take = min(2048 - so, end - offset)
            out += self.sector(lba + s)[so:so + take]
            offset += take
        return bytes(out)

    def walk(self, lba=None, length=None, prefix=""):
        if lba is None:
            root = self.sector(16)[156:190]
            lba, length = struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0]
        data = self.read(lba, length, 0, length)
        i = 0
        while i < len(data):
            n = data[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            flags, nlen = data[i + 25], data[i + 32]
            name = data[i + 33:i + 33 + nlen].decode("latin1")
            elba, elen = struct.unpack("<I", data[i + 2:i + 6])[0], struct.unpack("<I", data[i + 10:i + 14])[0]
            if name not in ("\x00", "\x01"):
                clean = name.split(";")[0]
                if flags & 2:
                    yield from self.walk(elba, elen, prefix + clean + "/")
                else:
                    yield prefix + clean, elba, elen
            i += n


def file_reader(path):
    def read(offset, n):
        with open(path, "rb") as f:
            f.seek(offset)
            return f.read(n)
    return read


def entries_from_image(path):
    img = DiscImage(path)
    return [Entry(path, rel, ln, (lambda o, n, lba=lba, ln=ln: img.read(lba, ln, o, n))) for rel, lba, ln in img.walk()]


def entries_from_folder(root):
    out = []
    for d, _, files in os.walk(root):
        for fn in files:
            p = os.path.join(d, fn)
            if fn.lower().endswith((".iso", ".img", ".bin")):    # a disc image inside the folder/zip: read it too
                try:
                    out += entries_from_image(p)
                    continue
                except Exception:
                    pass                                         # not an ISO 9660 image: treat as a plain file
            out.append(Entry(root, os.path.relpath(p, root), os.path.getsize(p), file_reader(p)))
    return out


def entries_from_zip(path, temp):
    dest = os.path.join(temp, os.path.splitext(os.path.basename(path))[0])
    print(f"  unpacking {os.path.basename(path)} (needs free disk space) ...")
    with zipfile.ZipFile(path) as z:
        z.extractall(dest)
    return entries_from_folder(dest)


def collect(path, temp):
    path = path.strip().strip('"').strip("'")
    low = path.lower()
    if os.path.isdir(path):
        return entries_from_folder(path)
    if low.endswith(".zip"):
        return entries_from_zip(path, temp)
    if low.endswith(".cue"):                                     # the image is the file the .cue points at
        base = os.path.dirname(path)
        for line in open(path, errors="replace"):
            if line.strip().upper().startswith("FILE"):
                return entries_from_image(os.path.join(base, line.split('"')[1]))
        raise ValueError("no FILE line in the .cue")
    if low.endswith((".iso", ".bin", ".img")):
        return entries_from_image(path)
    raise ValueError("not a folder, disc image or zip")


# ------------------------------------------------------------------ recognising games (port of ScummVM's SCUMM detector)
def detection_name(pattern, gen, platform):
    if gen in ("kGenDiskNum", "kGenRoomNum"):
        return pattern % 0
    if gen in ("kGenDiskNumSteam", "kGenRoomNumSteam"):
        return DETECT["steam"].get(pattern.lower() + "|" + platform)
    if gen in ("kGenHEPC", "kGenHEIOS"):
        return pattern + ".he0"
    if gen == "kGenHEMac":
        return pattern + " (0)"
    if gen == "kGenHEMacNoParens":
        return pattern + " 0"
    return pattern                                               # kGenUnchanged


def md5_of(e):
    return hashlib.md5(e.read(0, MD5_LIMIT)).hexdigest()


def add_game(games, entries, src, d, gid, exact, info):
    g = games.setdefault(gid, dict(title=DETECT["titles"].get(gid, gid), files=[], seen={}, exact=exact, info=info, problems=[]))
    g["exact"] = g["exact"] and exact
    prefix = d + "/" if d else ""
    for e in entries:
        if e.src != src or not e.rel.startswith(prefix):
            continue
        rel = e.rel[len(prefix):]
        if not is_data(rel):
            continue
        key = rel.upper()
        if key in g["seen"]:
            if g["seen"][key].size != e.size:
                g["problems"].append(f"{rel}: two different copies (sizes {g['seen'][key].size} and {e.size}); using the first")
            continue
        g["seen"][key] = e
        g["files"].append((rel, e))


def find_games(entries):
    """Returns (games, unclear). games = {game id: dict(title, files=[(relative path, Entry)], missing, problems, exact)};
    unclear = [(src, folder, [possible game ids])] for data that several games could own (unknown versions only)."""
    dirs = {}
    for e in entries:
        d, _, name = e.rel.rpartition("/")
        dirs.setdefault((e.src, d), {})[name.lower()] = e
    games, unclear, pending = {}, [], []
    for (src, d), names in dirs.items():
        verified, guessed = {}, {}                               # guessed: detection file -> {game id: note}
        for gid, pattern, gen, platform, extra in DETECT["patterns"]:
            fname = detection_name(pattern, gen, platform)
            e = names.get((fname or "").lower())
            if not e:
                continue
            if fname.lower().endswith((".d64", ".dsk", ".prg")):
                guessed.setdefault(fname.lower(), {}).setdefault(gid, extra or "")        # disk images: filename only
                continue
            row = MD5.get(md5_of(e))
            if row:
                verified[row[1]] = " ".join(x for x in (row[2], row[3]) if x)
            else:
                guessed.setdefault(fname.lower(), {}).setdefault(gid, extra or "")
        if verified:
            for gid, info in verified.items():
                pending.append((src, d, gid, True, info))
            continue
        for fname, ids in guessed.items():
            if len(ids) == 1:
                gid, info = next(iter(ids.items()))
                pending.append((src, d, gid, False, info))
            else:
                unclear.append((src, d, sorted(ids)))
    for src, d, gid, exact, info in pending:
        add_game(games, entries, src, d, gid, exact, info)
    return games, unclear


def finish_games(games):
    for gid, g in games.items():
        have = {r.upper() for r, _ in g["files"]}
        g["missing"] = [n for n in REQUIRED.get(gid, []) if n not in have]
        if not g["exact"]:
            g["problems"].append("version not in ScummVM's list of known versions; it may still run")
        if gid in ("tentacle", "samnmax") and "MONSTER.SOU" not in have:
            g["problems"].append("no MONSTER.SOU (the game will run, without speech)")


# ------------------------------------------------------------------ the Zune (Lyra daemon)
def read_exact(s, n):
    out = bytearray()
    while len(out) < n:
        c = s.recv(n - len(out))
        if not c:
            raise ConnectionError("socket closed")
        out.extend(c)
    return bytes(out)


def connect(ip, timeout=8.0):
    s = socket.create_connection((ip, PORT), timeout=timeout)
    s.settimeout(timeout)
    if read_exact(s, 6) != b"Hello\n":
        raise ConnectionError("not a Lyra daemon")
    return s


def mkdirs(ip, path):
    parts = path.strip(BS).split(BS)
    for i in range(2, len(parts) + 1):                            # \flash2 exists; create the rest
        p = (BS + BS.join(parts[:i])).encode()
        with connect(ip) as s:
            h = bytearray(32)
            h[0] = 13
            h[1:5] = struct.pack("<I", len(p))
            s.sendall(h)
            s.sendall(p)
            r = read_exact(s, 32)
        if not (r[5] == 1 or struct.unpack("<I", r[1:5])[0] == 183):   # created, or already exists
            raise RuntimeError("could not create " + p.decode())


def find_zune(hint):
    cands = [hint] if hint else []
    try:
        st = json.load(open(STATE_FILE)).get("last_ip")
        if st:
            cands.insert(0, st)
    except Exception:
        pass
    def ok(ip):
        try:
            connect(ip, 1.5).close()
            return True
        except Exception:
            return False
    for ip in cands:
        if ok(ip):
            return ip
    prefixes = set()
    try:
        for ip in socket.gethostbyname_ex(socket.gethostname())[2]:
            prefixes.add(ip.rsplit(".", 1)[0])
        u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        u.connect(("10.255.255.255", 1))
        prefixes.add(u.getsockname()[0].rsplit(".", 1)[0])
        u.close()
    except OSError:
        pass
    print("  looking for the Zune on your network ...")
    with cf.ThreadPoolExecutor(128) as ex:
        for pre in sorted(prefixes):
            ips = [f"{pre}.{i}" for i in range(1, 255)]
            for ip, good in zip(ips, ex.map(ok, ips)):
                if good:
                    return ip
    return None


def load_state():
    try:
        return json.load(open(STATE_FILE))
    except Exception:
        return {}


def save_state(st):
    json.dump(st, open(STATE_FILE, "w"), indent=1)


def upload(ip, remote, e, st, force):
    key = f"{ip}|{remote}"
    rec = st.get(key, {})
    stamp = [e.size, e.src]
    off = rec.get("offset", 0) if rec.get("stamp") == stamp and not force else 0
    if off >= e.size and not force and e.size > 0:
        return "already on the Zune"
    path, t0, start, s = remote.encode(), time.time(), off, None
    while off < e.size or e.size == 0:
        try:
            if s is None:
                s = connect(ip)
            data = e.read(off, CHUNK)
            h = bytearray(32)
            h[0] = 12
            h[1:5] = struct.pack("<I", len(path))
            h[5:9] = struct.pack("<I", off)
            h[9:13] = struct.pack("<I", len(data))
            s.sendall(h)
            s.sendall(path)
            s.sendall(data)
            r = read_exact(s, 32)
            written = struct.unpack("<I", r[1:5])[0]
            if r[9] == 0 or (written == 0 and data):
                raise ConnectionError("write failed")
            off += written
            if e.size == 0:
                break
            if (off // CHUNK) % 32 == 0 or off >= e.size:
                st[key] = dict(stamp=stamp, offset=off)
                save_state(st)
            rate = (off - start) / max(time.time() - t0, 0.1) / 1e6
            left = (e.size - off) / max(rate * 1e6, 1)
            sys.stdout.write(f"\r    {100 * off // max(e.size, 1):3d}%  {off / 1e6:7.1f}/{e.size / 1e6:.1f} MB  {rate:4.2f} MB/s  ~{left / 60:4.1f} min left   ")
            sys.stdout.flush()
        except (OSError, ConnectionError):
            sys.stdout.write("\r    link lost - is the Zune awake and on Wi-Fi? retrying ...            ")
            sys.stdout.flush()
            try:
                s and s.close()
            except OSError:
                pass
            s = None
            time.sleep(3)
    st[key] = dict(stamp=stamp, offset=e.size)
    save_state(st)
    sys.stdout.write("\r" + " " * 78 + "\r")
    return "done"


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(description="Load SCUMM games onto a Zune HD running the ScummVM tile.")
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--ip")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--yes", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--temp")
    a = ap.parse_args()
    paths = list(a.paths)
    if not paths:
        print("Drag a game folder, a disc image (.iso/.bin/.cue) or a .zip onto this window and press Enter.")
        print("Do this once per item (both discs for multi-disc games). Press Enter on an empty line when done.\n")
        while True:
            p = input("  path> ").strip().strip('"')
            if not p:
                break
            paths.append(p)
    if not paths:
        return 1
    temp = a.temp or tempfile.mkdtemp(prefix="zune_games_")
    entries = []
    try:
        for p in paths:
            try:
                got = collect(p, temp)
                print(f"  read {p}: {len(got)} files")
                entries += got
            except Exception as ex:
                print(f"  skipping {p}: {ex}")
        print("  recognising games ...")
        games, unclear = find_games(entries)
        for src, d, ids in unclear:
            where = os.path.basename(src.rstrip('/' + chr(92))) + ('/' + d if d else '')
            print(f"\n  '{where}' has SCUMM data, but it is a version I do not know and several games use those file names:")
            print('   could be: ' + ', '.join(f"{DETECT['titles'].get(i, i)} [{i}]" for i in ids))
            pick = '' if a.yes else input('   which one is it? type the id (or Enter to skip): ').strip().lower()
            if pick in ids:
                add_game(games, entries, src, d, pick, False, '')
        finish_games(games)
        if not games:
            print("\nNo SCUMM game recognised. (This tool handles Lucasfilm/LucasArts SCUMM games: Maniac Mansion, Zak McKracken,")
            print("Indiana Jones 3/4, Loom, Monkey Island 1/2/3, Day of the Tentacle, Sam & Max, Full Throttle, The Dig, ...)")
            return 1
        for gid, g in games.items():
            size = sum(e.size for _, e in g["files"])
            print(f"\n{g['title']}  [{gid}]{'  ' + g['info'] if g['info'] else ''}  ->  {ROOT}{BS}{gid}   ({len(g['files'])} files, {size / 1e6:.0f} MB)")
            for pr in g["problems"]:
                print("   !", pr)
            for m in g["missing"]:
                print("   ! missing", m)
            if g["missing"] and NOTES.get(gid):
                print("   (" + NOTES[gid] + ")")
        if a.list:
            return 0
        runnable = {k: v for k, v in games.items() if not v["missing"]}
        if not runnable:
            print("\nNothing complete enough to load. Add the missing files / disc and run again.")
            return 1
        if not a.yes and input(f"\nLoad {len(runnable)} game(s) to the Zune now? [Y/n] ").strip().lower() in ("n", "no"):
            return 0
        ip = find_zune(a.ip)
        while not ip:
            ip = input("Could not find the Zune. Wake it (same Wi-Fi, Lyra installed) and type its IP, or Enter to quit: ").strip()
            if not ip:
                return 1
            ip = ip if find_zune(ip) else None
        print(f"\nZune found at {ip}. Keep its screen on while this runs.")
        st = load_state()
        st["last_ip"] = ip
        save_state(st)
        for gid, g in runnable.items():
            files = g["files"]
            print(f"\n== {g['title']}")
            base = ROOT + BS + gid
            mkdirs(ip, base)
            for sub in sorted({r.rsplit("/", 1)[0] for r, _ in files if "/" in r}):
                mkdirs(ip, base + BS + sub.replace("/", BS))
            for i, (rel, e) in enumerate(sorted(files, key=lambda x: x[1].size), 1):
                print(f"  [{i}/{len(files)}] {rel}  ({e.size / 1e6:.1f} MB)")
                res = upload(ip, base + BS + rel.replace("/", BS), e, st, a.force)
                if res != "done":
                    print("    " + res)
        print("\nAll done. Open the ScummVM tile on the Zune; the games appear in its list.")
        return 0
    finally:
        if not a.temp:
            shutil.rmtree(temp, ignore_errors=True)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nStopped. Run it again to resume.")
        sys.exit(130)
