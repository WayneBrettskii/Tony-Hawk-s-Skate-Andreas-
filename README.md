# my-passthrough

Skate through GTA San Andreas with reTHAWed's real skating. reTHAWed runs hidden in the background and does the skating; GTA draws everything and keeps its story. Modelled on SkyCraft. How it works: `docs/DESIGN.md`. Change history: `MODLOG.md`.

**Status: phase 0 (very early): both games run our code and can exchange numbers. Nothing playable yet.**

## What works

(Only things tested on the real games are listed here.)

- **GTA plugin loads** (phase 0a, tested 2026-10-07): builds with Visual Studio 2026, Mod Loader loads it, it writes its log to `logs\gta.log` in the project folder.
- **plugin-sdk linked** (phase 0a-2b, tested 2026-10-07): built with `tools\build-plugin-sdk.ps1`; the plugin logs plugin-sdk's game version check: `GTA SA 1.0 US 'HoodLum'`.
- **Hooks into GTA's loop** (phase 0a-2c, tested 2026-10-07): counts drawn frames and world updates per second. While playing: 60 and 60. Main menu: 0 and 0. Pause menu: 0 drawn, 60 world updates.
- **Launcher starts reTHAWed** (phase 0b-1, tested 2026-10-07): `build\Release\my-passthrough-launcher.exe`; reTHAWed starts and exits normally, controller detected.
- **Our plugin runs inside reTHAWed** (phase 0b-2, tested 2026-10-07): the launcher loads `my-passthrough-thaw.dll` 0.3 s after start; it writes `logs\thaw.log` with a heartbeat. Tested on the menus only.

- **Link between the two games** (phase 0c-1, tested 2026-10-07): both sides see each other's once-per-second beat through shared memory.
- **Reading the skater in reTHAWed** (phase 1-1, tested 2026-10-07, read only): position and velocity, 10 times per second, confirmed by the distance-vs-speed check (ratio 1.00 to 1.02 skating straight). In THAW, Y is up; pushing speed ~500 units/s.
- **Menus vs level vs paused in reTHAWed** (phase 1-2, tested 2026-10-07, read only): "in menus" YES/no and the level ID change correctly; pause flag turns on when paused.

## What doesn't work / not tested

- **GTA in exclusive full screen freezes reTHAWed** when you switch back to reTHAWed (not caused by our code; happens without it). Run GTA in a window instead (see Requirements). With GTA in a window our full setup works (tested 2026-10-07, GTA on its main menu; not yet tested with GTA in a loaded game).
- GTA pauses itself while it is not the front window.
- No hooks inside reTHAWed yet.
- THAW's unit size is not confirmed (probably inches).
- reTHAWed's pause flag turning off when you unpause: not tested yet.
- Everything else in `docs/DESIGN.md`: not started.

## Requirements

- GTA San Andreas 1.0 US with ASI Loader and Mod Loader.
- GTA in a window or borderless window, **not exclusive full screen**: [Windowed Mode plugin](https://github.com/ThirteenAG/III.VC.SA.WindowedMode) by ThirteenAG (v2.1) in `modloader\WindowedMode\`; Alt+Enter in GTA cycles window styles.
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
