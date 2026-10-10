#!/usr/bin/env python3
"""Load SCUMM games onto a Zune HD running the ScummVM tile. Plain Python 3, no extra packages.

  python load_games.py                      interactive: drag a folder / disc image / .zip in, press Enter
  python load_games.py PATH [PATH ...]      PATH = game folder, disc image (.iso .bin .img .cue) or .zip
                                            (give every disc of a multi-disc game in the same run)
  options: --ip ADDR   skip the search for the Zune        --list       only show what was found
           --yes       do not ask for confirmation         --force      upload again even if done before
           --temp DIR  where to unpack .zip files (needs free disk space)

The Zune must be awake, on the same Wi-Fi as this PC, and running Project Lyra (its daemon listens on port 1337).
Games land in \\flash2\\scummvm\\games\\<id>\\ (ids: tentacle, samnmax, comi). Uploads resume if the link drops.
"""
import argparse, concurrent.futures as cf, json, os, shutil, socket, struct, sys, tempfile, time, zipfile

PORT, CHUNK = 1337, 16384
BS = chr(92)
ROOT = BS + "flash2" + BS + "scummvm" + BS + "games"
HERE = os.path.dirname(os.path.abspath(__file__))
STATE_FILE = os.path.join(HERE, "load_games_state.json")

# id -> title, files that must exist (all), optional files, extra folder (copied whole, keeping its name)
GAMES = {
    "tentacle": dict(title="Day of the Tentacle", need=["TENTACLE.000", "TENTACLE.001"], want=["MONSTER.SOU"], folder=None,
                     note="speech needs MONSTER.SOU (MONSTER.SOF, the FLAC version, is not supported)"),
    "samnmax": dict(title="Sam & Max Hit the Road", need=["SAMNMAX.000", "SAMNMAX.001"], want=["MONSTER.SOU"], folder=None,
                    note="speech needs MONSTER.SOU"),
    "comi": dict(title="The Curse of Monkey Island", need=["COMI.LA0", "COMI.LA1", "COMI.LA2"], want=[], folder="RESOURCE",
                 note="needs both discs (COMI.LA1 is on disc 1, COMI.LA2 on disc 2) and their RESOURCE folders"),
}


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


def file_reader(path):
    def read(offset, n):
        with open(path, "rb") as f:
            f.seek(offset)
            return f.read(n)
    return read


def entries_from_image(path):
    img = DiscImage(path)
    return [Entry(path, rel, ln, (lambda o, n, lba=lba, ln=ln: img.read(lba, ln, o, n))) for rel, lba, ln in img.walk()]


def entries_from_zip(path, temp):
    dest = os.path.join(temp, os.path.splitext(os.path.basename(path))[0])
    print(f"  unpacking {os.path.basename(path)} (needs free disk space) ...")
    with zipfile.ZipFile(path) as z:
        z.extractall(dest)
    return collect(dest, temp)


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


# ------------------------------------------------------------------ recognising games
def find_games(entries):
    """Returns {id: (files, problems)} with files = [(remote relative path, Entry)]."""
    by_name = {}
    for e in entries:
        by_name.setdefault(e.rel.split("/")[-1].upper(), []).append(e)
    found = {}
    for gid, g in GAMES.items():
        if not all(n in by_name for n in g["need"]) and not any(n in by_name for n in g["need"]):
            continue
        files, problems, seen = [], [], {}
        def add(rel, e):
            key = rel.upper()
            if key in seen:
                if seen[key].size != e.size:
                    problems.append(f"{rel}: two different copies (sizes {seen[key].size} and {e.size}); using the first")
                return
            seen[key] = e
            files.append((rel, e))
        for n in g["need"]:
            if n in by_name:
                add(by_name[n][0].rel.split("/")[-1], by_name[n][0])
        srcs = {e.src for e in seen.values()}
        for n in g["want"]:
            cands = by_name.get(n, [])
            same = [e for e in cands if e.src in srcs] or cands
            if same:
                add(n, same[0])
        if g["folder"]:
            for e in entries:
                parts = e.rel.split("/")
                if len(parts) >= 2 and parts[-2].upper() == g["folder"]:
                    add(g["folder"] + "/" + parts[-1], e)
        for n in g["need"]:
            if n not in by_name:
                problems.append(f"missing {n}")
        for n in g["want"]:
            if n not in by_name:
                problems.append(f"no {n} (the game will run, without speech)")
        found[gid] = (files, problems)
    if len([g for g in found if "MONSTER.SOU" in GAMES[g]["want"]]) > 1:
        print("  note: two games here use a file named MONSTER.SOU; if the speech comes out wrong, load them in separate runs.")
    return found


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
        games = find_games(entries)
        if not games:
            print("\nNo supported game found. Supported: " + ", ".join(g["title"] for g in GAMES.values()) + ".")
            return 1
        total = 0
        for gid, (files, problems) in games.items():
            size = sum(e.size for _, e in files)
            total += size
            print(f"\n{GAMES[gid]['title']}  ->  {ROOT}{BS}{gid}   ({len(files)} files, {size / 1e6:.0f} MB)")
            for pr in problems:
                print("   !", pr)
            if any(x.startswith("missing") for x in problems):
                print("   (" + GAMES[gid]["note"] + ")")
        if a.list:
            return 0
        runnable = {g: v for g, v in games.items() if not any(x.startswith("missing") for x in v[1])}
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
        for gid, (files, _) in runnable.items():
            print(f"\n== {GAMES[gid]['title']}")
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
