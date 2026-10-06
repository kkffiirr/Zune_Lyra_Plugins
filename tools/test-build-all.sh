#!/bin/bash
# Build every mod with CeGCC and report. usage: CEGCC=... LYRA_SRC=... tools/test-build-all.sh
cd "$(dirname "$0")/.."
for m in hebrew-font hebrew-rtl regprobe fontprobe playnext; do
  if bash ./build.sh $m > /tmp/build-$m.log 2>&1; then echo "OK    $m"; else echo "FAIL  $m (see /tmp/build-$m.log)"; tail -5 /tmp/build-$m.log; fi
done
echo "--- flip-logic unit test:"
gcc -fshort-wchar -Wall -Wno-pointer-sign -o /tmp/test_flip mods/hebrew-rtl/test_flip.c && /tmp/test_flip | tail -2
