"""Make the "ScummVM ZDK" Apps-tile kit from the existing ScummVM kit.

The XNA launcher (exploiter.exe) holds the path of the native program it starts as a UTF-16 string. The ZDK tile
starts scummvmzdk.exe instead of scummvmapp.exe; the two names have the same length, so the binary is patched in place.

  make_zdk_kit.py <existing deploykit dir with payload/exploiter.exe> <output dir>
Then deploy the output dir with zune-deploy (same as the normal tile) and put the other deploykit-zdk files next to it.
"""
import shutil, sys
from pathlib import Path

src, out = Path(sys.argv[1]), Path(sys.argv[2])
old, new = "scummvmapp.exe".encode("utf-16-le"), "scummvmzdk.exe".encode("utf-16-le")
assert len(old) == len(new)
exe = (src / "payload" / "exploiter.exe").read_bytes()
assert exe.count(old) == 2, "unexpected launcher binary"
(out / "payload").mkdir(parents=True, exist_ok=True)
(out / "payload" / "exploiter.exe").write_bytes(exe.replace(old, new))
for name in ("GameThumbnail.png",):
    if (src / name).exists():
        shutil.copy(src / name, out / name)
shutil.copy(Path(__file__).resolve().parents[1] / "deploykit-zdk" / "application.cfg", out / "application.cfg")
print("kit written to", out)
