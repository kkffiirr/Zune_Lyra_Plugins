# hebrew-rtl (EXPERIMENTAL, DO NOT ENABLE AS IS)

Reorders Hebrew text so it reads right-to-left on a renderer that only draws left-to-right.

- `flip_core.h` is plain C and unit-tested (`gcc -fshort-wchar test_flip.c && ./a.out`): whole line reversed, Latin/digit runs put back in reading order,
  brackets mirrored, lines handled separately, text without Hebrew untouched.
- `rtlflip.c` hooks `SetLabelText` (0x38434) and `RawRowLabel` (0x83914) in `gemstone.exe` (firmware 4.5) and passes the flipped text on.
- **It crashed the UI when scrolling lists.** Probable cause: the flipped text lives in a **stack buffer** that the UI keeps a pointer to.
  Fix before trying again: keep a permanent copy per distinct string (a bounded table), install the hooks in log-only mode first,
  and test with nothing else changed. Know the recovery routes in `docs/LESSONS.md` first.
