# lyra-plugins

Experimental mods ("plugins") for [Project Lyra](https://github.com/project-lyra-zune/project-lyra), the modding
platform for the **Zune HD (firmware 4.5)**. Tested on Lyra 1.4.0. Native code is built with a
[CeGCC](https://github.com/cegcc/cegcc) (`arm-mingw32ce`) cross compiler instead of Lyra's MSVC/OpenZDK toolchain.

| Mod | State | What it does |
|---|---|---|
| [`hebrew-font`](mods/hebrew-font) | **works** | Hebrew letters in the whole UI (track names, menus, Now Playing). A boot helper registers Hebrew-extended Zegoe UI fonts and the font-link lists get the Hebrew font appended. |
| [`hebrew-rtl`](mods/hebrew-rtl) | **partially working** | Visually reorders Hebrew text (the Zune draws everything left-to-right). Now Playing labels and artist names in rows are right-to-left; Albums/Artists lists and some other lists are still reversed. Staged with a self-disarming boot guard; see its README. |
| [`regprobe`](mods/regprobe), [`fontprobe`](mods/fontprobe) | diagnostics | Read-only logs of the font registry / font list. |
| `playnext` | builds | Lyra's own Play Next mod rebuilt with CeGCC (`./build.sh playnext`); untested on a device. |
| [`scummvm`](mods/scummvm) | **works, with known issues** | ScummVM (SCUMM v0-v6: Day of the Tentacle, Sam & Max) as a tile in the Apps list. Touch controls; quitting restarts the Zune and hardware buttons stop touch. See its README. |

The full, reproducible account of the font work (firmware extraction, font building, what failed and why, the font-link fix, with reference values to check against) is
[docs/FONTS-HOWTO.md](docs/FONTS-HOWTO.md). See also [docs/LESSONS.md](docs/LESSONS.md) for everything learned about the device and the platform, and
[docs/INSTALL.md](docs/INSTALL.md) for the full set-up and deploy recipe, and
[docs/OTHER-LANGUAGES.md](docs/OTHER-LANGUAGES.md) for how to reproduce this for Arabic, Cyrillic, Greek, Thai and others (and what is not feasible).

## Quick start

```bash
git clone https://github.com/project-lyra-zune/project-lyra lyra-src     # modkit, SDK, tools
CEGCC=/path/to/cegcc-prefix ./build.sh hebrew-font                       # in WSL/Linux: builds fontd.exe + fontload.dll
python tools/make-fonts.py path/to/EXT.bin [--scripts hebrew,arabic,...]  # Windows: generates the fonts (see below)
python lyra-src/modkit/mod-apply.py apply mods/hebrew-font --ip <zune-ip>
```

Then on the Zune: **Mods > Archived > Hebrew Font > enable**, and restart it **cleanly** (the font-link change needs one
normal shutdown/restart before it shows).

## The fonts are not in this repository

The Zune's UI font (Zegoe UI) has no Hebrew. The mod ships Zegoe UI with Hebrew glyphs copied from Windows' Segoe UI.
Both are Microsoft fonts, so **the generated `.ttf` files must not be committed or redistributed**. `tools/make-fonts.py`
builds them locally from your own Windows fonts and the Zune HD 4.5 firmware image (`EXT.bin` inside
`PavoBaseline.cab`, available from Microsoft's firmware package / the Internet Archive item
`updating-the-zune-firmware`).

## Safety

Mods run inside the Zune's UI process. A bad one can crash-loop the device. Keep `tools/disable-mod.py` and
`tools/disable-loop.py` (Wi-Fi) and the USB uninstall route in [docs/LESSONS.md](docs/LESSONS.md) in mind before
enabling anything new, and test one change per restart.

## Layout

```
mods/<id>/          manifest.json + C sources (binaries and fonts are generated, git-ignored; scummvm/ is an XNA app + patch instead)
build.sh            cross-compile a mod's native parts with CeGCC
tools/              deploy/verify/log helpers (Python), font builder, USB recovery loop
docs/               INSTALL.md, FONTS-HOWTO.md, LESSONS.md, OTHER-LANGUAGES.md
```

Licence: [MIT](LICENSE) for the code and notes in this repository (`tools/xuiz.py` credits Project Lyra, also MIT; see [THIRD-PARTY.md](THIRD-PARTY.md)).
The Microsoft fonts the Hebrew mod needs are **not** covered and not included; see "The fonts are not in this repository".
