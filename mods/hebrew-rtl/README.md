# hebrew-rtl (PARTIALLY WORKING)

Shows Hebrew in reading order on the Zune HD (firmware 4.5). The Zune draws every string left to right with no bidi, so the text has to be
reordered before it is drawn: whole line reversed, runs of Latin letters and digits put back in reading order, brackets mirrored, each line on its own.
Pair it with the `hebrew-font` mod (which provides the letters).

## What works / what does not
| Where | Status |
|---|---|
| Now Playing title/artist and other labels set through `SetLabelText` (`gemstone+0x38434`) | works, checked on a device |
| Artist names in list rows (copy at return address `0x27de8`) | works, checked on a device |
| Now Playing "next songs" small text (extended copy at `0x295d4`) | implemented in 0.7, **not yet checked on a device** |
| Albums and Artists lists, hub tiles, the songs list reached from "next songs" | **still reversed**: their text does not pass through any hook point found so far |

## How it is built (and why it is done this way)
- Only `SetLabelText` is detoured with `lyra_hook_install`. Its first two instructions are position independent, which is checked offline by `tools/hookcheck.py`
  (static check plus an emulated run of the generated trampoline) and again on the device against the exact expected words before patching.
- List text goes through coredll `StringCchCopyW` (`gemstone` import slot `0x962a8`) and `StringCchCopyExW` (`0x96228`). These are **not** code-patched:
  the import-table pointer is swapped for a wrapper that forwards all arguments (3 and 6, checked from the disassembly). The wrapper flips only at
  call sites on an allow-list (`ROW_LR`, `EXROW_LR`), because the same copy routine also handles file names and ids that must never be reordered.
- Text already flipped is remembered (hash + length) and passed through, because the UI sometimes re-sets a label from text it already displays.
- Flipped text goes into permanent buffers, so no pointer can dangle.

## Stages and safety
Write one letter into `\flash2\automation\rtlflip.stage` (default `A`):
`A` guard only, nothing patched; `B` label hook, text unchanged; `C` label hook, flips; `P` = C plus a log-only probe of the two copy routines;
`R` = P plus flipping at the allow-listed call sites.
Before patching, the mod creates `rtlflip.armed` and deletes it after 5 minutes of uptime. If it is still there at the next load (the UI died or the device was
restarted inside that window) no hook is installed and the flag becomes `rtlflip.tripped`; delete that file to re-enable. Delete `rtlflip.armed` yourself
before a deliberate restart in the first 5 minutes. Log: `\flash2\automation\rtlflip.log`. Recovery routes: `docs/LESSONS.md`.

## Finding more call sites
Run stage `P` or `R`, use the screens that are still reversed, read the log: `copy`/`copyex` lines give the caller address (`lr=`) and the Hebrew text.
Check what the caller does (disassemble the dumped `gemstone` image around `lr-4`) before adding it to the allow-list.

## Tests
`flip_core.h` is plain C: `gcc -fshort-wchar test_flip.c && ./a.out`; `fuzz_flip.c` runs 300,000 random strings under ASan/UBSan
(`gcc -fshort-wchar -fsanitize=address,undefined fuzz_flip.c`). Build the DLL with `./build.sh hebrew-rtl` (CeGCC).
