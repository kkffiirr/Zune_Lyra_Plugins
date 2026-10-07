# hebrew-rtl

Shows Hebrew in reading order on the Zune HD (firmware 4.5). The Zune draws every string left to right with no bidi, so the text has to be
reordered before it is drawn: whole line reversed, runs of Latin letters and digits put back in reading order, brackets mirrored, each line on its own.
Pair it with the `hebrew-font` mod (which provides the letters).

## Status
Works on a device (checked by eye on the screens tried: Now Playing, the next-songs queue, the songs list, Albums and Artists lists, hub tiles).
Not exhaustively tested. Hebrew text that does not come from the media library (for example file names or settings) is left alone on purpose.
Known limitation: Hebrew text that **wraps onto several lines** (for example a long album name in a narrow tile) comes out with its lines in the wrong order, because the string is reversed as a whole before the UI wraps it. Fixing that needs the flip to happen per drawn line.
Known cosmetic issue: the log line `prop-flip #1 ...` can repeat (the counter it prints only advances on real flips).

## How it is built (and why it is done this way)
- **Where the text comes from.** Track title, artist and album are fetched by one gemstone function, `0x748d0` (a metadata string getter, 112 callers):
  `GetStr(obj, propId, buf, cch, extra)` with `propId` 0x20001 = title, 0x20002 = artist, 0x20003 = album. The mod detours it and, after the call,
  flips the returned string **in place** for those three ids when it contains Hebrew. That one point covers every list.
- **Labels.** `SetLabelText` (`gemstone+0x38434`) is also detoured, for text that does not come from the getter.
- **Copy sites.** The string copies that build row text (coredll `StringCchCopyW`/`StringCchCopyExW`, `gemstone` import slots `0x962a8` and `0x96228`) are wrapped
  by swapping the import-table pointer (no code patching, all arguments forwarded). They flip only at allow-listed call sites (`ROW_LR`, `EXROW_LR`),
  because the same routines also copy file names and ids that must never be reordered. With the getter flip these are belt and braces.
- **Hook points are checked first.** The two code detours (`0x38434`, `0x748d0`) have position-independent first instructions, which `tools/hookcheck.py`
  verifies offline (static check plus an emulated run of the trampoline); the mod re-checks the exact instruction words on the device before patching.
- Text already flipped is remembered (hash + length) and passed through, because the UI re-sets labels from text it already displays.
- Flipped text goes into permanent buffers, so no pointer can dangle.

## Stages and safety
Write one letter into `\flash2\automation\rtlflip.stage` (default `A`):
`A` guard only, nothing patched; `B` label hook, text unchanged; `C` label hook, flips; `P` = C plus a log-only probe of the two copy routines;
`R` = P plus flipping at the allow-listed copy sites; `Q` = R plus a log-only hook on the getter; **`G` = Q plus flipping in the getter (the complete fix)**.
Before patching, the mod writes `rtlflip.armed` (one byte: how many unstable boots in a row it has seen) and deletes it after 3 minutes of uptime.
If the file is still there at the next load, the previous boot ended inside that window (a crash, or just a restart); the counter goes up and the hooks are installed again.
After **5 unstable boots in a row** no hook is installed and the file becomes `rtlflip.tripped`; delete that file to re-enable. A boot that stays up 3 minutes resets the count.
Log: `\flash2\automation\rtlflip.log`. Recovery routes: `docs/LESSONS.md`.

## Finding more call sites
Run stage `Q`, use the screens that are still reversed, read the log: `copy`/`copyex`/`prop` lines give the caller address (`lr=`), the property id and the Hebrew text.
Check what the caller does (disassemble the dumped `gemstone` image around `lr-4`) before adding it to the allow-list.

## Tests
`flip_core.h` is plain C: `gcc -fshort-wchar test_flip.c && ./a.out`; `fuzz_flip.c` runs 300,000 random strings under ASan/UBSan
(`gcc -fshort-wchar -fsanitize=address,undefined fuzz_flip.c`). Build the DLL with `./build.sh hebrew-rtl` (CeGCC).
