"""Minimal ISO9660 extractor for a MODE1/2352 raw .BIN (user data at +16 in each 2352-byte sector)."""
import struct, sys, os
BIN, OUT = sys.argv[1], sys.argv[2]
want = [w.upper() for w in sys.argv[3:]]
f = open(BIN, "rb")
def sector(n):
    f.seek(n * 2336 + 8); return f.read(2048)
def read_extent(lba, length):
    out = bytearray()
    n = lba
    while len(out) < length:
        out += sector(n); n += 1
    return bytes(out[:length])
pvd = sector(16)
assert pvd[1:6] == b"CD001", "no ISO9660 PVD"
root = pvd[156:190]
rlba, rlen = struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0]
def walk(lba, length, path=""):
    data = read_extent(lba, length); i = 0
    while i < len(data):
        l = data[i]
        if l == 0: i = (i // 2048 + 1) * 2048; continue
        flags = data[i + 25]; nlen = data[i + 32]
        name = data[i + 33:i + 33 + nlen].decode("latin1")
        elba = struct.unpack("<I", data[i + 2:i + 6])[0]; elen = struct.unpack("<I", data[i + 10:i + 14])[0]
        if name not in ("\x00", "\x01"):
            clean = name.split(";")[0]
            if flags & 2: yield from walk(elba, elen, path + clean + "/")
            else: yield (path + clean, elba, elen)
        i += l
for p, lba, ln in walk(rlba, rlen):
    print(f"{ln:>12}  {p}")
    if want and p.upper() in want:
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, os.path.basename(p)), "wb") as o:
            n, left = lba, ln
            while left > 0:
                d = sector(n); o.write(d[:min(2048, left)]); left -= 2048; n += 1
        print("   -> extracted", os.path.basename(p))
