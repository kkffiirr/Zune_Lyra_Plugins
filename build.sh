#!/bin/bash
# Build the native parts of a mod with the CeGCC (arm-mingw32ce) cross compiler. Run inside WSL/Linux.
#   ./build.sh hebrew-font | hebrew-rtl | regprobe | fontprobe
# Env: CEGCC=<prefix of a CeGCC build with bin/arm-mingw32ce-gcc>   (default below)
#      LYRA_SRC=<clone of github.com/project-lyra-zune/project-lyra> (default ./lyra-src)
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
CEGCC="${CEGCC:-$HOME/zune-scummvm/cegcc-prefix}"
LYRA_SRC="${LYRA_SRC:-$ROOT/lyra-src}"
export PATH="$CEGCC/bin:$PATH"
command -v arm-mingw32ce-gcc >/dev/null || { echo "arm-mingw32ce-gcc not found (set CEGCC)"; exit 1; }

mod="$1"; [ -n "$mod" ] || { sed -n '2,6p' "$0"; exit 2; }
W="$(mktemp -d /tmp/lyra-build-$mod.XXXX)"; cd "$W"
F="-O2 -D_WIN32_WCE=0x600 -DUNICODE -D_UNICODE"
OUT="$ROOT/mods/$mod"; mkdir -p "$OUT"
dll()  { arm-mingw32ce-gcc $F "$@"; }
sdk()  { # Lyra SDK + its logger, needed by mods that call lyra_hook_install
  mkdir -p sdk ce_log; cp -r "$LYRA_SRC/sdk/." sdk/; cp "$LYRA_SRC"/src/ce-common/src/ce_log/* ce_log/
  INC="-I sdk/include -I ce_log -I ."
  dll $INC -c sdk/src/lyra_client.c -o lc.o; dll $INC -c ce_log/ce_log.c -o cl.o
}

case "$mod" in
  hebrew-font)
    dll -shared -o fontload.dll "$ROOT/mods/hebrew-font/fontload.c" -lcoredll -ltoolhelp
    dll -o fontd.exe "$ROOT/mods/hebrew-font/fontd.c" -lcoredll
    cp fontload.dll fontd.exe "$OUT/" ;;
  hebrew-rtl)
    cp "$ROOT/mods/hebrew-rtl/"{rtlflip.c,flip_core.h} .; sdk
    dll $INC -c rtlflip.c -o rf.o
    dll -shared -o rtlflip.dll rf.o lc.o cl.o -lcoredll -ltoolhelp
    cp rtlflip.dll "$OUT/" ;;
  regprobe)
    dll -shared -o regprobe.dll "$ROOT/mods/regprobe/regprobe.c" -lcoredll; cp regprobe.dll "$OUT/" ;;
  fontprobe)
    dll -shared -o fontprobe.dll "$ROOT/mods/fontprobe/fontprobe.c" -lcoredll; cp fontprobe.dll "$OUT/" ;;
  *) echo "unknown mod: $mod"; exit 2 ;;
esac
ls -la "$OUT"
