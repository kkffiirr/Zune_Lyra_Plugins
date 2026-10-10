# scummvm: ScummVM on the Zune HD

Runs [ScummVM](https://www.scummvm.org/) (the SCUMM engine, v0-v6) on a Zune HD (firmware 4.5) with Project Lyra.
Tested with **Day of the Tentacle** and **Sam & Max Hit the Road** (data you supply yourself). It shows up as a normal
**ScummVM tile in the Apps list** and runs only when you open it; nothing runs at boot.

**State: works with limitations. Read [Known issues](#known-issues) first.**

## How it works

```
Apps tile "ScummVM"   (app/, an XNA title: the same native-launcher trick as Lyra's own installer)
  -> \flash2\scummvm\scummvmapp.exe     (native/scummvmapp.cpp: supervisor)
       -> \flash2\scummvm\scummvmrun.exe (native/scummvmrun.cpp: hosts the game in its own process)
            -> \flash2\scummvm\scummvm-zune.dll  (ScummVM + the Zune backend, scummvm-zune-port.patch)
```

* **Screen:** the backend scales the game into the 480x272 landscape view, rotates it into the 272x480 portrait
  framebuffer and writes it with Lyra's kernel call (`lyra_kcall`, small chunks). The Zune-logo loader and (in normal
  mode) the shell are frozen by suspending their threads so they cannot repaint over the game.
* **Touch:** read from the input ring in `servicesd` (read-only, then the ring is drained by the backend). Tap = click,
  drag = move, long press = right click. Two on-screen buttons: top-left menu (ScummVM menu, Quit), top-right skip (Esc).
  Holding the top-left corner for 3 s force-quits.
* **Audio:** WaveOut, 22,050 Hz stereo, fed by a mixer thread. Timers run on their own thread.
* **Threads need a big stack:** Windows CE ignores the stack size of `CreateThread` (always 64 KB), so the engine and the
  helper threads run on a private `VirtualAlloc` stack through a small ARM trampoline.
* **Safety net:** if no touch arrives at all for 120 s after start, ScummVM quits by itself (so a dead touch path
  cannot trap you).
* **Where things live:** everything is in `\flash2\scummvm\` (games in `games\<id>\`, saves in `saves\`). Not in
  `\flash2\automation`, because Lyra wipes that folder when it reinstalls itself. The game list is built at start-up
  by scanning `games\<id>\` for known data files (`tentacle`, `samnmax`).

## Build

You need the same set-up as the other mods here (CeGCC, Lyra's SDK) plus ScummVM.

1. **CeGCC (GCC 9.3, `arm-mingw32ce`)** from [MaxKellermann/cegcc-build](https://github.com/MaxKellermann/cegcc-build).
   On a modern host: build binutils with `CC="gcc -std=gnu99"`, but build the mingw/w32api stages with `CC`/`CXX` unset
   (stale host objects otherwise), and link C++ with `-static-libgcc -static-libstdc++`.
2. **ScummVM:** check out commit `bcef809e61` (2019-03-31, the last tree where the old WinCE port compiled), then
   `git apply scummvm-zune-port.patch` (adds `backends/platform/zune/`, a `zune` backend in `configure`, small fixes).
   Configure with the cross compiler and only the SCUMM engine:
   `configure --host=wince --disable-all-engines --enable-engine=scumm --disable-zlib --disable-mad --disable-vorbis
   --disable-flac --disable-png --disable-jpeg --disable-theoradec --disable-ogg --disable-tremor --disable-faad
   --disable-a52 --disable-fluidsynth --disable-freetype2 --disable-sdlnet --disable-libcurl --disable-cloud
   --disable-eventrecorder --disable-readline --disable-nasm --disable-mt32emu --disable-translation
   --disable-highres --disable-scalers --disable-hq-scalers --disable-taskbar --disable-system-dialogs --disable-bink
   --disable-debug --enable-release --enable-optimizations`, then `make -j4`. The result (`scummvm.exe`) is a DLL:
   rename it `scummvm-zune.dll`. (The patch already contains Lyra's SDK client, `lyra.h` and `lyra_client.cpp`, which are
   MIT-licensed Project Lyra files, under `backends/platform/zune/`.)
3. **Native programs:** `arm-mingw32ce-g++ -O2 -include stdarg.h -I<lyra>/sdk/include -o scummvmapp.exe native/scummvmapp.cpp -x c <lyra>/sdk/src/lyra_client.c -static-libgcc -static-libstdc++`
   and `arm-mingw32ce-g++ -O2 -o scummvmrun.exe native/scummvmrun.cpp -static-libgcc -static-libstdc++`.
4. **The Apps tile (XNA):** needs XNA Game Studio 3.1 (Zune extensions) on Windows. Create a 64x64 `GameThumbnail.png`
   in `app/` (see `tools/make_icon.py`), then `MSBuild.exe app\exploiter.csproj /p:Configuration=Release /p:Platform=Zune`
   (the .NET 3.5 MSBuild). Put the resulting `exploiter.exe` in `deploykit/payload/` next to `deploykit/application.cfg`
   and the thumbnail.

## Install

**Scripted (Windows, Python 3, standard library only):** `tools/deploy_scummvm.py` does both parts below.

```
python tools/deploy_scummvm.py find                                   # the Zune's IP on your Wi-Fi
python tools/deploy_scummvm.py kit --exe exploiter.exe --thumb GameThumbnail.png --out deploykit
python tools/deploy_scummvm.py all --ip <zune-ip> --bin <folder with the 3 built files> --kit deploykit ^
       --game tentacle=<folder> --game samnmax=<folder>
```

`all` uploads over Wi-Fi (resumable), asks you to plug in USB, then installs the tile (`wifi` and `usb` run the two
halves separately). The USB half needs WSL with `usbipd-win` and zune-deploy's `zcli`. Tested: `find`, `kit` and `usb`
(the tile installs and `application.cfg` matches the one that worked by hand). The Wi-Fi half reuses the uploader below
and the Lyra "create folder" call, but was not re-run after being wrapped into this script.

By hand:

1. Wi-Fi (Lyra's daemon): create `\flash2\scummvm\`, `games\`, `saves\` and upload `scummvmapp.exe`, `scummvmrun.exe`,
   `scummvm-zune.dll` (`tools/upload_resume.py <zune-ip> <local> <remote>` is a resumable uploader; it reconnects
   when the Zune sleeps its Wi-Fi). Upload your game data to `games\tentacle\` (`TENTACLE.000/.001`, `MONSTER.SOU`) and/or
   `games\samnmax\` (`samnmax.000/.001`, `monster.sou`). `tools/extract_iso.py` pulls files out of a raw `MODE1/2352` CD image.
2. USB: with the Zune attached to WSL (`usbipd`), run Lyra's `zcli deploy deploykit` (zune-deploy). The Zune cannot be on
   Wi-Fi and USB at the same time.

## Controls

Touch only (see Known issues). Tap = click, drag, long press = right click. Top-left button = ScummVM menu (Save,
Load, Quit). Top-right button = skip cutscene (Esc).

## Known issues

* **Quitting restarts the Zune.** Every time. While an XNA app runs the system shuts the Zune shell (`gemstone`) down
  and keeps `xnalauncher.exe` alive; when ScummVM ends the shell does not come back by itself and the device restarts.
  Ending `xnalauncher` yourself makes it restart immediately, so there is no way back to the shell without the restart
  (tried; documented in `native/scummvmapp.cpp`).
* **Hardware buttons stop the touch.** In app mode the first press of Home, the side button or power makes touch
  input stop (the system treats the buttons as "leave this app" and tears the input down). So the buttons cannot be
  used as controls; use the on-screen buttons. **Do not press power while ScummVM runs**: it suspends the device and you
  have to force a restart (hold power).
* **Performance is unmeasured.** Frame rate is unknown; scaling and rotation are plain C. Audio is 22 kHz.
* **Needs Lyra.** The backend uses Lyra's kernel access for the screen and the input ring.
* A first, simpler launch method exists (start from the PC with `lyra-plugin-daemon.py spawn ... RunDaemon`, shell
  frozen, Home button works as skip and double-press as menu, no restart on quit). It is not packaged here.

## Curse of Monkey Island (SCUMM v8) build

`scummvm-zune-port-cmi.patch` is the same port plus the **v7/v8 engine** and a few CMI-specific changes. It is a full diff
against `bcef809e61` (apply it *instead of* `scummvm-zune-port.patch`, not on top). Configure exactly as above but with
`--enable-engine=scumm,scumm-7-8`. Output is a DLL like before (~5 MB); the tile's `scummvm-zune.dll` can be this one
(it still runs Day of the Tentacle and Sam & Max). Keep the old DLL as a rollback.

* **Game:** id `comi`, data in `\flash2\scummvm\games\comi\` (`COMI.LA0/1/2` and the `RESOURCE\` folder, ~1.2 GB, bring
  your own discs). Disc 1 of the usual 2-CD rip is MODE2/2336: `tools/extract_iso2336.py` extracts it, `7z` handles disc 2.
  `tools/upload_cmi.py` uploads everything over Wi-Fi in playable-first order and resumes after drops (the Zune takes one
  connection at a time and drops Wi-Fi when its screen sleeps).
* **Touch in CMI:** press and hold (~0.45 s) holds the **left** button until you lift your finger, which is how CMI's
  verb coin works (slide to a verb and let go). A right click (the CMI inventory) is the box icon in the **bottom-right**
  corner. Other games keep long press = right click.
* **Known issue:** the Zune crashes when exiting CMI (not yet diagnosed).
* Without the Apps tile you can still start it from the PC: `lyra-plugin-daemon.py spawn <ip> <dll> --entry RunDaemon
  --arg "comi|\flash2\scummvm\games\comi"`.

## OpenZDK (GPU) build: "ScummVM ZDK" tile

`scummvm-zune-port-zdk.patch` (a full diff against `bcef809e61`, apply it instead of the other patches) replaces the Lyra
kernel framebuffer write and the servicesd touch reader with OpenZDK: the frame is uploaded as a texture and drawn by the
GPU through `ZDKGL_*`, touch comes from `ZDKInput_GetState`, and nothing freezes the Zune shell. Everything is loaded from
`zdksystem.dll` at run time, so no OpenZDK import libraries are needed to build it with CeGCC.

* **Programs:** `native/scummvmzdk.cpp` is the host the tile starts (`\flash2\scummvm\scummvmzdk.exe`); it loads
  `\flash2\scummvm\scummvm-zdk.dll` (the patched build, renamed) and runs its `RunDaemon`. `tools/make_zdk_kit.py` turns the normal tile's
  launcher into the ZDK tile's (same-length rename of the target path inside `exploiter.exe`); `deploykit-zdk/application.cfg`
  gives it its own GUID so both tiles can be installed. `zdk.ini.example` (copy to `\flash2\scummvm\zdk.ini`) holds run-time switches.
* **Shaders:** `zdk-shaders/` (GPU shaders must be compiled ahead of time; see the README there).
* **Build/config:** as above but `--enable-engine=scumm,scumm-7-8`; the backend reads `\flash2\scummvm\zdk.ini` on every launch.

**What was learned (on firmware 4.5):**

* `ZDKDisplay_*` (the 2D sprite API in the OpenZDK headers) is a dead end: `ZDKDisplay_Initialize` is really the media-notify
  init, the second frame hangs, and it cannot coexist with `ZDKGL_*`. The GL path (`ZDKGL_Initialize`, shaders from
  `glShaderBinary`, attach the shaders *before* loading the binaries) works.
* `ZDKInput` delivers touch only inside the XNA tile's process. Started from the PC through the Lyra daemon, no touch
  arrives anywhere after `ZDKGL_Initialize`, and the Zune shell's own touch stays dead until a restart.
* **Quitting the tile restarts the Zune**, and this cannot be fixed from inside the app. An empty program exits cleanly
  (the shell, `gemstone.exe`, is not running inside an XNA tile and can be restarted with `CreateProcess`), but as soon as
  the GPU session has been used the system restarts the device when the launcher exits, whatever the order of
  `ZDKGL_Cleanup`, the splash screen and restarting the shell. `scummvmzdk.cpp` has test modes (`\flash2\scummvm\zdk-test.txt`: 1 exit at
  once, 2 GL only, 3 restore the shell only, 4 input only, 5 audio only, 6/7 GL with other shutdown orders) used to find this.
* For a clean exit use the PC-launched, framebuffer build instead (`lyra-plugin-daemon.py spawn <ip> <dll> --entry RunDaemon
  --arg "comi|\flash2\scummvm\games\comi"`, stop with the daemon's `stop`).

## Loading games onto the Zune (easy way)

Double-click **`Load games to the Zune.bat`** (or drag a game onto it). It needs only Python 3 and the Zune awake on the same
Wi-Fi as the PC (it uses Project Lyra's daemon, so Lyra must be installed on the Zune). It accepts a **game folder, a disc
image (`.iso`/`.bin`/`.cue`) or a `.zip`**, including multi-disc games (give every disc in the same run), works out which
game it is, shows what it found and what is missing, finds the Zune by itself, and uploads into `\flash2\scummvm\games\<id>`.
Uploads resume if the Zune drops off Wi-Fi (keep its screen on); finished files are skipped on a re-run.

    python tools/load_games.py "D:\Games\Curse of Monkey Island.zip"      # or several paths, --list to only look, --ip to skip the search

Supported ids: `tentacle` (Day of the Tentacle: `TENTACLE.000/.001` + `MONSTER.SOU` for speech), `samnmax` (Sam & Max Hit the
Road: `SAMNMAX.000/.001` + `MONSTER.SOU`), `comi` (The Curse of Monkey Island: `COMI.LA0/1/2` + the `RESOURCE` folder from both
discs, about 1 GB). The ScummVM tile's game list is built from these three folders at start-up. Games are your own copies; none
are included.

## Licence

ScummVM is GPL-2.0-or-later; `scummvm-zune-port.patch` is a derivative of ScummVM and is under the same licence.
The XNA launcher in `app/` is the OpenZDK native-app launcher by itsnotabigtruck (BSD 3-clause, see the file headers).
The rest follows the repository's MIT licence. No game data, binaries or Microsoft assets are included.
