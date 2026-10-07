# MODLOG

One entry per change, newest at the top. Template: `MODLOG-template.md`.

## [2026-10-07] Phase 0b-2 tested: our plugin runs inside reTHAWed

**Changed:** no code. `README.md` and `docs/DESIGN.md` 8.2 updated.
**Why:** record the test of step 0b-2.
**Tested how:** user built (launcher 152,064 bytes, THAW plugin 117,248 bytes), switched the controller on, ran `build\Release\my-passthrough-launcher.exe`, used reTHAWed's menus with the controller, quit. Claude read `logs\launcher.log`, `logs\thaw.log`, and reTHAWed's `debug.txt` (compared with the normal launch from 00:30).
**Result:** works. No antivirus warning reported.
- Launcher: `rthw.dll found 0.125 s after start`; `inject: LoadLibraryW finished in 0.172 s, result 0x6A540000 (loaded)`; `confirmed: my-passthrough-thaw.dll is loaded in reTHAWed (57 program files now)`; game closed after 25.0 s, exit code 0.
- THAW plugin: `game exe: D:\Games\reTHAWed\reTHAWed.exe`, size `MATCH`; `module list: 57 program files; rthw.dll PRESENT; this plugin PRESENT`; `heartbeat 1` to `heartbeat 24`. New modules since the launcher's list: our plugin plus `CoreMessaging.dll`, `inputhost.dll`, `uxtheme.dll` (Windows window and input libraries, loaded as the game opened its window).
- reTHAWed's `debug.txt`: start-up identical to the normal launch, including `Detected controller "PS5 Controller"` and `Added player 1: PS5 Controller`. One difference: the normal launch also logged `Joystick added: Xbox 360 Controller`; this one didn't (probably no second/virtual controller this time; not checked).
**Still broken / not tested:** no hooks inside THAW yet; no link between the two games yet. Tested on reTHAWed's menus only, not while skating.
**Next:** user chooses: phase 0c (link: both sides see each other's counter via shared memory) or 0b-3 (find a per-frame spot inside THAW).

## [2026-10-07] Phase 0b-2: THAW plugin, loaded into reTHAWed by the launcher

**Changed:** new `src/thaw/main.cpp` (`my-passthrough-thaw.dll`: logs to `logs\thaw.log`: loaded, exe path/size check, module list with `rthw.dll` and itself marked PRESENT/MISSING, heartbeat once per second; no hooks). `src/launcher/main.cpp`: checks the plugin exists before starting the game; after `rthw.dll` appears, loads the plugin into reTHAWed (memory for the path, copy path, run `LoadLibraryW` on a thread in the game, wait up to 10 s, log result), then confirms the plugin is in the game's module list. `CMakeLists.txt`: `thaw_plugin` target; building the launcher builds the plugin. `README.md` updated.
**Why:** step 0b-2 of `docs/DESIGN.md` 8.2.
**Tested how:** not tested, not compiled.
**Result:** none yet.
**Still broken / not tested:** not compiled or run. Windows Defender may react to the launcher loading code into another program. Not yet tested with a controller on through our launcher.
**Next:** user builds, switches the controller on, runs the launcher, looks around reTHAWed ~30 s, quits. Claude reads `logs\launcher.log`, `logs\thaw.log` and reTHAWed's `debug.txt`.

## [2026-10-07] Phase 0b-1 tested: our launcher starts reTHAWed normally

**Changed:** no code. `README.md` updated.
**Why:** record the test of step 0b-1.
**Tested how:** user built and ran `build\Release\my-passthrough-launcher.exe`, looked at reTHAWed's menus, quit. Claude read `logs\launcher.log`, and compared reTHAWed's `debug.txt` from this launch (15,642 lines) with the one from the user's normal launch at 00:30 (21,792 lines; kept as a baseline in Claude's scratch area).
**Result:** works.
- `reTHAWed.exe: 3612672 bytes: MATCH`; process started; `rthw.dll` found **0.109 s** after start (module list unavailable once at the very start, as expected); 53 program files loaded, including `rthw.dll`, `SDL2.dll`, `discord_game_sdk.dll`, `d3d9.dll`, `d3dx9_27.dll`, `binkw32.dll`. Game closed after 15.0 s with exit code 0 (normal exit).
- `debug.txt`: the first 900 lines (start-up) are identical to the normal launch apart from the date and **controllers**: the normal launch logged `Detected controller "PS5 Controller"`, `Joystick added: PS5 Controller` and `Joystick added: Xbox 360 Controller`; ours logged none. Same window mode, same `Patching resolution: 3840x2160`, same network driver start-up. Everything else that differs is later in the session (the normal launch went further into the game; different song).
**Still broken / not tested:** no controllers detected: **explained**, the user's controller was switched off during this test (answered 03:18). Not yet tested with a controller on through our launcher; to check during step 0b-2.
**Next:** step 0b-2 (THAW plugin + loading it into reTHAWed).

