# my-passthrough: Design

> Skate through GTA San Andreas with reTHAWed's real skating: its physics, tricks, grinds and manuals, while GTA keeps its world, story, missions and saves.

Status: draft v0.1, 2026-10-07. Nothing here is built or tested yet.

Modelled on SkyCraft (github.com/chasmlol/SkyCraft), which does the same thing for Skyrim and Minecraft.

---

## 1. Core idea

**Neither game is rewritten.** Both games run at the same time:

- **reTHAWed** (Tony Hawk's American Wasteland, modded) runs **hidden in the background**. It does the skating: physics, tricks, grinds, bails, scoring.
- **GTA San Andreas** runs **on screen**. It draws everything and keeps running its world: streets, traffic, pedestrians, missions, cutscenes, saves.

Two small add-ons, one inside each game, only **translate** between them:

- GTA tells reTHAWed **what the ground and obstacles near CJ look like**, and passes on your **keyboard/controller input**.
- reTHAWed tells GTA **where the skater is, which way they face, and what they're doing** (grinding, in the air, bailing).

If we ever find ourselves rewriting reTHAWed's skating in our own code, the design has gone wrong.

## 2. Who controls the player

GTA is in charge by default. reTHAWed only takes over while **skate mode** is on.

| Skate mode | Who moves CJ | What reTHAWed does |
|---|---|---|
| **Off** (normal GTA) | GTA, exactly as normal | Waits in the background |
| **On** | reTHAWed's skater physics; GTA moves CJ to match | Skates, using GTA's surroundings |

- A key turns skate mode on and off.
- Skate mode **switches off automatically** whenever GTA takes control itself: cutscenes, scripted mission moments, entering a car, dying, being arrested, loading a save.
- This keeps the story playable: anything scripted happens with GTA in full control.

**Agreed with the user (2026-10-07):** GTA is authoritative for the player, except reTHAWed is authoritative for movement while skate mode is on. `AGENTS.md` says so.

## 3. Components

```
┌──────────── gta_sa.exe (on screen) ────────────┐        ┌──────── THAW.exe + reTHAWed (hidden) ────────┐
│  our GTA plugin (.asi, C++)                     │        │  our THAW plugin (.dll, C++)                 │
│                                                 │        │                                              │
│  SurroundingsExporter ─ ground/walls near CJ ───┼──────▶ │  injected into THAW's collision checks       │
│  InputBridge          ─ keys / controller ──────┼──────▶ │  fed to THAW as if its window had focus      │
│  SkateModeSwitch      ─ on/off, cutscene off ───┼──────▶ │  start / stop skating                        │
│                                                 │        │                                              │
│  CJPuppet  ◀─ skater position, facing, state ───┼─────── │  read from THAW's real skater                │
│  Overlay   ◀─ trick names, score, balance (later)┼─────── │  read from THAW                              │
└─────────────────────────────────────────────────┘        └──────────────────────────────────────────────┘
                    shared memory (a block of memory both programs read and write)
```

Plus a small **launcher** (our own .exe) that starts the hidden reTHAWed and loads our THAW plugin into it (see §6).

## 4. Units and directions

- GTA San Andreas: 1 unit = 1 metre, Z points up.
- THAW: **not confirmed.** Neversoft's engine is believed to use 1 unit = 1 inch with Y pointing up. **To be measured in Phase 1** (move the skater a known distance, compare the logged numbers).
- The conversion goes in one small function with a test, so it is only ever written once.

## 5. The hidden reTHAWed world

Like SkyCraft's empty Minecraft world, the hidden THAW needs a nearly empty level for the skater to stand in:

- Early phases: a **flat custom park** (THAW has a park editor; `data\customparks` exists). The skater skates on THAW's own flat floor, standing in for GTA's ground.
- Later phases: GTA's real surroundings are fed into THAW's collision checks, so THAW's physics "sees" GTA's streets, kerbs, ramps and rails.

### 5.1 Feeding GTA's world to THAW (the hardest part)

- **Stage A:** our GTA plugin asks GTA's own collision system "what's below / in front of this point?" (line tests) on a grid around CJ, and sends the answers to THAW as simple boxes and slopes.
- **Stage B:** read GTA's collision models directly for exact shapes.
- **Skate surfaces:** THAW decides what's a rail, a ramp or a wall-ride surface from tags in its levels. GTA has no such tags, so we must **guess from shape** (for example: a long thin edge at waist height = grindable rail; a steep curved surface = ramp). This is new work SkyCraft didn't need.

## 6. Getting our code into each game

Rule 4 in `AGENTS.md`: we never write to the game folders unless the user names the path.

- **GTA:** our plugin is an `.asi` file. The ASI Loader 1.3 and Mod Loader are already installed. The plugin lives in **`D:\Games\GTA San Andreas\modloader\my-passthrough\`** (approved by the user 2026-10-07; recorded in `AGENTS.md`). Only our files go there; deleting that folder removes the mod.
- **reTHAWed:** our **launcher** starts `THAW.exe` and loads our plugin into it as it starts (known as "DLL injection"). Nothing is written to the reTHAWed folder. This is not bypassing any protection; reTHAWed has no anti-cheat that we have found.
- **Unknown:** how reTHAWed itself starts (`reTHAWedLaunch.exe` vs `THAW.exe` with `rthw.dll`). Must be worked out before Phase 0b, by reading, not changing, its files.

## 7. Offline only

reTHAWed contains online code (it starts network drivers on every launch, and has "Autoserver" settings and Discord files). We use only single-player gameplay. Our code **never hooks, starts, or changes** anything network-related, and never hosts or joins. If a mechanic we need turns out to depend on online code, we stop and ask (AGENTS.md rule 5). We don't run reTHAWed's `Updater.exe` during development, so its version stays fixed at 5.0.0.1.

## 8. Phased plan

Each phase ends with something checkable **from log files**.

| # | Phase | Done when |
|---|---|---|
| 0a | **GTA plugin loads** | GTA writes our log file with a "loaded" line and a frame counter that keeps rising |
| 0b | **THAW plugin loads** | Our launcher starts reTHAWed with our plugin; its log shows "loaded" and a rising counter |
| 0c | **Link** | Each side's log shows the *other* side's counter rising (proves shared memory works both ways) |
| 1 | **Find the skater** | THAW plugin logs the skater's position each frame; user skates in a straight line, numbers change as expected. Units/directions measured (§4) |
| 2 | **Puppet** | Skate mode on: CJ follows the hidden skater on a flat GTA area (e.g. the airport). Logs show the two positions match |
| 3 | **Input** | Your keys/controller in GTA drive the hidden skater; reTHAWed's window stays hidden and keeps running |
| 4 | **Ground** | Stage A: THAW's skater follows GTA's real ground height, slopes and walls |
| 5 | **Skate surfaces** | Rails and ramps guessed from GTA's shapes; grinds and airs work in GTA streets |
| 6 | **Look and story safety** | Trick names / score / balance shown in GTA; skate mode auto-off in cutscenes and missions |

Showing the real THAW skater model and animations inside GTA (instead of CJ) is a stretch goal, not in this plan.

### 8.1 Phase 0a-2 plan: per-frame counter (written 2026-10-07)

**Goal:** our GTA plugin runs a little code on every frame GTA draws and every time GTA updates the game world, and logs how often. This is the first time we hook into GTA's own loop. A *hook* means: we redirect one of GTA's internal function calls through our code, then let the original call carry on as normal.

**Tool: plugin-sdk** (github.com/DK22Pac/plugin-sdk). It is the standard C++ library for GTA SA plugins: it knows the 1.0 US memory addresses and has ready-made "events" we can attach to. We will need it later anyway (CJ's position, collision tests), so we adopt it now rather than writing a throwaway hook.

What was checked in plugin-sdk's source (cloned to Claude's scratch area, commit `15f15b60`, 8 Sep 2026):
- `Events::drawingEvent`: hooks a call at address `0x53E293`, after GTA draws the scene. Runs once per drawn frame.
- `Events::gameProcessEvent`: hooks a call at `0x53E981`, after `CGame::Process`, GTA's world update.
- It is normally built with a tool called premake into a Visual Studio solution. Its SA settings: defines `GTASA`, `PLUGIN_SGV_10US`, `RW`; newest C++ standard; static C++ runtime; 32-bit.

**Steps** (each one built and tested before the next):

| Step | What | Done when |
|---|---|---|
| 0a-2a | User downloads plugin-sdk into `D:\Mods\my-passthrough\extern\plugin-sdk\`, pinned to commit `15f15b60`. `.gitignore` gets `/extern/` added to its block list, so none of it can be committed. | `git status` does not show `extern\` |
| 0a-2b | plugin-sdk's SA library is built with its own tools (`tools\build-plugin-sdk.ps1`: premake `vs2026` + msbuild, as plugin-sdk's own automated build does), and `CMakeLists.txt` links it into our plugin. *Changed from the original idea of compiling it inside our CMake: plugin-sdk generates a shared header all its files rely on, so its own build route is safer.* Only new behaviour: one log line with plugin-sdk's game version check. | **Done 2026-10-07:** log shows `GTA SA 1.0 US 'HoodLum'` |
| 0a-2c | Replace the heartbeat with event counters. Once per second the log says how many frames were drawn and how many world updates happened in that second, plus running totals. The first time each event fires, it logs the time. | See test below |

**Test for 0a-2c:** user starts GTA, stays on the main menu ~20 s, starts or loads a game, plays ~30 s, opens the pause menu ~10 s, quits.

Expected (to be confirmed, not known yet):
- drawn frames per second roughly equals the game's frame rate (about 30 or 60 with Framerate Vigilante) in all three places;
- world updates are about the same as drawn frames while playing, and **may be zero** on the main menu and pause menu (GTA may skip world updates there; the log will tell us).

**Risks:**
- plugin-sdk may not compile cleanly through our CMake (it's normally built with premake). Fallback after two real attempts: use premake as intended, or write a single small hook of our own at `0x53E293` that logs what instruction it finds there before changing anything.
- Other installed mods (SilentPatch, Framerate Vigilante, SkyGFX, Widescreen Fix) may hook the same spots. If the frame rate or those mods' effects change, that's the cause. The log's frame rate numbers will show it.

**License:** plugin-sdk uses the zlib license (checked in its `LICENSE` file): free to use, acknowledgment appreciated; we will credit it in `README.md`. It stays in the gitignored `extern\` folder and is not copied into our repo.

## 9. Biggest risks

| Risk | Why | Fallback |
|---|---|---|
| Finding THAW's internals | No modding framework like Minecraft's Fabric. We must reverse-engineer `THAW.exe` (study the compiled program). | reTHAWed's own changes and log may point the way. If stuck after two real attempts: write `STATUS.md` and stop |
| Clashing with reTHAWed's own patches | reTHAWed already rewrites parts of THAW in memory | Test with reTHAWed active from day one |
| THAW pauses when hidden | Many old games stop when their window loses focus | Tell it it still has focus (SkyCraft does this for Minecraft) |
| Rails and ramps in GTA | GTA's world has no skate tags | Shape-based guessing; tune by logs |
| Two games at once | RAM and CPU | Both are old games; measure early |

## 10. Prerequisites (user's PC)

- Visual Studio with "Desktop development with C++" (for 32-bit builds; both games are 32-bit).
- Git for Windows (installed).
- plugin-sdk (C++ library for GTA SA plugins); downloaded into a gitignored folder.
