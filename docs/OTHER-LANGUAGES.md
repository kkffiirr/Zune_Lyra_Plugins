# Reproducing this for other languages (Arabic, Cyrillic, Greek, Thai, ...)

Showing a script on the Zune has **two separate problems**. Only the first one is solved (for Hebrew, on a real device).

1. **Glyphs**: the UI font has no letters for the script, so you get rectangles. Fix: add the glyphs and make the Zune fall back to them.
2. **Layout**: the Zune draws every string left to right, one character after another, with no shaping. Right-to-left scripts come out
   reversed, and scripts whose letters change shape or reorder come out wrong. Fix: change the text before it is drawn (the
   `hebrew-rtl` mod, partly working).

## Problem 1: glyphs (works for any script that has a source font)

The recipe that worked for Hebrew, in order:

1. **Find a source font that has the script.** Windows' Segoe UI covers Hebrew, Arabic, Cyrillic and Greek. For other scripts pick another
   font from `C:\Windows\Fonts` (Thai: Leelawadee UI or Tahoma; Vietnamese extras: Segoe UI). Check coverage:
   ```python
   from fontTools.ttLib import TTFont
   cm = TTFont(r"C:\Windows\Fonts\segoeui.ttf").getBestCmap()
   print(sum(1 for c in range(0x621, 0x64B) if c in cm), "Arabic letters present")
   ```
2. **Add the script to `tools/make-fonts.py`**: one line in `SCRIPTS = {name: ([(first, last), ...], sample_letters)}`. The ranges are
   the Unicode blocks you want copied. A different source font than Segoe UI needs a small change in `WEIGHTS` (the Windows file per weight).
3. **Build the fonts** from the Zune HD firmware image (`EXT.bin`) plus your Windows fonts:
   ```
   python tools/make-fonts.py EXT.bin --scripts hebrew,arabic,cyrillic,greek
   ```
   Hebrew alone adds 57 glyphs per font (~36 KB per file); the four scripts together add ~900 glyphs (~170 KB per file). Flash space is not a concern.
4. **Deploy the same `hebrew-font` mod** (`mod-apply.py apply mods/hebrew-font --ip ...`) and restart **cleanly**, twice if the first restart shows no change.
   The mod's manifest, daemon and font-link entries need no change: the link entry points at the same font file, which now holds more scripts.
5. **Verify on the device, not by eye.** `fontload.c` draws one letter per face into a bitmap and compares it with the missing-glyph box
   (`render_hash` / `has_alef`). It tests U+05D0 (alef). For another script change that code point in `FontLoadInstall`/`has_alef`
   (for example U+0627 for Arabic alef), rebuild with `./build.sh hebrew-font`, and read `tools/read-fontlog.py <ip>`.
   Every face must say `HEBREW` (the label is historical: it means "real glyph, not the box").

Cyrillic and Greek need nothing else. They may already work from the ROM fonts: test first with the log before adding anything.

## Problem 2: layout

| Script | Glyphs | Layout work | Verdict |
|---|---|---|---|
| **Hebrew** | done | right-to-left order only | letters work; order is fixed in Now Playing labels and some rows (`hebrew-rtl`), other lists still reversed |
| **Arabic, Persian, Urdu** | easy (Segoe UI; font generation tested, not tried on a device) | right-to-left **and shaping**: each letter has isolated/initial/medial/final forms plus ligatures such as lam-alef | feasible but real work: extend the hook to map letters to the Arabic Presentation Forms (U+FB50-FDFF, U+FE70-FEFF) using the Unicode joining rules, then reverse. Without shaping the text is readable but every letter is disconnected. |
| **Cyrillic, Greek, Latin extensions** (Russian, Ukrainian, Vietnamese, ...) | easy | none | works as soon as the glyphs are there |
| **Thai** | needs Leelawadee UI/Tahoma | combining vowels and tone marks must be positioned above/below letters | partially feasible: base letters yes, marks will sit wrongly |
| **Hindi (Devanagari), Bengali, Tamil, Telugu, Khmer, Myanmar, ...** | easy to add | conjuncts, vowel reordering, half-forms need an OpenType shaping engine | not realistic inside a text hook; skip |
| **Chinese (Han)** | the ROM already links Japanese (Meiryo) and Korean (Malgun) fonts, which cover many Han characters | none | try it first; a full Chinese font is 10-20 MB |
| **Emoji / colour fonts** | no | the UI draws monochrome glyphs only | no |

### Extending `hebrew-rtl` to Arabic (sketch)
`mods/hebrew-rtl/flip_core.h` is plain C with unit tests (`test_flip.c`):
- extend `IS_HEB(c)` (the "this string needs flipping" test) with the Arabic ranges;
- before reversing, run a **shaping pass**: for each Arabic letter choose the isolated/initial/medial/final presentation form from its neighbours
  (joining types from Unicode `ArabicShaping.txt`, about 100 lines of table), and combine lam + alef into the ligature code points;
- keep the existing rule that Latin/digit runs are put back in reading order;
- add test cases first (host-side unit test, no device needed), and follow the staged workflow in the mod's README (log-only probe first, boot guard) before testing on a Zune.

## Things to remember for every language
- **Licensing:** the generated fonts are derived from Microsoft fonts. They are for your own device; do not publish them.
- **Order of work:** glyphs first (visible, safe), layout hook last (can crash the UI). One change per restart.
- **After any manifest change the first restart may look unchanged.** Restart cleanly once more before concluding it failed.
- **Know the recovery routes** (`docs/LESSONS.md`) before enabling a mod that hooks the UI.
