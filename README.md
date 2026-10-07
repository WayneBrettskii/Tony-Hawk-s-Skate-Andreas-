# my-passthrough

Skate through GTA San Andreas with reTHAWed's real skating. reTHAWed runs hidden in the background and does the skating; GTA draws everything and keeps its story. Modelled on SkyCraft. How it works: `docs/DESIGN.md`. Change history: `MODLOG.md`.

**Status: phase 0a (very early). Nothing playable yet.**

## What works

(Only things tested on the real games are listed here.)

- **GTA plugin loads** (phase 0a, tested 2026-10-07): builds with Visual Studio 2026, Mod Loader loads it, it writes its log to `logs\gta.log` in the project folder.
- **plugin-sdk linked** (phase 0a-2b, tested 2026-10-07): built with `tools\build-plugin-sdk.ps1`; the plugin logs plugin-sdk's game version check: `GTA SA 1.0 US 'HoodLum'`.
- **Hooks into GTA's loop** (phase 0a-2c, tested 2026-10-07): counts drawn frames and world updates per second. While playing: 60 and 60. Main menu: 0 and 0. Pause menu: 0 drawn, 60 world updates.

## What doesn't work / not tested

- Everything else in `docs/DESIGN.md`: not started.

## Requirements

- GTA San Andreas 1.0 US with ASI Loader and Mod Loader.
- reTHAWed 5.0.0.1.
- To build: Visual Studio 2026 with "Desktop development with C++".

## Building

Uses [plugin-sdk](https://github.com/DK22Pac/plugin-sdk) (zlib license), downloaded to `extern\plugin-sdk` at commit `15f15b60` (not part of this repo).

In **Developer PowerShell for VS 2026**:

```
cd D:\Mods\my-passthrough
powershell -ExecutionPolicy Bypass -File tools\build-plugin-sdk.ps1   # once, builds plugin-sdk
cmake -B build -A Win32
cmake --build build --config Release
```

The build copies `my-passthrough.asi` to `D:\Games\GTA San Andreas\modloader\my-passthrough\`.
To remove the mod, delete that folder.
