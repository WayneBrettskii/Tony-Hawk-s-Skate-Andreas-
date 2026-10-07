# MODLOG

One entry per change, newest at the top. Template: `MODLOG-template.md`.

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
