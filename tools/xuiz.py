"""Minimal XUIZ (.gem) reader, ported from lyra-src/src/zuxhook/formats/mods_xuiz.c (decode only)."""
import struct

def u16be(b, o): return struct.unpack_from(">H", b, o)[0]
def u32be(b, o): return struct.unpack_from(">I", b, o)[0]
def u24be(b, o): return (b[o] << 16) | (b[o + 1] << 8) | b[o + 2]
def u16le(b, o): return struct.unpack_from("<H", b, o)[0]

def decode(d):
    """Return [(name, data_offset_in_file, size)] for every entry."""
    if d[:4] != b"XUIZ" or u32be(d, 8) != len(d):
        raise ValueError("not an XUIZ file")
    count = u16be(d, 0x14)
    content_base = 0x16 + u32be(d, 0x10)
    out = []; p = 0x16; idx = 0; prev = -1
    while len(out) < count and p < len(d) - 8:
        if idx == 0:
            sb, size = 4, u32be(d, p)
        elif idx == 1:
            sb, size = 3, u24be(d, p)
        else:
            u16v = u32v = False
            if d[p] == 0 and p + 7 <= len(d):
                s16 = u16be(d, p + 1); o16 = u32be(d, p + 3)
                u16v = content_base + o16 + s16 <= len(d) and o16 >= prev
            if p >= 1 and d[p - 1] == 0 and p + 7 <= len(d):
                s32 = u32be(d, p - 1); o32 = u32be(d, p + 3)
                u32v = s32 > 0xFFFF and content_base + o32 + s32 <= len(d) and o32 >= prev
            if u32v and not u16v: sb, size = 4, s32; p -= 1
            elif u16v:            sb, size = 2, s16; p += 1
            else: raise ValueError("bad entry header at %d" % p)
        off = u32be(d, p + sb)
        nch = u16le(d, p + sb + 4)
        noff = p + sb + 6
        nbytes = nch * 2
        raw = d[noff:noff + nbytes]
        if noff + nbytes > content_base and noff < content_base:
            raw = d[noff:content_base] + b"\x00"
        name = raw.decode("utf-16-le", "replace")
        out.append((name, content_base + off, size))
        prev = off; idx += 1; p = noff + nch * 2
    if len(out) != count: raise ValueError("entry count mismatch")
    return out

if __name__ == "__main__":
    import sys
    d = open(sys.argv[1], "rb").read()
    for n, o, s in decode(d):
        mark = ""
        if "Zegoe".encode("utf-16-le") in d[o:o + s]: mark = "   <-- has Zegoe"
        print("%-40s off=%7d size=%7d%s" % (n, o, s, mark))
