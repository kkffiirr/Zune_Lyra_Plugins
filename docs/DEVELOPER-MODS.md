# The Project Lyra developer's own mods: what can be installed without Microsoft's toolchain

Lyra's repository ships seven feature mods under `mods/`. Two are declarative (a `manifest.json` only); five contain native code that the
developer builds with **Visual Studio 2008 (ARM, Windows CE) + OpenZDK**. This table records what we could do with a CeGCC (`arm-mingw32ce`) toolchain instead.
Status as of 2026-10-06, firmware 4.5, Lyra 1.4.0. "Deployed" = files copied to the Zune and verified byte-for-byte; mods always arrive **disabled** and
must be enabled in the Mods tab. Nothing here was enabled by us except `hebrew-font`.

| Mod | Needs | Result with CeGCC | On the Zune |
|---|---|---|---|
| **night-mode** | nothing (manifest + 2 PNGs) | n/a | deployed |
| **marketplace-redirect** | nothing (registry values: points the Marketplace endpoints at the community `zunes.me` servers) | n/a | deployed |
| **custom-background** | 1 C++ file + 2 UI scenes (`.xui` -> `.xur`) | scenes compile (see below); the DLL does **not**: it uses MSVC `__try/__except` in 10 places | not deployed |
| **screencast** | `ce_image` (Windows CE *Imaging* COM: `imaging.h`, `imgguids.h`) and low-level kernel/physical-memory code | blocked: CeGCC has no `imaging.h` | not deployed |
| **zune-cast** | wolfSSL, `ce_image`, a private `zdk.h` | blocked (imaging + wolfSSL submodule) | not deployed |
| **youtube** | 107 lines of `__try/__except`, DirectShow filter DLLs, wolfSSL HTTPS, 9 UI scenes | not realistic | not deployed |

## Building the UI scenes (`.xui` -> `.xur`) on Linux/WSL
`modkit/mod-apply.py build <mod>` calls the C# tool XUIHelper (Zune HD fork). Its submodule path is empty in a shallow clone:

```bash
git clone --depth 1 https://github.com/magicisinthehole/xuihelper-zune.git lyra-src/modkit/xuihelper-zune
cd lyra-src/modkit/xuihelper-zune && dotnet build XUIHelper.CLI/XUIHelper.CLI.csproj -c Release     # needs a .NET SDK
cd .. && DOTNET_ROLL_FORWARD=Major python mod-apply.py build ../mods/custom-background
#   built scenes\Pivot.xui -> Pivot.xur     (2431 bytes)
#   built scenes\Start.xui -> Start.xur     (5244 bytes)
```
(The tool targets .NET 8; on a machine with only a newer runtime set `DOTNET_ROLL_FORWARD=Major`.)

## Why the native ones are blocked
- **`__try/__except` (structured exception handling)** is a Microsoft compiler extension. GCC cannot compile it. The developer uses it to swallow access
  violations when calling into the UI with pointers that may be stale. Replacing it with plain pointer checks (`IsBadReadPtr`) protects *reads* but not
  *calls* into the UI, so a missed case turns a harmless error into a UI crash. We did not do that for `custom-background` or `youtube`.
- **`imaging.h` / `imgguids.h`** (Windows CE Imaging API) are not part of CeGCC's headers. `screencast` and `zune-cast` use them to encode JPEG frames.
  Hand-writing the COM interfaces from memory on code that reads display memory and injects kernel input events is not worth the risk.
- **wolfSSL** (TLS for Chromecast/YouTube) is a git submodule (`src/ce-common/deps/wolfssl`); it compiles with GCC in principle, but it only pays off once the
  imaging problem is solved.
- `screencast` is especially low-level: it copies the display's physical front buffer via a kernel `memcpy` at a fixed address and injects touch events with
  hand-assembled ARM code. It is the developer's design for firmware 4.5; treat it as high risk even when it builds.

## What would unblock the rest
The developer's own toolchain: **Visual Studio 2008 with the Windows CE ARM compiler** and the **OpenZDK** SDK
(`C:\Program Files (x86)\Windows CE Tools\wce600\OpenZDK`, per `src/ce-common/build/env_setup.bat`). OpenZDK is available from the Internet Archive item `open-zdk`.
With that in place, each mod builds with its own `mods/<name>/build/*.bat`, then deploy with `mod-apply.py apply`.

## Deploy and verify (what was done for the three that are on the Zune)
```bash
python lyra-src/modkit/mod-apply.py apply lyra-src/mods/night-mode lyra-src/mods/marketplace-redirect --ip <zune-ip>
python tools/verify-mod.py <zune-ip> lyra-src/mods/night-mode lyra-src/mods/marketplace-redirect     # every file must say MATCH
```
Enable one mod at a time, restart cleanly, and keep the recovery routes in `LESSONS.md` in mind (Play Next hooks UI menus and is untested on a real device).

## Notes on specific mods
- **marketplace-redirect** changes the Zune's Marketplace/sign-in/stats endpoints to third-party community servers. Only enable it if you want that.
- **night-mode** is a pure visual tint with a moon quick-toggle (bottom right of the playback HUD).
