# How Hebrew was added to the Zune HD UI: a reproducible walkthrough

This is the full story of the font work: what the device does, how each step was done, the exact values to check against,
and every approach that **failed** (with the evidence), so you can repeat it, adapt it to another script, or avoid the dead ends.

Environment used: Windows 11 PC, WSL2 Ubuntu 26.04, Zune HD on firmware 4.5, Project Lyra 1.4.0, Python 3.13 with fontTools 4.66.1,
CeGCC `arm-mingw32ce-gcc` 9.3.0. Everything here was verified on a real device unless marked **(untested)**.

## 0. Result and the one-paragraph explanation

The Zune's UI font family, **Zegoe UI** (six weights), contains about 500 glyphs and **no Hebrew**, so Hebrew text is drawn as rectangles.
The Zune already has Windows CE *font linking* enabled: when a font lacks a glyph, GDI looks it up in a "link list" of other fonts.
The list for every Zegoe weight ships pointing at the Japanese and Korean fonts. The fix is therefore to (1) build a font that contains
Hebrew glyphs, (2) put it on the device, and (3) **append it to the link list of every Zegoe weight** with one `lyra.registry_write` per weight.
A tiny boot helper also registers the fonts so the Hebrew-extended copies of four weights replace the ROM ones directly.
Hebrew then shows everywhere, in **reversed order** unless the `hebrew-rtl` mod fixes that screen (the Zune has no right-to-left layout; see `OTHER-LANGUAGES.md`).

## 1. How the Zune's text stack behaves (facts established on the device)

