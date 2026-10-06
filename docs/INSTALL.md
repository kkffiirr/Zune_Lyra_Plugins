# Set-up and deploy recipe

Windows 11 PC, WSL2 Ubuntu, a Zune HD on firmware 4.5.

## 1. Install Lyra on the Zune (once)
1. `winget install dorssel.usbipd-win`; in an admin shell: `usbipd bind --force --busid <id>` (find it with `usbipd list`).
2. In Ubuntu: `apt install dotnet-sdk-10.0 build-essential cmake libssl-dev git`; clone and `dotnet build`
   [gigalasr/zune-deploy](https://github.com/gigalasr/zune-deploy) (needs `--recurse-submodules`); copy `docs/.mtpz-data` to `~`.
3. Download `lyra-hd-deploykit.zip` from the Lyra release, unzip, then (Zune plugged in, Zune software closed):
   `usbipd attach --wsl --busid <id>` and `zcli deploy --launch lyra-hd-deploykit`. The Zune reboots with Lyra installed.
4. Put the Zune on your Wi-Fi (Settings > Wireless) and note its IP (scan port 1337).

## 2. Build a mod
```bash
git clone https://github.com/project-lyra-zune/project-lyra lyra-src
export CEGCC=/path/to/cegcc-prefix            # a CeGCC build providing arm-mingw32ce-gcc 9.x
./build.sh hebrew-font                         # -> mods/hebrew-font/fontload.dll, fontd.exe
python tools/make-fonts.py /path/to/EXT.bin    # -> the Hebrew fonts (Windows; needs fonttools)
python lyra-src/modkit/mod-apply.py validate mods/hebrew-font
```

## 3. Deploy and enable
```bash
python lyra-src/modkit/mod-apply.py apply mods/hebrew-font --ip <zune-ip>
python tools/verify-fontmod.py <zune-ip>       # every file must say MATCH
```
On the Zune: Mods > Archived > Hebrew Font > enable, restart cleanly. Then `python tools/read-fontlog.py <zune-ip>` should show
`HEBREW` for every Zegoe face.

## 4. If something goes wrong
See "Crashes and recovery" in [LESSONS.md](LESSONS.md).