## [2026-10-07] Phase 0b-1: launcher that starts reTHAWed (no injection)

**Changed:** new `src/launcher/main.cpp` and a `launcher` target in `CMakeLists.txt` (builds `build\Release\my-passthrough-launcher.exe`; `RETHAWED_DIR` setting, default `D:/Games/reTHAWed`). `README.md` updated.
**Why:** step 0b-1 of `docs/DESIGN.md` 8.2: check reTHAWed runs normally when we start it, before loading anything into it.
**Tested how:** not tested, not compiled. Checked reTHAWed's own launcher (`reTHAWedLaunch.exe`) text: it passes no start-up options to `reTHAWed.exe`; it writes settings into `rethawed.ini`, which the game reads itself. So we start `reTHAWed.exe` directly from its folder with no options.
**Result:** none yet.
**Still broken / not tested:** not compiled or run. The launcher refuses to run if `reTHAWed.exe` isn't exactly 3,612,672 bytes. Compare our launch's `debug.txt` with the earlier normal launch (copy from 00:30 today, 21,792 lines).
**Next:** user builds and runs the launcher, plays reTHAWed's menus ~30 s, quits. Claude reads `logs\launcher.log` and reTHAWed's `debug.txt`.

## [2026-10-07] DRM decision recorded; phase 0b plan written

**Changed:** `AGENTS.md`: under rule 5, recorded the user's decision to proceed with both games' protection-removed executables as installed (our code never touches protection; never use `THAW.exe`). `docs/DESIGN.md`: section 6 now says how reTHAWed starts; new section 8.2, the phase 0b plan (launcher + THAW plugin that only logs). No code.
**Why:** user chose "carry on as planned" after the DRM finding; plan before code.
**Tested how:** nothing to run.
**Result:** plan with steps 0b-1 (launch only, no injection), 0b-2 (inject logging plugin), 0b-3 later (find a per-frame spot in THAW).
**Still broken / not tested:** all of phase 0b. Open risks: antivirus reaction, settings reTHAWed's launcher may pass, timing of loading our plugin.
**Next:** user approves the plan; then step 0b-1.

## [2026-10-07] Phase 0b research: STOPPED on a DRM question (AGENTS.md rule 5)

