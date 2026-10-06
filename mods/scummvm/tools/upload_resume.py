#!/usr/bin/env python3
"""Resumable big-file upload to the Zune via Lyra opcode 12 (write at explicit offsets).

Reconnects forever after a drop (the Zune sleeps its WiFi) and continues from the last acknowledged
byte. A checkpoint file also lets a restarted run resume. Offset 0 creates/truncates the file, any
other offset writes in place, so a retried chunk is harmless.

  upload_resume.py <ip> <local> <remote> [--ckpt FILE]
"""
import argparse, json, os, socket, struct, sys, time

CHUNK = 16384

def read_exact(s, n):
    out = bytearray()
    while len(out) < n:
        c = s.recv(n - len(out))
        if not c: raise ConnectionError("socket closed")
        out.extend(c)
    return bytes(out)

def connect(ip, port, timeout):
    s = socket.create_connection((ip, port), timeout=timeout)
    s.settimeout(timeout)
    if read_exact(s, 6) != b"Hello\n": raise ConnectionError("bad banner")
    return s

def write_chunk(s, path, off, data):
    h = bytearray(32); h[0] = 12
    h[1:5] = struct.pack("<I", len(path)); h[5:9] = struct.pack("<I", off); h[9:13] = struct.pack("<I", len(data))
    s.sendall(h); s.sendall(path); s.sendall(data)
    r = read_exact(s, 32)
    if r[0] != 12: raise ConnectionError(f"bad response {r[0]:#x}")
    return struct.unpack("<I", r[1:5])[0], struct.unpack("<I", r[5:9])[0], r[9] != 0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ip"); ap.add_argument("local"); ap.add_argument("remote")
    ap.add_argument("--port", type=int, default=1337); ap.add_argument("--timeout", type=float, default=8.0)
    ap.add_argument("--ckpt", default=None)
    a = ap.parse_args()
    ckpt = a.ckpt or (a.local + ".upload.json")
    size = os.path.getsize(a.local)
    path = a.remote.encode()
    off = 0
    if os.path.exists(ckpt):
        try:
            j = json.load(open(ckpt))
            if j.get("remote") == a.remote and j.get("size") == size: off = j["offset"]
        except Exception: pass
    print(f"upload {a.local} -> {a.remote}  size={size}  starting at {off}", flush=True)
    f = open(a.local, "rb")
    s = None; last_pct = -1; t0 = time.time(); start_off = off
    while off < size:
        try:
            if s is None:
                s = connect(a.ip, a.port, a.timeout)
                print(f"  connected, resuming at {off}", flush=True)
            f.seek(off)
            data = f.read(CHUNK)
            bw, err, ok = write_chunk(s, path, off, data)
            if not ok or bw == 0:
                raise ConnectionError(f"write failed at {off}: bw={bw} err={err}")
            off += bw
            if (off // CHUNK) % 64 == 0:
                json.dump({"remote": a.remote, "size": size, "offset": off}, open(ckpt, "w"))
            pct = int(100 * off / size)
            if pct != last_pct and pct % 5 == 0:
                rate = (off - start_off) / max(1, time.time() - t0) / 1024
                print(f"  {pct}%  {off}/{size}  {rate:.0f} KB/s", flush=True); last_pct = pct
        except (OSError, ConnectionError) as e:
            print(f"  link lost at {off}: {e}; retrying...", flush=True)
            try: s and s.close()
            except OSError: pass
            s = None
            time.sleep(3)
    json.dump({"remote": a.remote, "size": size, "offset": off}, open(ckpt, "w"))
    print("complete", flush=True)

if __name__ == "__main__":
    main()