| Fact | How it was established |
|---|---|
| UI text is drawn through a GDI-like API: `EnumFontFamilies`, `CreateFontIndirect`, `ExtTextOut`, `GetPixel` on a memory bitmap all work in the UI process | `mods/fontprobe`, `mods/hebrew-font/fontload.c` |
| System fonts live in the **read-only firmware ROM** under `\Windows\`: `ZegoeUI.ttf`, `ZegoeUI_L/SL/SB/B/Blk.ttf`, `MeiryoForZune.ttf`, `MalgunForZune.ttf`, `arial.ttf`, ... | `GetFileAttributes` returns attr `0x47` (read-only, hidden, system, in-ROM) |
| 20 faces are registered: Arial, Arial Black, Courier New, Times New Roman, Verdana, Georgia, Impact, Trebuchet MS, Webdings, Wingdings, Malgun for Zune, Meiryo for Zune, Zegoe UI x6, plus the two "@" vertical-writing variants of Malgun and Meiryo | `EnumFontFamiliesW` dump in `fontprobe.log` |
| The default system font (`HKLM\SYSTEM\GDI\SYSFNT`) is Arial; text that names no font uses it | `mods/regprobe` registry dump |
| `AddFontResourceW` works at runtime from our own code and the new face is listed **first** | `fontprobe.log`: `AddFontResourceW(test) -> 1 err=0` and the face appears at index 0 |
| Font linking is **on**: `HKLM\SYSTEM\GDI\FontLinkMethods = 1` and `HKLM\SOFTWARE\Microsoft\FontLink\SystemLink` has a value per face | `regprobe.log` (full dump in section 7) |

## 2. Step A: get the firmware image

The fonts are inside the Zune HD firmware package, not on the PC.

- Package: **`PavoBaseline.cab`** (Pavo = Zune HD), version 4.5. Source used: Internet Archive item `updating-the-zune-firmware`, file `Firmware Package/PavoBaseline.cab`.
- Expected: size **28,384,856** bytes, SHA-1 **`484a0d5c0a19c813c7a4d8fbcd8c1e32fd65057f`**.
- Unpack with 7-Zip: `7z x PavoBaseline.cab -oextract` gives `NK.bin` (12 MB, Windows CE kernel image), **`EXT.bin`** (39,366,567 bytes), `Recovery.bin`, `ZBoot.bin`.
- **`EXT.bin`** SHA-1 should be **`88471c30dfcbc6bb3818fece692008ce46b59181`**. It is a Windows CE BIN image (starts `B000FF\n`) whose files are stored
  **uncompressed**, so the TrueType files can be cut out directly. (`NK.bin` holds the UI resource packages, `scenes_standard.gem`, `skin_default.gem`, ...)

## 3. Step B: carve the fonts out of `EXT.bin`

A TrueType file starts with the 4 bytes `00 01 00 00`, then a big-endian table count and a table directory of 16-byte records
(`tag, checksum, offset, length`). The file length is the maximum `offset + length` over all tables. The carver in `tools/make-fonts.py`
(`carve_zegoe`) does exactly that: it scans for the signature, validates the directory (table count 5..30, printable tags, `cmap`+`glyf`+`head` present,
offsets inside the image), cuts `start .. start+maxEnd`, and keeps it if `fontTools` can read its full name.

Zegoe UI fonts found (offsets are inside `EXT.bin`):

| Face (name ID 4) | Offset | Size | Glyphs | OS/2 weight |
|---|---|---|---|---|
| Zegoe UI | 20574699 | 31884 | 509 | 400 |
| Zegoe UI Light | 20606595 | 34148 | 507 | 300 |
| Zegoe UI Bold | 20640743 | 31876 | 507 | 700 |
| Zegoe UI Black | 20672619 | 45604 | 498 | 800 |
| Zegoe UI Semibold | 20718223 | 46252 | 509 | 600 |
| Zegoe UI SemiLight | 20764475 | 34768 | 506 | **400** |

Other fonts in the same image (not modified): Meiryo for Zune (9.8 MB, 25,138 mapped characters), Malgun for Zune (2.3 MB), subsets of Arial, Georgia,
Times New Roman, Verdana, Trebuchet MS, Courier New, Impact, Wingdings, Webdings. **None of them maps a single Hebrew letter** (checked with `getBestCmap()`).

## 4. Step C: confirm the problem on the device (the measurement trick)

Do not trust your eyes, and do not trust width queries: `GetCharWidth32` returned **26 for every character** (even `A` and a private-use code point), so it is useless.
What works: draw the glyph into a 64x64 memory bitmap with the face and compare pixels.

`render_hash()` in `mods/hebrew-font/fontload.c`: create a compatible DC and bitmap, `PatBlt(BLACKNESS)`, `CreateFontIndirectW` with the face, `ExtTextOutW`
one character in white, then read all 4096 pixels with `GetPixel` and hash them. A missing glyph draws the font's `.notdef` rectangle, which has a fixed hash
(here `14720398`, 317 lit pixels). A real glyph gives a different hash. `has_alef()` compares U+05D0 against the rectangle and logs `BOX` or `HEBREW`.

Baseline on the stock device: every Zegoe face logged `BOX`.

## 5. Step D: build Hebrew-extended Zegoe fonts

`python tools/make-fonts.py path/to/EXT.bin` (needs `pip install fonttools` and Windows' Segoe UI). For each weight it opens the carved Zegoe font and
copies glyphs from the matching **Segoe UI** font (the two families are closely related, both are 2048 units per em, and the result looked consistent; no formal design relationship was checked):

| Zegoe weight | Output | Hebrew glyphs from (version 5.71 on the build PC) |
|---|---|---|
| Zegoe UI | `ZegoeUI.ttf` | `segoeui.ttf` |
| Light | `ZegoeUI_L.ttf` | `segoeuil.ttf` |
| SemiLight | `ZegoeUI_SL.ttf` | `segoeuisl.ttf` |
| Semibold | `ZegoeUI_SB.ttf` | `seguisb.ttf` |
| Bold | `ZegoeUI_B.ttf` | `segoeuib.ttf` |
| Black | `ZegoeUI_Blk.ttf` | `segoeuib.ttf` (**Segoe UI Black has no Hebrew**, so Bold's is used) |

What the merge does, in order (`add_scripts`):

1. Characters copied: U+05B0-05C7 (points/niqqud), U+05D0-05EA (letters), U+05F0-05F4 (Yiddish ligatures, punctuation), U+20AA (shekel sign), U+200E-200F (direction marks).
   Result: **57 new glyphs** per font. Afterwards 59 code points in these ranges are mapped (27 letters, 24 points, 5 Yiddish/punctuation, shekel sign, two direction marks);
   the difference is characters Zegoe already had. Anything already in the Zegoe cmap is left alone.
2. Each source glyph is drawn through a `DecomposingRecordingPen` (Segoe's niqqud are composites; a plain glyph pen fails with "NoneType is not iterable"),
   then replayed through a `TransformPen` that scales by `unitsPerEm(Zegoe) / unitsPerEm(Segoe)` = `2048/2048` = 1 here, into a `TTGlyphPen`.
3. Glyph name `uniXXXX`, advance width and left side bearing scaled and written to `hmtx`; every Unicode `cmap` subtable gets the mapping.
4. `glyf` order, `maxp.numGlyphs` updated; `post` table set to **format 3.0** (no glyph names needed on the device).
5. Saved with fontTools. File sizes grow by about 4 KB each (e.g. `ZegoeUI.ttf` 31,884 -> about 36,000 bytes).

Two derived copies are also written (used by the link entries): `ZegoeHeb_R.ttf` (from `ZegoeUI.ttf`) and `ZegoeHeb_SL.ttf` (from SemiLight), with the
name table's family (IDs 1, 4, 16) changed to **`Zegoe UH`** and **`Zegoe UH SemiLight`** (a unique name; same length as the originals).

Check: `getBestCmap()` shows all 27 letters (`27/27`) for every file. Render a test string with PIL to see them (this is how it was confirmed before touching the device).

## 6. Step E: get the fonts onto the device and registered

- Files travel with the mod: `mod-apply.py apply mods/hebrew-font --ip <zune>` mirrors them to `\flash2\automation\mods\hebrew-font\`.
  Any file referenced by `@/name` in `manifest.json` is shipped, so the changelog text lists every font as `@/...` (that is what pulls them into the deploy).
- **`fontd.exe`** (`mods/hebrew-font/fontd.c`), a boot daemon (`"daemons": [{"binary": "@/fontd.exe"}]`), calls `AddFontResourceW` on all eight files one to two seconds
  after the boot starts (its log shows it succeeded on the first try), and would retry until each returns > 0, then sleeps forever (the fonts may be dropped if the process exits).
  Log: `\flash2\automation\fontd.log`, e.g. `==== 8/8 fonts added after 1 tries`.
- `fontload.dll` (loaded into the UI process with `lyra.load_module`) repeats the check and adds the fonts itself as a fallback.

**Result of this step alone: 4 of 6 weights fixed, 2 not.** Light, Semibold, Bold and Black drew real Hebrew; plain **Zegoe UI** and **SemiLight**
(both OS/2 weight 400) kept drawing the box although the same-named Hebrew copy was registered. On screen: bold titles in Hebrew, regular lines still rectangles.
Why exactly the 400-weight faces lose to their ROM originals was not determined.

## 7. Step F: the fix that covered every weight (font linking)

Dump of the Zune's registry (`mods/regprobe`, read-only):

```
HKLM\SYSTEM\GDI                          FontLinkMethods = 0x00000001 (DWORD)
HKLM\SYSTEM\GDI\SYSFNT                   Nm = "Arial"
HKLM\SOFTWARE\Microsoft\FontLink\SystemLink
  [Zegoe UI]            = "\Windows\MeiryoForZune.ttf,Meiryo for Zune;\Windows\MalgunForZune.ttf,Malgun For Zune"
  [Zegoe UI Bold] / [Semibold] / [Light] / [Black] / [SemiLight]   (identical values)
  [Arial] [Courier New] [Georgia] [Times New Roman] [Verdana]       (identical values)