**Changed:** nothing. Research only; no code, no plan written.
**Why:** before planning phase 0b, find out how reTHAWed starts and how its code gets into the game.
**Tested how:** copied (read-only) `reTHAWed.exe`, `THAW.exe`, `rthw.dll`, `_d3d9.dll`, `binkw32.dll`, `reTHAWedLaunch.exe` into Claude's cloud workspace and read their structure and text strings with `objdump` and `strings`. Nothing was run, nothing in the game folder was changed.
**Result:**
- `THAW.exe` (21,442,560 bytes, Feb 2006) is the retail game and contains **SecuROM 7** copy protection (strings: `SecuROM User Access Service (V7)`, `Please insert the original "%PROJECT%" Disc`, extra protection sections).
- `reTHAWed.exe` (3,612,672 bytes) is the same game code (Aspyr build paths, game script names) **without** the SecuROM strings, with `rthw.dll` added so it loads reTHAWed's code at start. reTHAWed's launcher (`reTHAWedLaunch.exe`, .NET) starts `reTHAWed.exe`.
- `binkw32.dll` is the genuine Bink video library (not a loader). `rthw.dll` is reTHAWed's main code (uses SDL2, Discord, networking).
- Also realised: plugin-sdk reports our GTA exe as 1.0 US **"HoodLum"**, which is the name of the group that released a no-CD version of the 1.0 exe. So the GTA side most likely also runs on an exe with its disc check removed (produced by the downgrader from the user's Steam copy). Should have been flagged when that line first appeared.
**Still broken / not tested:** nothing built. Decision needed from the user before phase 0b.
**Next:** user decides how to proceed (see chat).

## [2026-10-07] Phase 0a-2c tested: frame and world-update counters work

**Changed:** no code. `README.md` and `docs/DESIGN.md` 8.1 updated with results.
**Why:** record the first test of our hooks into GTA's loop.
**Tested how:** user built, started GTA: main menu, then started/loaded a game and played, then the pause menu, then quit (about 49 s in total). Claude read `logs\gta.log`.
**Result:** works.
- Hook bytes: draw hook at `0x53E293` was `call 0x0060F8C0` before, `call 0x69F93E90` after (our plugin). Process hook at `0x53E981` was `call 0x0053BEE0` before, `call 0x69F93EA0` after. Both "before" targets are inside `gta_sa.exe`'s own code, so most likely no other mod had hooked these spots. At 5 s: unchanged, so nothing re-hooked them after us.
- Seconds 1 to 18 (startup and main menu): drawn **0**, world updates **0**. Neither hook runs on the main menu.
- From 18.5 s (game started): world updates start at once (35, then 60 per second). Drawn frames start about 2 s later (1, 0, then 51), probably the loading screen.
- While playing (about seconds 22 to 40): **60 drawn and 60 world updates per second**, a few dips to 55 to 58. 60 is the frame cap (Framerate Vigilante).
- From second 41 (pause menu): drawn **0**, but world updates **still 60 per second**. GTA keeps calling its world update while paused, but our draw hook does not run.
- Last second (49): 16 world updates, then the game closed.
**Still broken / not tested:** not known whether the world actually moves during the paused "world updates" (GTA probably runs them with time stopped). This matters later: skate mode must check GTA's pause state, not just count updates. User has not yet said whether the game felt normal (frame rate, other mods' effects).
**Next:** phase 0a is complete. Plan phase 0b (our launcher starts reTHAWed with our THAW plugin).

## [2026-10-07] Phase 0a-2c: count GTA's drawn frames and world updates

**Changed:** `src/gta/main.cpp`: on load, adds two plugin-sdk hooks that only count: `drawingEvent` (call at `0x53E293`, after the scene is drawn) and `gameProcessEvent` (call at `0x53E981`, after `CGame::Process`). Logs the 5 bytes at both addresses before and right after hooking, and again at 5 s. Replaced the heartbeat with one line per second: drawn frames and world updates in that second, plus totals; logs when each first happens. `README.md` updated.
**Why:** step 0a-2c of `docs/DESIGN.md` 8.1: first hook into GTA's own loop.
**Tested how:** not tested, not compiled. Checked in plugin-sdk's source: its hooks keep whatever call was already at the address and call it (so they chain with other mods), and `AddressList` hooks install for both 1.0 US versions (1001 Compact, 1002 HoodLum).
**Result:** none yet.
**Still broken / not tested:** not compiled or run. Unknown whether world updates happen on the menus. Unknown whether other installed mods (SilentPatch, Framerate Vigilante, SkyGFX, Widescreen Fix) use the same two addresses; the before/after/5 s byte lines will show it.
**Next:** user builds and runs the menu / play / pause-menu test; Claude reads `logs\gta.log`.

## [2026-10-07] Phase 0a-2b tested: plugin-sdk builds and links

