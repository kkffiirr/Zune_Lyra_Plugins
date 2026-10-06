# hebrew-font

Hebrew letters everywhere in the Zune HD UI.

Parts:
- `fontd.c` -> `fontd.exe`: a boot daemon (manifest `daemons`) that registers the eight Hebrew-extended fonts with `AddFontResourceW`
  as early as possible and stays resident.
- `manifest.json`: six `lyra.registry_write` actions that append `ZegoeHeb_R.ttf,Zegoe UH` to the font-link list of every Zegoe UI weight
  (the Zune already links them to Meiryo/Malgun for CJK), plus `lyra.load_module` for the checker below.
- `fontload.c` -> `fontload.dll`: loaded into the UI process; logs, per face, whether a Hebrew alef draws as a real glyph or the missing-glyph box
  (`\flash2\automation\fontload.log`) and adds the fonts itself if the daemon did not.

The eight `.ttf` files are **not in the repo** (Microsoft-derived). Generate them with `python tools/make-fonts.py <EXT.bin>`, build the
native parts with `./build.sh hebrew-font`, then deploy. Hebrew shows right-to-left-reversed (the UI has no bidi), see `hebrew-rtl`.

Full step-by-step account, reference hashes and the list of approaches that failed: [`docs/FONTS-HOWTO.md`](../../docs/FONTS-HOWTO.md).