```

Format: one `REG_SZ` per base face, entries `"<font file path>,<face name>"` separated by `;`. If the base face lacks a character, GDI tries the linked fonts in order.
The change (in `mods/hebrew-font/manifest.json`, one action per Zegoe weight):

```json
{ "type": "lyra.registry_write", "hive": "HKLM", "key": "SOFTWARE\\Microsoft\\FontLink\\SystemLink",
  "name": "Zegoe UI", "value_type": "SZ",
  "value": "\\Windows\\MeiryoForZune.ttf,Meiryo for Zune;\\Windows\\MalgunForZune.ttf,Malgun For Zune;\\flash2\\automation\\mods\\hebrew-font\\ZegoeHeb_R.ttf,Zegoe UH" }
```

Repeat for `Zegoe UI SemiLight`, `Light`, `Semibold`, `Bold`, `Black`. (Keep the original two entries and **append** ours; the JSON needs doubled backslashes.)

**Timing observation:** the boot that first applied this looked unchanged (`state: Zegoe UI alef -> BOX` in `fontload.log`); a later normal restart showed **every face
drawing Hebrew**, including the fonts we never touched (Arial, Verdana, Times, Georgia, Impact, Trebuchet, Meiryo, Malgun). Our explanation (**unproven**): the Zune
keeps the registry change from one boot and uses it from the next clean shutdown/boot onward. Restart cleanly (not by pulling power) once or twice before judging.

Final log (`tools/read-fontlog.py <ip>`):

```
state: Zegoe UI             alef lit=282 -> HEBREW
state: Zegoe UI SemiLight   alef lit=282 -> HEBREW
state: Zegoe UI Light/Semibold/Bold/Black ... -> HEBREW
state: Arial / Verdana / Times New Roman / Georgia / Impact / Trebuchet MS / Courier New ... -> HEBREW
```

## 8. Everything that did NOT work (so you can skip it)

| Attempt | What happened | Evidence / reason |
|---|---|---|
| Set `HKLM\SOFTWARE\Microsoft\FontPath` to a folder with our fonts (Windows CE's documented font-folder key) | no effect | Lyra writes the key after the graphics system started; log shows `[key created]` on every boot |
| Copy the fonts into `\Windows\` with `lyra.write_blob_bytes` | impossible | the action only accepts bytes that Lyra's own code supplies, not a file from a manifest (`ModActionGetBytes` reads only synthetic args) |
| Same-name `AddFontResourceW` only | 4 of 6 weights | section 6 |
| `RemoveFontResourceW` on the ROM font, then add ours | refused | returns 0, `err=2` (the ROM fonts are not removable resources) |
| Redirect the UI's font requests by patching import-table slots of `CreateFontIndirectW`/`CreateFontW` in every module of the UI process | patched **0** slots | the UI does not call font creation through its import table; `CreateFontW` was not even exported by that name |
| Rename the font in the UI skin (`skin.xur` string table: length-prefixed UTF-16 `Zegoe UI` -> `Zegoe UH`) and replace the entries with `lyra.gem_replace_entry` | skin never applied | Lyra rewrites the whole 570 KB `skin_default.gem` into `\Windows`, a RAM store with ~600 KB free: `flush: short write 126976/570392` |
| Unique-name fonts (`ZegoeHeb UI`) on their own | draw Hebrew fine in our test | nothing in the UI asks for those names |

## 9. Verify your own run

1. `python lyra-src/modkit/mod-apply.py validate mods/hebrew-font`
2. `python lyra-src/modkit/mod-apply.py apply mods/hebrew-font --ip <zune-ip>`
3. `python tools/verify-fontmod.py <zune-ip>`: every file must print `MATCH`; `enabled.json` should list `hebrew-font`; `boot.state` should be `normal`, `failures: 0`.
4. Zune: Mods > Archived > Hebrew Font > enable; restart cleanly (twice if needed).
5. `python tools/read-fontlog.py <zune-ip>` and `python tools/read-bootlog.py <zune-ip> hebrew-font` (the boot log shows each `registry_write` and `spawn_daemon`, `0 failed`).

## 10. Open question: what is the minimal recipe? (untested)

The deployed mod uses **both** mechanisms (daemon-registered same-name fonts and the link entries) and was only tested in that combination. The link entries alone
probably suffice, since the link list is consulted for every missing glyph. To test: deploy a manifest containing only the six `registry_write` actions
and a daemon (or the DLL) that adds only `ZegoeHeb_R.ttf`; if every face still logs `HEBREW`, the six Hebrew-extended Zegoe copies and the same-name override are unnecessary,
and you could ship a single small open-licence Hebrew font (Noto Sans Hebrew, SIL OFL) as the link target instead, with no Microsoft font modified.

## 11. Pitfalls

- The Zune's IP changes with the Wi-Fi network; its file service stalls when it sleeps (see `LESSONS.md`).
- Read the `hebrew-rtl` README (stages, boot guard) before enabling it; it patches the UI process.
- Do not redistribute the generated fonts (Microsoft). `THIRD-PARTY.md` has the notice.
- After uninstalling Lyra, the registry link entries may remain until a clean restart (**untested**).