**Changed:** no code. `README.md` "What works" updated; `docs/DESIGN.md` 8.1 step 0a-2b now describes the build route actually used.
**Why:** record the test result.
**Tested how:** user ran `tools\build-plugin-sdk.ps1`, then `cmake --build build --config Release`, started GTA, waited on the menu, quit. Claude checked `extern\plugin-sdk\output\lib`, the deployed plugin, and `logs\gta.log`.
**Result:** works. `Plugin.lib` built (258,062,356 bytes; large because it's built for link-time optimisation). Our plugin rebuilt and deployed (117,760 bytes, was 115,200). Log: `plugin-sdk 1005: game version 1002 = GTA SA 1.0 US 'HoodLum'`, the rest unchanged, `heartbeat 1` to `heartbeat 47`. "HoodLum" is one of the two 1.0 US executables plugin-sdk supports, both covered by `PLUGIN_SGV_10US`.
**Still broken / not tested:** no hooks yet; tested on the main menu only.
**Next:** step 0a-2c: replace the heartbeat with per-frame and per-world-update counters.

## [2026-10-07] Phase 0a-2b: build and link plugin-sdk (no behaviour change)

**Changed:** new `tools/build-plugin-sdk.ps1` (builds plugin-sdk's SA library with its own premake + msbuild, Release). `CMakeLists.txt`: links `extern/plugin-sdk/output/lib/Plugin.lib`, adds plugin-sdk's include folders and SA settings (`GTASA`, `PLUGIN_SGV_10US`, `RW`, `/std:c++latest`, `/sdl-`, MBCS); stops with a clear message if the library hasn't been built. `src/gta/main.cpp`: includes `plugin.h`; adds one log line with plugin-sdk's detected game version. `README.md`: build steps and plugin-sdk credit.
**Why:** step 0a-2b of `docs/DESIGN.md` 8.1. Changed approach from the plan: instead of compiling plugin-sdk inside our CMake, use plugin-sdk's own build route, because it generates a shared header that every file includes, and its own automated build (`.github/workflows/Build_Plugin_GTA_SA.yml`) already uses premake `vs2026` + msbuild on Visual Studio 2026.
**Tested how:** not tested. Checked on the user's PC: plugin-sdk at commit `15f15b60` (`.git\HEAD`), `Events.h` identical to the copy studied (apart from line endings). Read plugin-sdk's build script for its SA settings; checked it has no start-up code of its own (no `DllMain`, version message only shown if called).
**Result:** none yet.
**Still broken / not tested:** not compiled. Unknown: whether `Release` + `Mixed Platforms` builds cleanly (plugin-sdk's CI builds `zDebug`); whether our flags match the library well enough to link. The plugin-sdk build compiles ~750 files and may take several minutes.
**Next:** user runs the script and the build; Claude checks `logs\gta.log` for the new version line (expect "1.0 US"), the rest unchanged.

## [2026-10-07] Phase 0a-2a: block extern/ in .gitignore, download plugin-sdk

**Changed:** `.gitignore`: added `/extern/` to the block list (step 3). Still a whitelist. User downloads plugin-sdk into `extern\plugin-sdk`, pinned to commit `15f15b60bbf74c106e1b496ff92c98764abf4605`.
**Why:** step 0a-2a of `docs/DESIGN.md` 8.1. plugin-sdk must stay on the user's machine and never be committed.
**Tested how:** on a scratch git repo with the new `.gitignore`: fake files inside `extern/plugin-sdk/` (README.md, Events.h, x.cpp, CMakeLists.txt, which would be allowed elsewhere) and our real file names.
**Result:** all 4 `extern/` files ignored; `src/gta/main.cpp`, `CMakeLists.txt`, `README.md`, `docs/DESIGN.md`, `.gitignore` tracked. Not yet checked on the user's PC.
**Still broken / not tested:** download not yet confirmed on the user's PC.
**Next:** user runs the download commands and `git status`; then step 0a-2b (build plugin-sdk, no behaviour change).

## [2026-10-07] Plan for phase 0a-2 (per-frame counter)

**Changed:** `docs/DESIGN.md`: added section 8.1, the plan for phase 0a-2. No code.
**Why:** AGENTS.md: plan before code. 0a-2 is the first hook into GTA's own loop.
**Tested how:** nothing to run. Research: cloned plugin-sdk (commit `15f15b60`) into Claude's scratch area and read its event list and build settings. Found `drawingEvent` at `0x53E293` and `gameProcessEvent` at `0x53E981` for SA 1.0 US; SA build defines `GTASA`, `PLUGIN_SGV_10US`, `RW`; license is zlib.
**Result:** three-step plan (download plugin-sdk to gitignored `extern\`; build it with no behaviour change; then add the counters).
**Still broken / not tested:** whether plugin-sdk builds through our CMake (it's normally built with premake) is unknown. Whether world updates happen on the menus is unknown; the test will show it.
**Next:** user approves the plan; then step 0a-2a.

## [2026-10-07] Log move tested: works

**Changed:** no code. `README.md` updated.
**Why:** record the test of the log move.
**Tested how:** user rebuilt (`cmake --build build --config Release`), started GTA, sat on the menu, quit. Claude read `logs\gta.log` and `modloader\modloader.log`.
**Result:** works. `logs\gta.log` written, same lines as before, `game exe size ... MATCH`, `heartbeat 1` to `heartbeat 73` (~73 s). No `my-passthrough-log-failed.txt` created. Mod Loader scanned our folder **1 time** (was 14). Plugin is now 115,200 bytes.
**Still broken / not tested:** the `gta3.std.dmaudio` startup warning appears again in `modloader.log`, unchanged; still believed unrelated to us. Old `my-passthrough.log` still in the modloader folder (left for the user to delete). Tested on the main menu only.
**Next:** phase 0a-2: per-frame counter hooked into GTA's main loop.

## [2026-10-07] Moved the GTA plugin's log into the project folder

**Changed:** `src/gta/main.cpp`: log now goes to `D:\Mods\my-passthrough\logs\gta.log` (folder created if missing) instead of next to the plugin. If that fails, the plugin writes one file next to itself, `my-passthrough-log-failed.txt`, with the Windows error number, then stays silent. `CMakeLists.txt`: passes the logs folder path to the plugin (`MP_LOG_DIR`, default `<project>/logs`).
**Why:** Mod Loader watches its folders; our log changing every second made it rescan our folder 14 times in one session.
**Tested how:** not tested. Not compiled in the cloud (no Windows compiler there).
**Result:** none yet.
**Still broken / not tested:** not compiled, not run. The old `my-passthrough.log` in the modloader folder is left behind; the user can delete it.
**Next:** user rebuilds and runs GTA; Claude checks `logs\gta.log` exists and `modloader.log` shows only one or two scans of our folder.

## [2026-10-07] Phase 0a tested on the user's PC: plugin loads

**Changed:** no code. Updated `README.md` "What works" with this result.
**Why:** record the first real test of the GTA plugin.
**Tested how:** user built with `cmake -B build -A Win32` and `cmake --build build --config Release` in Developer PowerShell for VS 2026, started GTA, waited ~30 s on the menu, quit. Claude read `modloader\my-passthrough\my-passthrough.log` and `modloader\modloader.log`.
**Result:** works. Log shows `loaded (phase 0a)`, plugin path in the modloader folder, `game exe size: 14383616 bytes ... MATCH` (confirms 1.0 US by size), then `heartbeat 1` to `heartbeat 34`, about 1.0 s apart (drift ~0.1 s over 34 s). `modloader.log`: `Found file "my-passthrough.asi" with handler "gta3.std.asi"`. Built plugin is 114,688 bytes.
**Still broken / not tested:** (1) Mod Loader watches its folders for changes, and our log file changing every second made it rescan our folder 14 times in one session (`Scanning files at "modloader\\my-passthrough\"`). Harmless so far but wasteful; the log should move out of the modloader folder. (2) `modloader.log` also shows `Warning: Failed to startup plugin "gta3.std.dmaudio"`. That is Mod Loader's own audio plugin, loaded before ours; it is very likely not caused by our plugin, but not confirmed (no earlier log kept to compare). (3) Heartbeat is a timer, not a per-frame counter yet. (4) Did not test while actually playing (only the main menu).
**Next:** move the log into the project folder (`D:\Mods\my-passthrough\logs\`, already gitignored); then 0a-2, a per-frame counter hooked into GTA's main loop.

## [2026-10-07] Phase 0a: GTA plugin that only writes a log

**Changed:** added `src/gta/main.cpp` (the plugin), `CMakeLists.txt` (build setup, 32-bit, copies the plugin to `D:\Games\GTA San Andreas\modloader\my-passthrough\` after each build), `README.md` (what works / doesn't).
**Why:** Phase 0a in `docs/DESIGN.md`: prove our code runs inside GTA before doing anything else.
**Tested how:** not tested. Could not compile in the cloud: no Windows compiler available there (tried the Linux cross-compiler package and the zig toolchain; both unavailable). Confirmed from Mod Loader's own notes (`modloader\.data\plugins\gta3\std.asi.md`) that it loads `.asi` plugins from its folders.
**Result:** none yet.
**Still broken / not tested:** not compiled, not run. The heartbeat is once per second from our own thread, not per game frame (frame counter is the next step, 0a-2). The plugin does not touch the game in any way.
**Next:** user builds and runs GTA; Claude reads `my-passthrough.log` from the GTA modloader folder and checks the lines.

## [2026-10-07] Recorded user's two decisions

**Changed:** `AGENTS.md` (Environment section): player authority line now says GTA, except reTHAWed controls movement while skate mode is on; added the one approved write path in a game install. `docs/DESIGN.md` sections 2 and 6: replaced the two "needs OK" notes with the decisions.
**Why:** user answered yes to both open questions from the design plan.
**Tested how:** listed the project folder after copying to confirm the files arrived.
**Result:** files updated on the user's PC.
**Still broken / not tested:** no code yet; nothing to run.
**Next:** Phase 0a: GTA plugin that only writes "loaded" and a rising frame counter to a log. Needs Visual Studio with "Desktop development with C++".

## [2026-10-07] First design plan (SkyCraft approach, option A)

**Changed:** created `docs/DESIGN.md`. No code.
**Why:** user chose option A: reTHAWed runs hidden and does the skating physics, GTA draws and keeps its story, like SkyCraft. AGENTS.md requires a plan before code.
**Tested how:** nothing to run. Before writing it: read SkyCraft's README and DESIGN.md in full; read GTA's mod loader setup (ASI Loader 1.3, Mod Loader v0.3.10, exe downgraded to 1.0 US) and reTHAWed's `rethawed.ini` and `debug.txt`.
**Result:** plan with phases 0a to 6, each checkable from log files.
**Still broken / not tested:** everything in the plan is untested. Open questions: THAW's units (believed inches, Y-up, not confirmed); how reTHAWed starts itself; whether THAW keeps running when hidden. Two items need the user's OK: (1) AGENTS.md line "authoritative for the player: A" vs reTHAWed controlling movement while skating; (2) permission to write our plugin into `D:\Games\GTA San Andreas\modloader\my-passthrough\`.
**Next:** user reviews the plan and answers the two questions; then Phase 0a (GTA plugin that only writes a log).

## [2026-10-07] Added MODLOG and its template

**Changed:** created `MODLOG.md` (this file) and `MODLOG-template.md`.
**Why:** AGENTS.md requires a log entry after every change, using a template that did not exist yet.
**Tested how:** listed `D:\Mods\my-passthrough` after copying the files to confirm they arrived.
**Result:** see the file sizes in the folder listing; both files are present.
**Still broken / not tested:** not tested whether `git status` shows both files on the user's PC (they should, since `.md` files at the top level are whitelisted).
**Next:** run `git status`; then write the first plan in `docs/DESIGN.md`.

## [2026-10-07] Added whitelist .gitignore

**Changed:** created `.gitignore`.
**Why:** AGENTS.md rule 2: the repo must ignore everything except our own source and docs, so game files can never be committed by accident.
**Tested how:** on a cloud copy of the repo, created 15 fake files (source, docs, game files like `.exe`/`.dll`/`.ifp`, and files inside `extracted/`, `build/`, `logs/`) and asked git which ones it would ignore.
**Result:** 7 tracked as intended (AGENTS.md, .gitignore, README.md, src/core/skate.cpp, src/core/skate.h, docs/DESIGN.md, tools/dump.py); 8 ignored as intended, including a game animation file placed inside `src/` and a `.cpp` file inside `extracted/`.
**Still broken / not tested:** not tested on the user's PC. Any file type not on the whitelist (for example the mod's own `.ini` settings file, if we add one) will be ignored until we add it to the list.
**Next:** user runs `git status` and should see only `.gitignore` and `AGENTS.md` (plus the MODLOG files).

## [2026-10-07] Project set up with AGENTS.md

**Changed:** created the project folder `D:\Mods\my-passthrough`, added `AGENTS.md` (the user's rules for this project, saved word for word). User ran `git init`.
**Why:** starting point for the reTHAWed-to-GTA-San-Andreas mod.
**Tested how:** listed the folder after copying; user confirmed `git init` succeeded.
**Result:** `AGENTS.md` present, 3,282 bytes (same as the original). Git repo initialized.
**Still broken / not tested:** the "Loader" line in AGENTS.md is still a placeholder. `STATUS-handoff.md` (mentioned in AGENTS.md) does not exist yet.
**Next:** whitelist `.gitignore`.
