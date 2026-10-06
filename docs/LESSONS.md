# What we learned (Zune HD fw 4.5, Lyra 1.4.0)

## Getting a mod onto the device
- **Install route that worked from Windows:** the Deploy Kit via `zune-deploy` (`zcli deploy --launch`) in **WSL2**, with the Zune passed
  through using `usbipd-win` (`usbipd bind --force --busid X`, then `usbipd attach --wsl`). `--force` is needed because of the UsbDk filter.
  Keep a WSL process alive (`wsl -d Ubuntu sleep 7200`) or the attach reports "no WSL 2 distribution".
  `lyra-hd.ccgame` is rejected by XnaPack 3.1 on Windows (error 2007, its metadata is .NET 4 style); the browser installer needs
  `install.zune.moe`, which was unreachable.
- **Deploying mods:** `python lyra-src/modkit/mod-apply.py apply mods/<id> --ip <zune-ip>`. It pushes over Wi-Fi to port 1337 and
  mirrors the mod to `\flash2\automation\mods\<id>\`. Mods arrive **disabled ("archived")**; enable them in the Mods tab, then restart.
- The Zune's **IP changes with the Wi-Fi network**: scan port 1337 (its MAC starts `00:25:ae`). Wi-Fi is off while the Zune is on USB.
  The file service stalls when the Zune sleeps; wake the screen or restart it. It does not auto-connect to a network.
- `tools/read-log.py`, `read-bootlog.py`, `read-fontlog.py`, `verify-fontmod.py` read files back from the device (`pull_file`).
- A mod's referenced files (`@/...`) ship automatically; a token may name a directory (shipped recursively).

## What Lyra can and cannot do for a mod
- Native DLLs/EXEs built with **CeGCC load fine** (`lyra.load_module` into `gemstone`, `daemons` in the manifest).
- `lyra.write_blob_bytes` only takes data Lyra builds itself, **not a file from the manifest**.
- `lyra.registry_write` runs in boot phase 1, after the graphics system started. Settings read once at graphics start (e.g. `FontPath`) are too late.
  Settings read lazily (font **links**) do work; the first boot after a change may look unchanged, the next clean boot shows it.
- `lyra.gem_replace_entry` rewrites the whole `.gem` into `\Windows` (a small RAM store, about 600 KB free). `skin_default.gem` (570 KB) does
  **not fit** (`flush: short write`), so skin/style entries cannot be replaced that way.
- IAT patching of `CreateFontIndirectW` found no slots in the UI process; the UI does not call it through its import table.
- Hook points in `gemstone.exe` (fw 4.5, static VA == live VA, from Lyra's own source): `0x38434` `SetLabelText(elem, text)` and
  `0x83914` `RawRowLabel(a, b, text)`. `lyra_hook_install(va, replacement, &next)` detours them.

## Fonts
- The ROM fonts (carved from the firmware image): Zegoe UI in six weights (about 500 glyphs, **no Hebrew**), Meiryo/Malgun for Zune (CJK),
  and subsets of Arial, Georgia, Times, Verdana, Trebuchet, Courier, Impact, Wingdings, Webdings.
- `AddFontResourceW` works at runtime and new fonts are listed first. A **same-name** Hebrew copy replaces the ROM font only for weights other than
  Regular/SemiLight (both weight 400). Those two keep using the ROM font.
- **What fixed everything:** the Zune already has font linking on (`HKLM\SYSTEM\GDI\FontLinkMethods = 1`) and every Zegoe weight has a
  `HKLM\SOFTWARE\Microsoft\FontLink\SystemLink\<face>` value `"path,face;path,face"` (Meiryo, Malgun). Appending
  `\flash2\automation\mods\hebrew-font\ZegoeHeb_R.ttf,Zegoe UH` gives every weight (and Arial, Verdana, ...) Hebrew glyphs.
- Verification trick: draw the glyph into a 64x64 bitmap with the face and compare against the missing-glyph box (`render_hash` in `fontload.c`);
  GDI width queries are useless here (they return the same width for every character).

## Crashes and recovery
- Lyra's boot ladder: `normal` -> `safe` (platform only) -> `bare`. NORMAL tolerates 2 boots that do not reach a stable shell; a boot counts as healthy
  after 8 UI ticks, so a crash a few seconds later (e.g. when scrolling) never demotes.
- Remote disable (Wi-Fi): `tools/disable-mod.py <ip> <mod_id>` rewrites `\flash2\automation\mods\enabled.json`; `tools/disable-loop.py` polls and
  catches the window after each boot.
- **Over USB** (no Wi-Fi): launching the Lyra installer app while Lyra is installed runs its **uninstall** (wipes `\flash2\automation`, keeps the installer);
  launch it again to reinstall. `tools/usb-uninstall-loop.ps1` retries `zcli deploy --launch` until the Zune stays connected.
- `hebrew-rtl` crashed the UI when scrolling lists. Probable cause: the hook passes a **stack buffer** to a function that keeps the pointer.
  Rewrite with permanent per-string copies, log-only first, and test alone.

## Tooling quirks
- Shell heredocs/`sed` halve doubled backslashes: write C paths with an editor tool, or use macros like `#define DIR L"\\flash2\\..."`.
- Old Microsoft installers: XNA Game Studio 3.1 needs C# 2008 Express; XNA setup fails on Windows 10/11 because of the Xbox Live component
  (install its MSIs by hand). None of this is needed for the WSL route.
