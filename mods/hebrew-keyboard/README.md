# hebrew-keyboard

Hebrew letters on the Zune HD's on-screen keyboard (firmware 4.5), with right-to-left typing. Needs the `hebrew-font` mod (letters) and `hebrew-rtl` (so Hebrew text elsewhere reads correctly).

## What it does
- **A third keyboard page.** The `123` / `abc` button now cycles **English -> 123 -> Hebrew -> 123 -> English**. The Hebrew page is Android-style:
  `ק ר א ט ו ן ם פ ת צ` / `ש ד ג כ ע י ח ל ך` / `ז ס ב ה נ מ ץ`; the final peh `ף` is the long-press of `פ`. Shift does nothing in Hebrew.
- **Right-to-left typing.** The keyboard's text box is a plain left-to-right edit. Hebrew is kept in *visual* order in it: each new letter (or the space after a
  word) goes to the left end of the current Hebrew run, so the text grows leftwards like real RTL typing. Backspace removes the newest letter (the left end of the word).
  English and digits typed after Hebrew go to the right end. Apps still receive normal (logical) text: the keyboard's get-text function returns a flipped copy.
- Works in every text box that uses the system keyboard (YouTube search, Wi-Fi names, ...). The page and the direction logic apply to the whole HUD keyboard.

## How it works (firmware 4.5, `zhud_serv.dll`, base `0x419b0000`, loaded into `servicesd`)
- The keyboard is `HudKeyboardScene` (`HudScenes.gem` / `Keyboard.xur`) in the HUD process, not in `gemstone`. The scene has three `XuiKeyStrip` rows; the key **letters are not in any gem file**,
  they are a table in zhud's read-only data: 16-byte entries `{wchar normal, shift, ?, ?; ptr popup, ptr popup}`. Rows: `q..p` at `0x419b2be0` (10 keys), `a..l` at `0x419b2c90` (9), `z..m` at `0x419b2d40` (7).
  Popup pointers lead to 10-entry blocks (the accent long-press, e.g. `e` -> `ë é e ê è`).
- The table is ROM-backed: `VirtualProtect` fails with error 87. It is written with Lyra's `lyra_patch_code` (16 bytes per key).
- Page switch: `SetPage(keyboard, page)` at `0x419c36f0` (0 = letters, 1 = numbers/symbols) is hooked; the English <-> Hebrew swap happens when returning from page 1.
- Typing: key presses go `KeyInput 0x419c3f78 -> SendKey 0x419c6824(keycode, char, flags, edit)`, which hands the key to the native edit at its own cursor. The mod hooks `SendKey`
  and moves the edit's cursor (`CUR_SET 0x419c23d8`, `CUR_GET 0x419c4a6c`) before the key is delivered. `GetBuffer 0x419c2914(kbd, flag)`: flag 0 is the get-text path for apps (returns a flipped copy), flag 1 the live edit text.
  `InsertText 0x419c3940` and `SetBuffer 0x419c37f8` are also hooked but only matter for the HUD's own buffer mode.
- `hebrew-rtl` must not flip the keyboard's edit a second time: it asks `KbdHebOwnsElem()` (exported by `kbdheb.dll`) and skips that element. That call is made **only in the HUD process**
  (`kbdheb.dll` is not mapped in `gemstone`; calling into it from there crashes the UI).

## Stages and safety
`\flash2\automation\kbdheb.stage` (first char, default `A`): `A` read-only check, `B` + hook on `SetPage` (log only), `C` the Hebrew page, **`D` = C plus the typing direction (complete)**.
Before hooking it writes `kbdheb.armed` (one byte, unstable boots seen) and deletes it after 120 s of uptime; after **5** unstable boots in a row it renames the file to `kbdheb.tripped` and stays inert (delete that file to re-enable).
Log: `\flash2\automation\kbdheb.log`. The hooks check the instruction words at each target before patching.
`hebrew-rtl` needs `\flash2\automation\rtlflip.ext` (any content) to leave the keyboard's text box alone.

## Hebrew search in the YouTube mod
`youtube-hebrew-search.patch` (against `project-lyra` `mods/youtube`) makes `ytsearchd` send the query as UTF-8 (it used to drop every non-ASCII character, so a Hebrew search arrived empty and found nothing)
and decode result text as UTF-8. It has to be rebuilt (`ytsearchd.exe`); the result rows then also need `hebrew-rtl` with `rtlflip.ytrows` present (see its README).

## Build
```bash
CEGCC=/path/to/cegcc-prefix ./build.sh hebrew-keyboard      # kbdheb.dll
python lyra-src/modkit/mod-apply.py apply mods/hebrew-keyboard --ip <zune-ip>
```
Then enable **Hebrew Keyboard** in the Mods tab, put `D` into `kbdheb.stage` and restart.

## Lessons from building it
- Writing the C source through a shell heredoc **halves doubled backslashes** (`L"\\flash2\\automation\\"` became `\f` and `\a` escapes): no log, no stage file, no guard, with no error. Write such files with an editor tool and check the strings in the built DLL.
- Hook the function that really runs: the first typing hooks (`InsertText`, `SetBuffer`) never fired; a log-only hook that prints its first calls found the real path in one restart.
- Never make a hook in one process call into a DLL that only another process mapped. Keep per-call work in a hook to plain memory reads; look up modules on a helper thread (and by walking the module list, `GetModuleHandle` by name did not find the mod DLL).
- A guard that arms for 2-3 minutes after boot cannot see a crash that happens later in the session; judge a change by whether the UI survives *use*, not only boot.
