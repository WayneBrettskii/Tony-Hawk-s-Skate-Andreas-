# Tony Hawk's SkateAndreas: Design

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
- THAW, **measured 2026-10-07 (phase 1-1):** **Y points up** (confirmed by ollies). Pushing speed about 500 units/s, ollie height about 65 to 90 units, gravity about 1,350 units/s². **Unit size not confirmed:** these fit 1 unit = 1 inch (12.7 m/s, ~1.9 m ollie), but THAW exaggerates speed and jumps, so this needs checking against a known object size. Which way +X and +Z point relative to GTA's axes is not decided yet.
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

- **GTA:** our plugin is an `.asi` file. The ASI Loader 1.3 and Mod Loader are already installed. The plugin lives in **`D:\Games\GTA San Andreas\modloader\SkateAndreas\`** (approved by the user 2026-10-07; recorded in `AGENTS.md`). Only our files go there; deleting that folder removes the mod.
- **reTHAWed:** our **launcher** starts `reTHAWed.exe` and loads our plugin into it (known as "DLL injection"). Nothing is written to the reTHAWed folder. reTHAWed has no anti-cheat that we have found.
- **How reTHAWed starts (found 2026-10-07 by reading its files):** its settings program `reTHAWedLaunch.exe` (.NET) starts `reTHAWed.exe`. That is the game with the retail SecuROM 7 protection already removed and `rthw.dll` (reTHAWed's code) added to the files it loads at start. The retail `THAW.exe` still has SecuROM; **we never use it.** See the decision under rule 5 in `AGENTS.md`.

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
| 0a-2c | Replace the heartbeat with event counters. Once per second the log says how many frames were drawn and how many world updates happened in that second, plus running totals. The first time each event fires, it logs the time. | **Done 2026-10-07.** Playing: 60 drawn / 60 updates per second. Main menu: 0 / 0. Pause menu: 0 drawn / 60 updates. No other mod had hooked either spot. |

**Learned for later phases:** GTA keeps calling its world update while the pause menu is open, and neither hook runs on the main menu. Skate mode must check GTA's real pause state rather than assume "world update = game running".

**Test for 0a-2c:** user starts GTA, stays on the main menu ~20 s, starts or loads a game, plays ~30 s, opens the pause menu ~10 s, quits.

Expected (to be confirmed, not known yet):
- drawn frames per second roughly equals the game's frame rate (about 30 or 60 with Framerate Vigilante) in all three places;
- world updates are about the same as drawn frames while playing, and **may be zero** on the main menu and pause menu (GTA may skip world updates there; the log will tell us).

**Risks:**
- plugin-sdk may not compile cleanly through our CMake (it's normally built with premake). Fallback after two real attempts: use premake as intended, or write a single small hook of our own at `0x53E293` that logs what instruction it finds there before changing anything.
- Other installed mods (SilentPatch, Framerate Vigilante, SkyGFX, Widescreen Fix) may hook the same spots. If the frame rate or those mods' effects change, that's the cause. The log's frame rate numbers will show it.

**License:** plugin-sdk uses the zlib license (checked in its `LICENSE` file): free to use, acknowledgment appreciated; we will credit it in `README.md`. It stays in the gitignored `extern\` folder and is not copied into our repo.

### 8.2 Phase 0b plan: our code running inside reTHAWed (written 2026-10-07)

**Goal:** a small program of ours (the *launcher*) starts reTHAWed, and a small plugin of ours runs inside it and writes a log, the same proof we got for GTA in phase 0a. No hooks, no changes to the game, normal visible window.

**Two new pieces** (both built by our CMake into the `build` folder; nothing goes into `D:\Games\reTHAWed`):

1. **`SkateAndreas-thaw.dll`, the THAW plugin.** Like the first GTA plugin: when loaded, it starts its own thread and writes `logs\thaw.log`:
   - "loaded", the game exe path and size (expect `reTHAWed.exe`, 3,612,672 bytes),
   - the list of program files loaded in the game at that moment (to confirm `rthw.dll` is there, i.e. reTHAWed is active),
   - a heartbeat once per second.
   No plugin-sdk (it's GTA-only). No hooks.
2. **`SkateAndreas-launcher.exe`, the launcher.**
   1. Checks `D:\Games\reTHAWed\reTHAWed.exe` exists and is the expected size; refuses `THAW.exe`.
   2. Starts `reTHAWed.exe` with `D:\Games\reTHAWed` as its working folder, the way reTHAWed's own launcher does (it does not run `Updater.exe`).
   3. Waits until `rthw.dll` shows up inside the game (checks every 100 ms, gives up after 30 s), so reTHAWed has started its own setup first.
   4. Loads our plugin into the game: it asks the game to run Windows' `LoadLibrary` on our DLL's path (standard "DLL injection"; it adds our code, it doesn't change the game's files or protection).
   5. Writes its own log, `logs\launcher.log`: each step, timings, and any Windows error numbers.

**Steps** (each built and tested before the next):

| Step | What | Done when |
|---|---|---|
| 0b-1 | Build both pieces. **Before** injecting anything: launcher only starts reTHAWed and waits for `rthw.dll`, then logs and stops. | reTHAWed runs normally from our launcher; `launcher.log` shows `rthw.dll` found and how long it took; reTHAWed's own `debug.txt` starts the same way as a normal launch |
| 0b-2 | Launcher also injects our plugin. | **Done 2026-10-07:** plugin loaded 0.3 s after start, `rthw.dll PRESENT; this plugin PRESENT`, 24 heartbeats, normal exit, controller detected (step 0b-1 also done: identical start-up to a normal launch) |
| 0b-3 (later, own plan) | Find a once-per-frame spot inside THAW to hook (no plugin-sdk here, so this needs reverse engineering). | Planned separately |

**Test procedure (both steps):** user runs `build\Release\SkateAndreas-launcher.exe` (from the project folder), plays reTHAWed's menus for ~30 s, quits normally. Claude reads `logs\launcher.log`, `logs\thaw.log` and reTHAWed's `debug.txt` (read-only).

**Risks:**
- **Antivirus:** starting a game and loading a DLL into it is also what some malware does, so Windows Defender may warn about or quarantine our launcher. If so we stop and look, rather than switching protection off.
- **reTHAWed's own launcher may pass settings** to `reTHAWed.exe` that we don't. Step 0b-1 checks this: `debug.txt` from our launch is compared with a normal launch.
- **Loading too early or too late:** waiting for `rthw.dll` should avoid racing reTHAWed's setup, but it's untested.
- **Online code** still starts on its own when reTHAWed starts (as in every normal launch). We don't touch it (rule 5).

### 8.3 Phase 0c plan: the link between the two games (written 2026-10-07)

**Goal:** the GTA plugin and the THAW plugin share a small block of memory, and each one's log shows the *other* game's heartbeat rising. This proves the two games can exchange data, which every later phase depends on (SkyCraft does the same with `Local\SkyCraft_v1`).

**How it works (plain language):** Windows lets two programs open the same named block of memory (a *shared memory* or *file mapping*; no actual file on disk). Whichever plugin starts first creates it; the second one opens the same block. Each side writes only its own part and reads the other's.

**The shared block, version 1** (one new header file both plugins include: `src/common/link.h`). Fixed-size numbers only, so both sides agree on the layout; a check in the code stops the build if the size ever changes by accident.

| Field | Written by | Meaning |
|---|---|---|
| `magic`, `version` | first side to start | "MPT1", 1: confirms both sides use the same layout |
| `gtaPid`, `thawPid` | each side | process id of each game (0 = not started yet) |
| `gtaBeat`, `thawBeat` | each side, once per second | a counter that rises while that side's plugin is running |
| `gtaDrawn` | GTA side, once per second | GTA's total drawn frames (from phase 0a-2c), to see whether GTA keeps drawing while the other window has focus |

- Name: `Local\SkateAndreas_v1` (`Local` = only programs in the user's own Windows session can see it; nothing on the network).
- Once per second each plugin: bumps its own beat, then logs the other side's process id and beat, and by how much it rose since last time. If the other side isn't there yet, it logs "other side: not connected" (once, until it changes).

**Steps:** a single step, 0c-1 (both plugins + the shared header together, since one side alone can't show anything).

**Test:** user starts GTA normally and waits on its menu or in-game; then runs the launcher to start reTHAWed; switches between the two windows a few times (~30 s total); quits reTHAWed, then GTA. Claude reads `gta.log` and `thaw.log`.

**Done when:** each log shows the other side's process id, and its beat rising about 1 per second while both run; after reTHAWed quits, `gta.log` shows THAW's beat stop rising.

**Also learned from this test (useful later):** whether GTA keeps drawing frames while it doesn't have focus (`gtaDrawn`). Phase 3 needs both games to keep running at once.

**Risks:**
- Two games running at once uses more memory and CPU; both are 2000s games, so expected to be fine, but untested.
- GTA may pause itself when it loses focus. That doesn't stop the link test (the beat comes from our own thread), but it will show up in `gtaDrawn`.
- Both sides could start at the same instant and both try to set up the block; the code uses an atomic "compare and set" on `magic` so only one does.

**Result (2026-10-07):** the link works both ways (each side saw the other's beat rise +1 per second, 133 times in a row). Two findings:
- **GTA in exclusive full screen freezes reTHAWed** when switching back to reTHAWed (happens without our code too). With GTA in a window (ThirteenAG's Windowed Mode plugin, `mode=1`) reTHAWed kept running. **So GTA must not run in exclusive full screen** (see section 10).
- GTA pauses itself completely (0 drawn, 0 world updates) while it isn't the front window. Fine for our design, since GTA is the game on screen.
- Confirmed with our launcher + plugin and GTA windowed: link works both ways, no freeze. **Phase 0 (0a, 0b, 0c) is done.** Not yet tested with GTA in a loaded game while windowed.

### 8.4 Phase 1 plan: find the skater inside reTHAWed (written 2026-10-07)

**Goal:** our THAW plugin reads the local skater's position and velocity and logs them about 10 times per second, so we can confirm from the numbers that they are right and measure THAW's units and directions (section 4).

**What research found (read-only analysis of a copy of `reTHAWed.exe`, nothing run):**
- `reTHAWed.exe` has the **same code layout as THAW 1.01**, the game PARTYMANX's open-source *partymod* targets (github.com/PARTYMANX/partymod-thaw, MIT license). 9 of 9 byte patterns partymod expects at fixed addresses match our file exactly. So partymod's addresses apply to us.
- THAW's script command table pairs names with code. `GetSkaterPosition` (code at `0x005A98D0`) takes the script's object (pointer at script `+0xC4`) and reads three floats at object **`+0x68`, `+0x6C`, `+0x70`**, which it returns as `x`, `y`, `z`.
- `SkaterSpeedGreaterThan` (code at `0x005A93F0`) gets the skater with: pointer at global **`0x0074FB44`** (most likely the skate manager), then calls **`0x004A0160`** on it (walks a skater list at manager `+0x204`; most likely "get local skater"). It then reads three floats at skater **`+0xC8`, `+0xCC`, `+0xD0`** and takes their length as the speed, so those are the **velocity**. The same global + call pair appears 66 times in the game.
- Also noted for phase 3: partymod uses `0x0072E850` as THAW's "window is focused" flag.

**Not yet confirmed:** that the object from `0x004A0160` is the same kind of object `GetSkaterPosition` reads (i.e. that `+0x68` is its position too). The test below checks this from the numbers.

**Step 1-1 (one step):** in the THAW plugin's own thread, every 100 ms:
1. Safety check once at start: the code bytes at the addresses above must be exactly what we found (version guard). If not, log it and do nothing.
2. Read the manager pointer at `0x0074FB44`; if non-zero, call `0x004A0160` to get the local skater. All reads wrapped in Windows' exception protection, so a bad read is logged instead of crashing the game.
3. Log: whether a skater exists, position (`+0x68/6C/70`), velocity (`+0xC8/CC/D0`), speed (length of velocity).
4. Once per second, the self-check: distance the position moved in that second vs the average speed over that second. If they agree, `+0x68` is the position and `+0xC8` the velocity.

This only **reads** game memory. It writes nothing into the game and hooks nothing. Calling `0x004A0160` from our thread reads the skater list while the game may be changing it; the exception protection covers that, and phase 1 only logs.

**Test:** GTA not needed. User starts reTHAWed with our launcher, loads into a level, then: stands still ~5 s; skates in a straight line ~5 s; stops; does 3 ollies (jumps) in place; quits. Claude reads `thaw.log`.

**Done when:**
- in menus: "no skater"; in a level: a skater is found;
- standing still: position constant, speed about 0;
- skating straight: distance moved per second ≈ average speed (within ~10%);
- ollies: one coordinate goes up then back down; that is THAW's "up" axis. Typical ollie height and skating speed give a first estimate of the units (inches would mean an ollie of roughly 20 to 50 units and skating speed of a few hundred units per second; to be confirmed, not assumed).

**If it doesn't work:** two real attempts (e.g. if the self-check fails, try the other candidate offsets from the disassembly), then stop and write `STATUS.md`.

**Result (2026-10-07): works on the first attempt.** All code checks matched, no read failures. Self-check ratio **1.00 and 1.02** while skating straight, so `+0x68` = position and `+0xC8` = velocity of the local skater are confirmed. Y is up. **Correction to the plan:** reTHAWed has a skater in its **menus** too (found 2.3 s after start), so "skater exists" does not mean "in a level"; the skater teleports between menu spots and the level start point. Telling menu from level is still to do.

### 8.5 Phase 1-2 plan: tell reTHAWed's menus from a level (written 2026-10-07)

**Why:** reTHAWed has a skater in its menus too (phase 1-1), so "skater exists" doesn't mean "in a level". Phase 2 must not move CJ to menu positions.

**What research found** (read-only analysis of `reTHAWed.exe`):
- Script command `InFrontend` (code `0x0059A660`): reads a global at **`0x008A3B5C`** (game-flow object, most likely), takes the object at `+0x20`, and calls **`0x00594310`** on it, which returns true/false. That function does a table lookup (`0x004D0510`) that **only reads**.
- Script command `GetCurrentLevel` (`0x0059C230`): current level ID (a checksum) at game-flow object **`+0x64`**.
- Script command `GameIsPaused` (`0x0059BD30`): one flag byte at **`0x007CDD2C`**.

**Step 1-2:** the THAW plugin also reads, every 100 ms, in its **own** exception-protected function (separate from the skater read, so a failure in one doesn't hide the other): game-flow pointer, "in front end" (by calling `0x00594310`, read only), level ID, pause flag. Each is logged when it changes, and each position line is tagged `[menu]` or `[level]`. 4 more code checks (exact bytes at `0x0059A660`, `0x00594310`, `0x0059C230`, `0x0059BD30`). Still read only: nothing written into the game.

**Test:** launcher → stay on reTHAWed's main menu ~5 s → load a level → skate a little → pause (Esc) ~5 s → unpause → quit to the main menu → quit.

**Done when:** "in front end" is true on the menus and false in the level; the level ID changes when the level loads and again when back in the menus; the pause flag turns on and off with the pause menu; the position lines show `[level]` only while in the level.

**Attempt 1 (2026-10-07) failed:** in the running game, `InFrontend`'s final jump (at `0x0059A668`) doesn't go to `0x00594310` as it does in the file; most likely reTHAWed redirects it. Lesson: **reTHAWed changes some THAW functions in memory, so file-based checks can fail in the running game.** Attempt 2: checks split into groups (skater / state) so one can't block the other; InFrontend's jump is logged (target and which file it's in) instead of checked.

**Attempt 2 result (2026-10-07): works.** InFrontend's jump goes into `rthw.dll` (confirmed). The original check `0x00594310` correctly says YES on the menus and no in the level; level ID: menus `0x99CE4D72`, the test level `0xD94DB0CB`; pause flag ON when paused. **While paused the skater's velocity keeps its last value**, so use the pause flag, not velocity. Not tested: the flag turning off on unpause (the test went from paused straight to the menu). **Phase 1 is done.**

### 8.6 Phase 2 plan: CJ follows the skater (written 2026-10-07)

**Goal:** with **skate mode** on, CJ in GTA moves to match the reTHAWed skater, and faces the way the skater moves. With it off, GTA is completely normal. First time our code **changes** something in a game, so each step is small and has an off switch.

**What research found** (plugin-sdk source, commit `15f15b60`):
- `FindPlayerPed()` gives CJ. Position via `SetPosn` / `GetPosition`; heading via `m_fHeadingCurrent` / `m_fHeadingGoal` (CPed `+0x558` / `+0x55C`); speed via `m_vecMoveSpeed` (`+0x44`).
- Player controls: `CPad::GetPad(0)->DisablePlayerControls` (`+0x10E`) is a set of flags. Missions use `bPlayerSafe` and others; plugin-sdk marks two bits **unused**. We use one unused bit (bit 4) as **our own** "controls off" flag, so we never clear a flag a mission set. Not yet confirmed that the game treats any non-zero value as "controls off"; the log will show it.
- States to respect: game paused (`CTimer::m_UserPause`, `m_CodePause`), cutscene (`CCutsceneMgr::ms_running`), scripted camera with black bars (`TheCamera.m_bWideScreenOn`), player state (`CWorld::Players[0].m_nPlayerState`: playing / died / arrested), in a vehicle (`bInVehicle`), health.
- Ground height: `CWorld::FindGroundZFor3DCoord`.
- Hotkeys already used by installed mods: F5 (limit adjuster), F10 / F11 (SkyGFX), Alt+Enter (Windowed Mode). **Skate mode key: F7** (believed unused; to be confirmed).
- **Controller (user's request 2026-10-07): clicking both sticks, L3 + R3, also toggles skate mode.** GTA's own controller state has both (`CPad` `NewState.ShockButtonL` / `ShockButtonR`), so it works with whatever GInput feeds GTA. It counts as one press when both sticks are down and they weren't both down the frame before, so holding them doesn't flip it on and off. In GTA, L3 on foot is crouch and R3 is look behind, so **pressing the combo may also make CJ crouch for a moment**. We'll try hiding the combo from GTA for that frame; if that doesn't work, the brief crouch is a known side effect.

**The mapping** (one small function in `src/common/convert.h`, tested in the cloud with a unit test before it goes into a game):
- When skate mode turns on: remember CJ's position **C0** and the skater's position **P0** (the "anchor").
- Every GTA frame: CJ = C0 + M × (skater − P0), with
  - scale **0.0254 m per THAW unit** (assumes inches; phase 1 numbers fit, not proven);
  - axes: GTA x = THAW x, GTA y = THAW z, GTA z (up) = THAW y. Swapping two axes also mirrors left/right. Whether THAW needs that mirror is **unknown**, so the y sign is a constant to flip if "turn left in reTHAWed" makes CJ turn right.
- CJ never goes below GTA's ground (clamped with `FindGroundZFor3DCoord`), because THAW slopes don't exist in GTA.
- CJ faces the skater's direction of travel (from velocity), only while moving faster than a small threshold.

**Steps** (each built and tested before the next):

| Step | What | Changes a game? | Done when |
|---|---|---|---|
| 2-0 | **GTA keeps running when not in front.** Set `autoPause=0` in `modloader\WindowedMode\iii.vc.sa.windowedmode.ini` (approved folder). If GTA still stops, ask the user to approve `AllowAltTabbingWithoutPausing=1` in Widescreen Fix's settings (a different folder). | Settings only | **Done 2026-10-07:** with reTHAWed in front for ~50 s, GTA drew 60/s throughout; Widescreen Fix change not needed |
| 2-1 | **Link v2: reTHAWed publishes the skater.** Shared block grows to version 2 (`Local\SkateAndreas_v2`): skater position, velocity, in-level, paused, level ID, sample time, protected by a sequence counter so GTA never reads a half-written set. THAW plugin publishes about every 15 ms (logs still 10/s). GTA plugin reads it and logs it once per second. Plus `convert.h` and its unit test. | No | `gta.log` shows the same skater numbers as `thaw.log`; unit test passes (**passes in the cloud: 20/20**). **Done 2026-10-07:** GTA gets 93 to 96 samples/s, 0 to 16 ms old, values on reTHAWed's path, states and pause on/off arrive correctly |
| 2-2 | *(**Done 2026-10-07** after fix 1: CJ follows 1:1, faces the right way (ySign −1, left turns now left), holds through GTA's menu and reTHAWed's pause, toggles work. L3 + R3 is not hidden from GTA)* **Skate mode moves CJ.** F7 or L3 + R3 (only while GTA is the front window) toggles it. While on, after each GTA world update: CJ placed at the mapped position (plus velocity × age of the sample, for smoothness), speed zeroed, heading set, our controls-off flag set. | **Yes: CJ's position, heading, speed, one control flag** | Logs show CJ's position matches the target each second; user sees CJ move as the skater moves |

**Shared controller (found in the first test):** while skating in reTHAWed, GTA also reads the controller (through DualSenseX). Start opens GTA's pause menu; skate mode now holds while it is open. Fixed properly in phase 3 (GTA keeps the controller and passes it to reTHAWed).

**Skate mode switches itself off** (restores controls, leaves CJ where he is) when: CJ dies, is arrested or enters a vehicle; a cutscene or black-bar scene starts; a mission or script disables controls; reTHAWed leaves the level; the link is lost (no new skater sample for 1 s). It **holds** (stays on, doesn't move CJ) while GTA or reTHAWed is paused. Every switch on, off and hold is logged with the reason.

**Not touched:** saves (don't save during tests), missions (test in free roam), the world, traffic, other peds, CJ's animations (he may look like he's standing or falling while he slides; animations are a later phase).

**Test for 2-2 (one monitor):** go somewhere **flat and open in GTA** (e.g. an empty parking lot or the airport) and in reTHAWed load a level and stand on flat ground. GTA: turn skate mode on (try F7 once, and L3 + R3 once, off and on again). Switch to reTHAWed: skate straight ~5 s, turn left, ollie twice, pause ~3 s, unpause, stop. Switch to GTA, check where CJ is and which way he faces, turn skate mode off, walk around to confirm normal controls. Claude reads `gta.log` and `thaw.log`.

**Risks:**
- **Seeing both at once:** reTHAWed runs borderless at 3840×2160, covering the screen, and the user has **one monitor** (confirmed). So in phase 2 the user sees CJ's result after switching back to GTA, and the logs prove the rest. Watching live needs phase 3 (reTHAWed hidden, input passed through from GTA).
- **The controller combo in reTHAWed:** if reTHAWed also reads the controller while it's in the background, L3 + R3 pressed in GTA may do something in reTHAWed too. Will show in `thaw.log` / reTHAWed's `debug.txt`.
- **CJ jitter:** GTA's physics may fight our position each frame. Mitigation: set the position after GTA's world update (so what's drawn is ours) and zero his speed. Measured in the log (target vs actual).
- **Controller read by both games:** GInput may still read the controller while GTA isn't in front. Our controls-off flag should stop CJ reacting; untested.
- **THAW slopes** become CJ floating above flat GTA ground (clamped so he can't sink). Expected until phase 4.
- **Our "unused" control bit** may not be unused. The log records the full flags value whenever it changes.

### 8.7 Phase 3 plan: play it live (written 2026-10-07)

**Goal:** with skate mode on, you stay in GTA the whole time. Your controller moves the skater in the hidden reTHAWed, and you watch CJ follow live. Start in skate mode does nothing in GTA (no pause menu); it goes to reTHAWed instead (pause there is still seen as "hold"). With skate mode off, GTA is completely normal and reTHAWed ignores the controller unless it's the front window.

**What we know already:**
- reTHAWed runs fine behind GTA (GTA in a window, `autoPause=0` keeps GTA going in front or not). Our THAW plugin kept publishing ~95 samples/s while GTA was in front, **but that is our own thread**: whether THAW's *game* keeps running when its window isn't in front is **not known**. The original THAW stops in a "waiting for focus" loop; partymod replaces that loop with one that only handles window events (partymod `input.c`, `processEventsUnfocused`) and keeps a "window focused" flag (partymod: `0x0072E850`, not yet confirmed for reTHAWed's own `rthw.dll`).
- reTHAWed reads the controller in partymod's `pollController`, which asks SDL two things, over and over: `SDL_GameControllerGetButton` and `SDL_GameControllerGetAxis`. `rthw.dll` gets both from `SDL2.dll` through its import table (seen in its file). That is a small, clean place to hand it our input instead.
- SDL normally ignores controllers when the window isn't in front, so a hidden reTHAWed gets no input from the controller itself anyway.
- GTA reads the controller in `CPad::UpdatePads` (`0x541DD0`, plugin-sdk). The pause menu, camera and CJ all read the result afterwards. plugin-sdk has no ready-made event for it, so we have to find where GTA calls it.

**The plan (option B, the SkyCraft way): GTA owns the controller and passes it on.**
- **GTA side:** right after GTA reads the controller, our plugin copies the state (sticks, triggers, buttons, as GInput gave them to GTA) into the link, then, while skate mode is on, **blanks it** so GTA itself does nothing with it (no pause menu, no camera, no CJ). The skate mode toggle (F7, L3 + R3) is read before blanking, so it still works.
- **reTHAWed side:** our plugin replaces the two SDL entries in `rthw.dll`'s import table with ours. When reTHAWed is the front window: real controller (as now; menus work normally). When it isn't, and skate mode is on with fresh input (under 100 ms old): the input from GTA. Otherwise: nothing pressed, sticks centred, so the skater doesn't move while you walk CJ.
- **One input path for every controller GTA understands**, so it also works for real Xbox controllers later (DESIGN §11).
- Rejected for now, **option A**: let reTHAWed read the PS5 controller in the background itself (SDL setting `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1`). Less code, but two games read the same controller with no way to say who it's for; it depends on DualSenseX and the controller fix, and wouldn't work for Xbox players. Fallback if B's hook doesn't work.

**Button mapping** (GTA's pad names → SDL, the same physical buttons): Cross → A, Circle → B, Square → X, Triangle → Y, L1/R1 → left/right shoulder, L2/R2 (0–255) → triggers (0–32767), L3/R3 → stick clicks, Start → Start, Select → Back, D-pad → D-pad, sticks (−128…127) → SDL (−32768…32767, y the same way up; to check). Exact GInput values logged before anything is fed to reTHAWed.

**Steps** (each built and tested before the next):

| Step | What | Changes a game? | Done when |
|---|---|---|---|
| 3-0 | **Does reTHAWed keep running behind GTA?** Test only, no code: roll in reTHAWed, switch to GTA, wait 5 s, switch back. | No | **Done 2026-10-07: no.** At the moment of Alt+Tab (97.66 s) reTHAWed's pause flag (`0x007CDD2C`) turned ON and the skater froze mid-roll (same position and speed 503 for 22 s) until the user quit. reTHAWed **pauses itself when it loses focus** |
| 3-1 | **Keep reTHAWed running behind GTA** (needed, see 3-0). Attempt 1: tell reTHAWed's window it never loses focus. Our THAW plugin wraps reTHAWed's window procedure (`SetWindowLongPtr`, put back on exit) and swallows the "you lost focus" messages (`WM_ACTIVATE` inactive, `WM_ACTIVATEAPP` false, `WM_KILLFOCUS`, `WM_NCACTIVATE` false), the SkyCraft trick. Both THAW (its focus flags) and SDL (its focus events) learn about focus only from those messages. Logged: every swallowed message, and changes of the focus flags and pause flag. Attempt 2 if it still pauses: keep the focus flag `0x008A35D5` at 1 and find what turns the pause on. | **Yes: reTHAWed's window messages** | **Done 2026-10-07 (attempt 1):** window `SDL_app` wrapped; with GTA in front the skater kept moving for 24 s; focus flag stayed 1. One pause while in GTA: the user pressed Start, which reached reTHAWed (confirmed; expected until 3-3) |
| 3-2 | **Controller into the link** (read only for both games): link v3 adds an input block (buttons, 6 axes, time, counter; fits in the spare space). GTA plugin finds the `CPad::UpdatePads` call (logs the address, code-byte check), copies pad 0 there, logs it once per second; THAW plugin logs what it receives. *(**Done 2026-10-07:** all buttons/sticks/triggers arrive in order, 0–32 ms old; GInput doesn't hook this call. Written 2026-10-07: the call is at `0x53BEE6`, the first call in `CGame::Process` (`0x53BEE0`), before the pause menu at `0x53BF44`; `src/gta/padlink.cpp`.)* | No (GTA: a hook that only reads) | Pressing buttons in GTA shows up in `thaw.log` within ~20 ms |
| 3-3 | **reTHAWed uses the input from GTA**: swap the two SDL import entries in `rthw.dll` (logged; left in place until reTHAWed closes, as our plugin is never unloaded before that). *(**Done 2026-10-07:** source switches REAL / NOTHING / GTA as designed; 1,200 questions per second answered from GTA in skate mode; CJ followed live. Open: F7 sometimes needed an extra Alt+Tab.)* | **Yes: two import entries in reTHAWed** | Skate mode on, GTA in front: the skater moves (seen in `thaw.log`) when you push the stick in GTA |
| 3-4 | **GTA ignores the controller during skate mode**: blank pad 0 right after copying it. | **Yes: GTA's pad state, skate mode only** | Start in skate mode doesn't open GTA's menu, camera doesn't turn; normal again after skate mode off. *(**Done 2026-10-07:** pad blanked only in skate mode; Start paused only reTHAWed; L3 + R3 still toggles)* |
| 3-5 | **Play test**: skate around the airport live. Then tuning (camera, sound, smoothing) as found. | No | You skate CJ around the airport without switching windows. **Done 2026-10-07** (same run as 3-4). Found: CJ stops at the hidden level's walls (expected; section 5) |

**Found while planning 3-1** (read only, from `reTHAWed.exe` on disk, nothing saved in the repo): reTHAWed's window procedure handles `WM_ACTIVATE` at `0x00543D51`: it sets THAW's focus flag `0x008A35D5` to "active or not", and on losing focus also clears `0x0072E850` (used by the drawing code at `0x0050D44D`). partymod's pattern for its focus flag (`isFocused_again`) also finds `0x008A35D5` in reTHAWed.exe, so the earlier note "partymod's focus flag is `0x0072E850`" was only half right: that is the second flag. What actually switches the pause on after focus loss is not found yet (`0x007CDD2C` is written at `0x0057FE18`, among others).

**Risks / open questions:**
- **reTHAWed pauses behind GTA** (3-0: confirmed). If hiding the focus loss doesn't stop it, THAW may also wait in its old focus loop (partymod's `addr_unfocuspoll` patch) or reTHAWed's own code may pause on focus loss.
- **Side effect of "never loses focus":** SDL will then also deliver the PS5 controller to reTHAWed while GTA is in front (it thinks it's focused). Until step 3-3 replaces the input, pressing buttons while in GTA may move the skater too. Keyboard is safe (Windows sends keys only to the front window).
- **GInput** may replace parts of GTA's pad reading. Reading after `UpdatePads` returns should still see what GTA sees; if GInput reads the controller somewhere else, blanking may not catch everything (the log will show what GTA still reacts to).
- **Both games on the screen:** GTA in a window covers reTHAWed only if GTA is borderless full size; otherwise reTHAWed shows around it. Fine for testing.
- **Sound:** reTHAWed's music and sounds will play from behind. Turn reTHAWed's music off in its options for now; skate sounds may actually be nice. Later: our own option.
- **reTHAWed's camera, menus, tricks list** all need the same input; menus are handled by bringing reTHAWed to the front (real input there).
- **Keyboard players:** not covered (GTA's keyboard isn't in pad 0 the same way). Later.
- **Rule 5:** all single player, offline; nothing touches reTHAWed's online code.

### 8.8 Phase 4 plan: skate on GTA's real streets (draft, written 2026-10-07, not approved yet)

**Goal (user, 2026-10-07):** eventually the whole San Andreas map skateable: the hidden skater hits GTA's walls and kerbs, grinds GTA's rails. Short term: one area where it already works.

**What we have: "Los Santos East"** (thpsgoat.com mod 598, by PunkyDTH, uploaded by yngzoch; installed by the user in `reTHAWed\data\mod\UserMods\Maps\Los Santos East`). A piece of GTA SA's East Los Santos (skatepark and hospital area) already turned into a reTHAWed level. Looked at read only (copy in the cloud scratch area, nothing in the repo):
- `level.ini`: "Converted with reMAP", level prefix `SALSEAST`.
- `SALSEAST.pak.wpc` (107 MB) holds the textures (~59 MB), the visible geometry (~28 MB), the level script with its node list (`SALSEAST.qb.wpc`, 2.9 MB, includes rail names such as "GrindWoodRailing") and **the collision** (16.7 MB, THAW collision version 10: 7,532 objects, 686,760 corners, 495,075 triangles).
- So someone has already done the full chain SA geometry -> THAW collision + rails, and the result is a format reTHAWed loads as a user map. That is the target format for our own converter later.

**Steps (each tested before the next):**

| Step | What | Changes a game? | Done when |
|---|---|---|---|
| 4-0 | **Line the two worlds up.** Find the formula from Los Santos East's coordinates to GTA's (scale, which way each axis points, rotation, offset). Two ways, both read only: (a) from the data: GTA's own collision for that area (from the user's `gta3.img`, read only, extracted to the cloud scratch area) against the level's collision, matched by shape; (b) in the game: the user puts the skater and CJ on the same 3 landmarks (corners, a lamp post) and both logs record the positions. *(2026-10-08: (a) gave no trustworthy answer (no name matches; GTA's text map files hold too few LA objects); did (b) with F9 marks)* | No | One formula that puts 3+ landmarks within ~0.5 m. **Done 2026-10-08:** 41.27 units per metre, no rotation, offset (1789.21, -1397.86, 24.98); errors 0.1–0.5 m sideways, under 1 cm in height (`src/common/convert.h`) |
| 4-1 | **Absolute mode:** while reTHAWed is in Los Santos East, skate mode places CJ at formula(skater) instead of anchor-relative, so the skater's walls and rails are GTA's walls and rails. Turning on far from the skater's spot: first version simply moves CJ to the skater (logged); later: move the skater to CJ instead. | Yes (as skate mode now) | Skating along a wall in LSE, CJ runs along the same wall in GTA. **Done 2026-10-08:** user: "seemed to line up well"; CJ jumped 998 m to the skater's spot on turning on |
| 4-2 | *(Plan in section 8.9, 2026-10-08)* **Whole map, research:** how the level was built (reMAP and the tools before it), THAW's collision and node formats (the LSE files are a worked example), how big one level can be, how to switch levels as CJ moves without the skater freezing. Write the converter plan. | No | Plan for the converter in this file |
| 4-3+ | **Our converter** (later): reads GTA's collision from the user's own install, writes THAW collision + rail nodes per area, on the user's PC only. Rails guessed from shape (section 5.1). | No (makes local files only, gitignored, never in the repo) | One converted area skates like LSE |

**Rules:** game files (GTA's, LSE's) are only read; anything extracted stays in gitignored folders or the cloud scratch area (AGENTS rule 1). LSE is someone else's mod: used locally only, not changed, not redistributed. A released mod would ship the converter, never converted files.

**Open questions:** how LSE's coordinates relate to GTA's (4-0); whether reMAP's source was a THUG2 / THUG Pro version of the level (search the authors' pages); how big a THAW level can get before memory or float precision trouble; loading time when switching levels.

### 8.9 Phase 4-2: whole-map research and converter plan (written 2026-10-08)

**How Los Santos East was made** (thpsgoat.com "Using reMAP" page): reMAP "converts THUGPro maps to playable maps" for reTHAWed 4.2+. It is a Node.js wrapper (by ZedekThePD, `gitgud.io/ZedekThePD/nx-scripts`) around **SceneConverter** (THUG2 scene, collision, materials -> THAW) and **NodeQBC** (`gitgud.io/fretworks/nodeqbc`, scripts and node lists). So the chain was: GTA SA -> a THUGPro custom level (most likely built in Blender with denetii's **io_thps_scene** addon, which exports THUG2-format levels and has an "autorail" tool that makes rails from mesh edges) -> reMAP -> reTHAWed user map. The SA -> THUGPro part was the creator's own work and is not published.

**What a reTHAWed user map needs** (from the LSE files): a folder in `data\mod\UserMods\Maps\<name>\` with `level.ini` (name, level prefix, time-of-day preset, images) and `<PREFIX>.pak.wpc` holding textures, visible geometry, the level script with its **node list** (rails, start points, ...) and **collision** (THAW collision version 10). For us only the collision and the rails matter (GTA draws everything); the visible part can be minimal, if reTHAWed accepts that (to check).

**GTA's side** (public format, gta.fandom.com "Collision File"): SA collision is "COL3": per model spheres, boxes (axis-aligned) and a triangle mesh (vertices as 3 x int16 / 128, faces with a surface material). The models are placed in the world by the map files (text `.ipl` in `data\maps`, plus binary "stream" `.ipl` files inside `gta3.img`, where the `.col` files also live). Everything needed is in the user's own install.

**Options for "the whole map":**
- **A. Offline converter (recommended first):** a tool on the user's PC reads GTA's collision + placements for an area and writes a reTHAWed user map with THAW collision + rails made from the shapes (kerbs, ledges, railings; section 5.1). Proven possible (LSE). Open: how big one map may be; changing area means loading a new map in reTHAWed (a loading pause), unless THAW's own zone streaming (it streams between its big zones, e.g. `z_ho` -> `z_hobh`) can be used.
- **B. Live collision:** feed GTA's collision around CJ into a running reTHAWed (THAW creates collision for moving objects at run time). Seamless and no files, but needs THAW's collision system reverse-engineered: much harder. Keep as a later research item.

**Research steps for A (each small, read only unless stated):**

| Step | What | Output |
|---|---|---|
| 4-2a | **Read THAW's level format from the LSE example:** a Python tool (`tools/`) that opens a reTHAWed map's `.pak.wpc`, lists its files, and turns its collision into a 3D file (OBJ) and its rails into a list, written to the gitignored `extracted/` folder. | Collision and rails of LSE visible in any 3D viewer; format notes in this file. **Done 2026-10-08 (cloud):** `tools/thaw_level_dump.py`; formats documented at the top of that file; LSE: 495,075 collision triangles, 11,574 rail nodes / 9,272 rail segments |
| 4-2b | **Read GTA's collision for an area:** a Python tool that reads `gta3.img` (its file list, the `.col` models, the binary `.ipl` placements) from the install path, read only, and writes the area's collision as OBJ to `extracted/`. | GTA's East Los Santos collision next to LSE's (they should overlap, using the 4-0 formula). *(Tool written 2026-10-08: `tools/gta_area_dump.py`; must run on the user's PC: `gta3.img` is 940 MB.)* **Done 2026-10-08:** all 499 models' collision found; GTA walls lie on LSE's walls (median 0.12 m, 95% within 0.38 m); GTA's collision is simpler (no lamp posts, trees, kerb details) |
| 4-2c | **Get SceneConverter / NodeQBC** (user downloads them; the cloud can't reach gitgud.io) and read how they write THAW collision and node lists; check their licences before reusing anything. io_thps_scene's source is public but has **no licence**: read for understanding only, nothing copied. | Notes; decision: our own writer vs. using their tools. **Done 2026-10-08:** no licences (learn only, no code copied); collision format fully known; decision: our own writer (see below) |
| 4-2d | **First converted area:** GTA collision for the LSE area -> our own reTHAWed map (collision + auto rails + almost no visuals). Needs the user's OK for a new folder in `reTHAWed\data\mod\UserMods\Maps\` (AGENTS rule 4). | Skating it feels like LSE. *(Step 1 written 2026-10-08: `tools/gta_to_thaw_map.py`. Attempt 1 (collision + script only) crashed while loading the collision: no parent scene. Attempt 2 added an empty `.tex` and an invisible scene: those load, then it crashed on the level script. Attempt 3 added LSE's empty load scripts and the `_NodeArray_SortedNames` / `_SortedIndices` lists: same crash. Attempt 4 fixes the real mistake: array headers are bytes `00 01 <kind> 00` (kind 0x0A structs, 0x0D checksums, 0x01 integers); ours had two bytes swapped: with that fixed **the whole level loads, collision included**; the crash moved to the empty sky PAK. Attempt 5: a sky PAK shaped like LSE's (tex, scene with 2 empty sectors, sky script with node list, lookup lists and a settings struct; PAK flag 1): **the map loads**, but the skater hangs in the air. Cause found: triangle corner order; THAW's floors have (B-A) x (C-A) pointing up, GTA's order gave the reverse. Attempt 6 writes corners as (a, c, b): **works, the user skated the skatepark bowl** (2026-10-08 12:52). GTA skate test on it: CJ follows well, walls line up (14:20). Step 2: vert (face flag 0x0008) on the skatepark bowl via a per-area table (`VERT_AREAS`: box, GTA models, height, steepness): **works, skates "pretty spot on" like LSE** (2026-10-08 19:09), after two fixes (box covering the whole bowl; vert also on the near-vertical walls above the lip))* |
| 4-2e | **Size and switching:** how big a map can be; load time; whether THAW zone streaming can switch areas without a pause. | Plan for covering the whole map |

**4-2c findings (2026-10-08; tools downloaded by the user to `D:\Mods\thps-tools`, read only):**
- **Licences:** SceneConverter, reMAP (both `gitgud.io/ZedekThePD/nx-scripts`) and NodeQBC (`gitgud.io/fretworks/nodeqbc`) have **no licence file**; only SceneConverter's bundled `math3d` is MIT. So: **no code copied** from them into this project. We learn facts about the formats from them (facts are not code) and may *run* them as separate tools the user installs (they need Node.js), but our own writer is the plan.
- **THAW collision (`.col`, version 10), now fully known** (from SceneConverter's `thaw_Collision.js` / `CollisionNode.js`, matching what 4-2a read from LSE): header = version 10, object count, vertex count, large-face count, small-face count, large-vertex count, small-vertex count, 12 bytes zero, **"super sector" rows and columns** (world width and depth / 1000 units, at least 1; LSE: 144 x 113), overall box min/max (xyzw). Objects (64 bytes): name checksum, u16 flags, u16 vertex count, u16 face count, u8 small faces, u8 fixed-point vertices, face byte offset, box min/max, vertex byte offset, **BSP tree offset**, light offset, 4 bytes 0. Then vertices (floats, or fixed u16 x 1/16), one light byte per vertex (pad to 4), faces (u16 flags, u16 terrain, 3 x u16; small: 3 x u8 + pad), **one "optimization" byte per large face** (pad to 4), then the **BSP trees**: u32 byte size, 8-byte nodes (split: u32 = axis (0..2) | (split point x 16) << 2, u32 = byte offset of the left child, right child = next node; leaf: u8 3, u8 0, u16 face count, u32 index into the BSP face list), then the face list (u16 face numbers), pad to 4.
- **Map assembly (reMAP):** a level PAK built from a folder of files (`.scn` scene, `.tex` textures, `.col`, the level `.qb` with the node list, plus a separate `_sky` PAK) with entries padded to 32 bytes and a "last" end entry, and a `level.ini` (`[ModInfo]` Description/Author/Version, `[LevelInfo]` Name/Description/TODPreset=THUG2/LevelPrefix).
- **Node list of LSE by kind:** 11,574 RailNode, 715 LevelGeometry (likely one per scene object), 111 LevelObject, 46 Waypoint, 18 **Restart** (start points), 12 GameObject, 7 GenericNode, 3 ParticleObject, 2 Pedestrian, 34 of an unknown class.
- **Answered (attempt 1, 2026-10-08): no.** Without a scene reTHAWed crashes reading the collision (null pointer; the collision attaches to its parent scene). Attempt 2: an empty texture file (header 0xABADD00D, 1, 4, u16 count 0) + a scene with no materials and one empty sector (flags 3, geom with 0 meshes) per collision object, PAK order tex -> scene -> script, collision; parents as in LSE. Earlier question was: does reTHAWed load a map with collision and nodes but **no scene / textures** (or does each collision object need a matching scene object / LevelGeometry node)? If not, we need a minimal scene writer (SceneConverter's `thaw_CScene.js` / `thaw_sMesh.js` show the format).

**Plan for 4-2d (our first converted area), small steps:**
1. **Writer, tested in the cloud:** GTA area (from `gta_area_dump`'s data) -> THAW `.col` (one object per GTA placement, our own simple BSP) + level script with a node list (a Restart node at the skatepark; rails later) + PAK + `level.ini`. Checked by reading it back with `thaw_level_dump.py`.
2. **Load test:** the user copies the map folder into reTHAWed (needs the user's OK for one new folder in `reTHAWed\data\mod\UserMods\Maps\`, AGENTS rule 4) and tries to load it. If it doesn't load: add a minimal scene.
3. **Skate test** with skate mode (absolute mode, a new alignment entry: our map uses GTA's own coordinates scaled, so the formula is exact by construction).
4. **Rails** from shapes (kerbs, ledges, railings): later step.

**Rules:** GTA and reTHAWed files are only read, from their install folders; everything extracted or converted goes to the gitignored `extracted/` folder (AGENTS rule 1) and never into the repo; nothing is shared. A released mod would ship the converter, not converted files.

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
- GTA must run in a window or borderless window, **not exclusive full screen** (exclusive full screen freezes reTHAWed). Done with ThirteenAG's Windowed Mode plugin v2.1 in `modloader\WindowedMode\` (Alt+Enter cycles window styles).

### 8.10 Phase 4-3 plan: vert everywhere (draft, written 2026-10-08, not started)

**Goal (user, 2026-10-08):** bowls, half pipes, ramps and empty pools all over San Andreas skate like in reTHAWed (vert air, spine transfers), so the mod feels like a true crossover. Starts only after the bowl test (4-2d step 2) shows vert works on GTA's shapes.

**What we know:** THAW face flag 0x0008 = vert (LSE: 1,544 faces, all sloped, normal-up 0.0 to 0.9). GTA's collision has no such mark. The tool already has a per-area table (`VERT_AREAS`: box, GTA models, height, steepness).

**Rule learned from LSE's skatepark (2026-10-08, matched in our map and confirmed by the user's test):** on a skate surface, vert = every face with normal-up between about 0.03 and 0.7 (54-degree walls, 65-degree lip, AND the nearly vertical walls above the lip: leaving those out stops the skater dead at the lip); plain = gentler faces (the bottom curve at 0.85) and floors; wallride (0x0004) = truly vertical walls (|normal-up| <= 0.01). Where LSE covers an area, compare our flags with LSE's before testing.

**Steps (one at a time, each tested in reTHAWed, then with GTA):**
1. *(Done 2026-10-08: 2 areas, 24 vert faces; both half pipes skate well, user 19:33.)* **Rest of the skatepark.** GTA has a second skatepark model, `laeskatetube1` (the half pipes): real curved transitions, faces going from nearly flat (normal-up 0.9) to nearly vertical (0.1) between about 14 and 23 m. Add it to the table. If the bowl test shows flat 54-degree walls feel wrong, this tells us whether curved GTA ramps feel better.
2. **A finder for candidates across the map.** A read-only tool that lists likely ramps / pools / bowls from GTA's collision, with GTA coordinates and a top-down picture per spot: steep faces (roughly 30 to 85 degrees) whose lower edge meets flatter ground (a "transition"), plus models whose names suggest them (e.g. containing `pool`, `ramp`, `skate`). Building walls, roofs and embankments are the false alarms to watch for.
3. **The user checks candidates in GTA** (F9 marks the spot) and says yes / no; the yes list goes into the table. Hand-picked first, so nothing odd (a roof that throws you straight up) sneaks in.
4. **Later, if the finder proves reliable:** let it mark automatically, with the table only for exceptions.
5. **Related, separate small steps (moved forward by the user, 2026-10-08 19:36):** wallride (0x0004) on vertical walls (LSE does this on 31k faces): *written 2026-10-08, every face with |normal-up| <= 0.01 that isn't vert, 65,023 faces in our area; not tested*. Grind rails from GTA shapes (kerbs, ledges, railings): plan below.

**Grind rails (written 2026-10-08, not tested in the game; see MODLOG):** kinds (a) top meets steep side and (b) open top edge with lower ground beyond (GTA roads have no kerb faces). Plan as drafted:
- *Format (from LSE's level script, 11,574 rail nodes):* a rail is a chain of nodes, each `Pos`, `Angles` (0,0,0), `Name`, `Class = RailNode`, `Type = Concrete`, `TerrainType` (LSE: mostly `TERRAIN_GRINDCONC` 6,674, `TERRAIN_GRINDMETAL` 1,379, also wood, plastic, chain link, rope, wire, round metal pole), the `CreatedAtStart` flag, and `Links` = the index of the next node in the chain (the last node has no `Links`). Optional: `CollisionMode = Geometry`, `TrickObject` + `Cluster` (scoring). **A one-entry array is stored inline** (header 00 01 01 00, count 1, then the value itself, no pointer): our array writer must handle that.
- *Where rails go:* edges of GTA's collision where a flat top (normal-up > 0.9) meets a steep side (|normal-up| < 0.3) going down: kerbs, ledges, planters, benches, low walls. Join touching edges into chains; skip short bits (< 1 m), very high edges (roof tops), and edges with something in the way above them.
- *Check against LSE first:* LSE has hand-made rails in the same area; compare where ours and theirs are (picture from above) before the in-game test.
- *Railings / handrails* (thin poles in GTA's collision) are a later, separate step.

**Things GTA draws without collision (2026-10-09):** some walls / fences / railings are only in GTA's visual models (e.g. the East LS skatepark's low wall and fence). Two ways: (1) **hand-made extras** in `tools/gta_extras.py` (wall runs: centre-line points with ground, top and fence heights; inside on the left; rail along the inner top edge): *started 2026-10-09 with the skatepark wall*; (2) later, for the whole map: read GTA's visual models (DFF) and pick out solid parts (not leaves, glass, decals), probably reviewed with the same finder-and-check approach as the ramps.


**Depends on:** the map covering more than East LS (whole-map size and streaming research, 4-2e), for spots elsewhere in San Andreas.

### 8.12 Phase 4-4 plan: how big can a map be? (written 2026-10-09, user OK'd starting)

**Goal:** know whether all of San Andreas can be one reTHAWed map, or how to split it, before scaling ramps / rails / extras.

**What we already know:**
- East LS (0.85 x 1.05 km incl. margin): 2,486 collision objects, 164k triangles, 7.4 MB level PAK; loads.
- 21,963 nodes (19,476 rail nodes, 4.2 MB level script) made reTHAWed hang while loading; 11,494 nodes load. LSE: 12,524 nodes, 495k triangles, 106 MB PAK (with visuals), loads.
- THAW itself streams its open world in **zones**: on loading a level the log lists many zone PAKs (`Z_HOBH`, `Z_HODT`, ...) and `zone_init` / `FinishPendingZoneLoads` lines. If user maps can be zones too, San Andreas could stream in pieces without loading screens.
- Coordinates: whole SA is about 6 x 6 km = 236,000 THAW units across; floats keep about 1 cm precision there. The collision header's super-sector grid would be about 240 x 240.

**Steps (one at a time):**
1. **R1 - how much is there?** New read-only tool `tools/gta_world_stats.py`: reads the whole map from the GTA install (same reader as `gta_area_dump.py`, interiors and far-away stand-ins skipped) and writes, per 500 m square: placements, collision objects, triangles, vertical faces; plus totals and a picture. Run by the user (needs `gta3.img`). Tells us the size of the whole job and how uneven it is (city vs countryside).
2. **R2 - where does reTHAWed stop?** `gta_to_thaw_map.py --box` builds bigger and bigger squares around East LS (same centre, so the GTA alignment stays valid): e.g. 1, 2, 4, 9, 16 km^2. The user loads each, notes load time / hang / crash and frame rate; rails on and off, to separate the node limit from the collision size. Gives the real limits in numbers.
3. **R3 - zones.** Read how reTHAWed's own world is cut into zones and streamed (zone PAK names, the scripts that load / unload them, what triggers a load), and whether a user map can have several zones. Read only, no game files changed.
4. **Decide:** one map, several maps switched behind the scenes, or zones; then write the plan for the whole map.

**R1 result (2026-10-09):** whole map 28,213 collision objects, 2.25M triangles (East LS test map: 7% of that); busiest 500 m square 59k triangles. See MODLOG.

**R2 ladder (each: build, copy, load in reTHAWed via the launcher, note: loads? how long? frame rate? collision works?):**
- T1: East LS, `--no-geometry-nodes` (164k triangles, 9,001 nodes): does collision still work without the LevelGeometry nodes?
- T2: `--box 1000 -2500 3000 -500 --no-geometry-nodes` (2 x 2 km, ~482k triangles, ~7,000 objects; LSE-sized).
- T3: `--box 0 -3000 3000 0 --no-geometry-nodes` (all of LS, ~728k triangles).
- T4: `--box -3000 -3000 3000 3000 --no-geometry-nodes` (whole map, 2.25M triangles).

**R2 results so far (2026-10-09):** T2 (541k triangles, 7,800 objects) and T3 (all of LS) load and skate. T4 loaded all collision (61 MB, 2.25M triangles) but hit **"Out of HashItems (30000 max)"**: one hash entry per collision object / sector / node, 30,000 in all. Fix: merge collision objects into 200 m tiles (whole map: about 700 objects). Known limits now: hash pool 30,000 (objects + nodes + the game's own); rail nodes tested up to 9,000.

### 8.11 Open items noted along the way (2026-10-09)

- **CJ's facing from the skater's facing, not his speed.** At rail ends the skater's speed flips for a moment and CJ turns around (see MODLOG 2026-10-09). Needs finding the skater's orientation in reTHAWed's memory (like phase 1 did for position); also needed for skate animations on CJ.
- **Skate animations on CJ**: first step planned in 8.13 (phase 5-0).
- **reTHAWed's own source code** (user offered the repository link, 2026-10-09; the link did not come through yet): could show where limits such as the 30,000 hash items or rail node counts are set. Reading it to learn is fine; changing reTHAWed itself means writing to its install or building our own copy, which needs the user's explicit OK (AGENTS.md) and a look at its licence first. Not started.

### 8.13 Phase 5-0 plan: CJ skating poses (written 2026-10-09, user asked for it)

**Found:** GTA SA has no skateboard. Its object list (`default.ide`) has `#340, skateboard` commented out: cut content, no model in the game files. It does have roller-skating animations for its roller-skating pedestrians: animation file `skate` in `anim.img` with `skate_idle`, `skate_run`, `skate_sprint`.

**Step 5-0 (this step):** while skate mode is on, CJ plays a pose picked each frame from the skater:
- in the air (feet more than 0.8 m above GTA's ground) -> `JUMP_glide` (GTA's own falling pose)
- slower than 1 m/s -> `skate_idle`; up to 7 m/s -> `skate_run`; faster -> `skate_sprint`
- ground poses held at least 250 ms (no flicker); blends at GTA's usual speed
- the `skate` file is loaded by GTA when skate mode starts and released when it stops; skate mode off = CJ's animation fades out, his normal ones return
- code: `src/gta/skateanim.cpp` (one task in CJ's task slot, `CTaskSimpleRunNamedAnim`), called from `skatemode.cpp`
It is roller-skating motion, not skateboarding, and does not show tricks.

**Step 5-1 (user chose 2026-10-09: stand-in board first, reTHAWed's board later):** a simple board built by our own code, no model file:
- shape in `src/gta/board.cpp`: 80 x 21 cm deck with kicked nose and tail (black grip on top, red underside), two trucks, four 5.4 cm wheels; 472 corners, 236 triangles; plain colours, shaded once by face direction (no textures, no GTA lighting: it will look the same at night)
- drawn by the plugin itself with RenderWare's immediate mode (`RwIm3DTransform`), hooked right after GTA draws the world (plugin-sdk `renderEffectsEvent`), so walls and CJ's legs still hide it correctly. Nothing added to GTA's files, no model ID used, no modloader files needed
- placed each frame by skate mode: wheels at CJ's feet, pointing where CJ faces; on the ground it tilts with the slope (ground height 30 cm ahead and behind, steps over 35 cm ignored, eased in); in the air it levels out
- CJ is lifted by the deck height (11.2 cm) so he stands on it
- skate mode off = board gone
Why not a GTA model file: that would need a model ID, a model file writer and streaming; drawing it ourselves is simpler, and the same drawing code can later show the reTHAWed board's shape (5-2) once we can read it.

**Later steps (not started):**
- 5-2 reTHAWed's own board: a converter reads the board model from the user's reTHAWed install at run time (nothing copied into the repository), the plugin draws that shape instead of the stand-in. Needs THAW's model format worked out first.
- 5-3 tricks and grinds: needs reading which trick/state the skater is in from reTHAWed's memory (like phase 1 did for position), then picking GTA poses or our own animations.
- CJ's facing from the skater's facing (8.11).

### 8.14 Phase 5-3 plan: skateboarding poses and tricks on CJ (written 2026-10-09, order OK'd by the user 01:34)

**Asked (user, 01:32):** skateboarding animations and tricks instead of the roller-skating moves, now that there is a stand-in board.

**What we have to work with:** GTA SA has no skateboarding animations at all (cut content, like the board). So every pose is new work. Two sources:
- **reTHAWed knows exactly what the skater is doing** (which trick, grinding, manual, in the air, where the board is pointing and how it is flipping). Our THAW plugin already reads the skater's position and speed from memory (phase 1, using partymod-thaw's open, MIT-licensed findings); the skater's facing, the board's rotation and the trick state are most likely in the same object or close to it. Reading them is research, like phase 1.
- **CJ's skeleton can be bent by our code** each frame (spine, hips, legs, arms, head), the way GTA itself turns CJ's head and torso to aim or look at things. No animation files needed.

**Steps (one at a time, each tested):**
- **R4 research: the skater's state in reTHAWed** (read only, like phase 1). Find and log: the skater's facing (also fixes CJ turning around at rail ends, 8.11), the board's own rotation (flips, shove-its), and the state: on the ground / in the air / grinding / manual / lip trick / bailed, plus the trick's name if it is readable. THAW plugin sends what it finds through the shared link.
- **5-3a board stance:** CJ stands sideways on the board, knees bent, arms out a little; crouches more when pushing fast and when landing; leans into turns. Bones bent in code over a neutral standing pose. Replaces the roller-skating animations (5-0 stays as a fallback).
- **5-3b board tricks:** the stand-in board copies the reTHAWed board's rotation, so kickflips, heelflips and shove-its show exactly as you do them. Needs R4.
- **5-3c body for each state:** tuck in the air, grind and slide stances, manual and nose manual lean, grab poses, bail (ragdoll-like fall or GTA's own fall animation). Needs R4.
- **Later, the full crossover idea:** copy the reTHAWed skater's whole skeleton pose onto CJ each frame (bone by bone), so every trick animation reTHAWed has plays on CJ. Big step; only after R4 shows the skeleton can be read and the bones can be matched.

**Order proposed:** test 5-0/5-1 first (already built), then R4 and 5-3a (they don't depend on each other), then 5-3b, then 5-3c.

**Risks:** R4 may take a few tries (memory layout unknown beyond position and speed). Bending CJ's bones needs the right moment in GTA's frame (after animations, before drawing); found by research in GTA's code, not guessed.

### 8.15 Phase R4: read the skater's facing, skeleton and board from reTHAWed (written 2026-10-09)

**Test of 5-0/5-1 (user, 01:42):** poses change with speed and air (log: 112 changes, "skate" file loaded as block 122), board shows and follows CJ but stutters; tricks don't show (expected). The stutter is most likely the slope tilt: GTA's ground under the board is measured straight down, which on vert, kerbs and rails jumps around. The real fix is the same as for tricks: take the board's angle from reTHAWed.

**What research found (read-only analysis of a copy of `reTHAWed.exe`, nothing run):**
- Script command `GetBonePosition` (table entry at 0x00730C70, code 0x0047DA90): finds an object, takes its component with checksum 0x222756D5 = "skeleton" (component lookup 0x0042E8C0: sorted (checksum, component) pairs at object `+0x60`, count at `+0x64`; fallback list at `+0x11C`, next `+0x10`, checksum `+0x4`), then calls 0x0046A9A0 for the bone.
- 0x0046A9A0: bone position from the skeleton (component `+0x18`), times the model's scale, turned by the **object's orientation matrix at `+0x88`** (4 x 4 floats, rows) and moved by the position at `+0x68`. So the skater object = `+0x68` position, `+0x88` orientation, `+0xC8` velocity.
- Skeleton (0x004EFF10): bone count `+0x14`; bone list `+0x18` (0x1C bytes each, first value = the bone name's checksum); current bone matrices `+0x04` (64 bytes each); bind-pose inverse matrices at `[[+0x08] +0x04] +0x28`. Bone matrix in the skater's own space = inverse(bind inverse) x current (row-vector order, as the game does).
- Checksums: THPS's CRC32 of the lower-case name (checked: "skeleton", "model", "id", "bone" match the game's numbers). Bone names from the thps-tools scripts (only names used).

**Step R4-1 (read only, log only):** code checks; 10 times a second the skater's orientation, `Bone_Board_Root` and `Bone_Pelvis`; bone list once per skater; all bones on F9. Test with reTHAWed only.

**R4-1 result (user test 01:52): works on the first attempt.** All 9 code checks match; 52 bones, every one named by our list (board #47, pelvis #1); the menu pose looks like a person holding a board. In the level: orientation rows are unit length; the "at" row points along the velocity while skating straight (0.96 to 1.00); the "up" row tilts up to 90 degrees on the bowl's walls (orientation includes ramps and walls). Board bone while riding: 3.7 units above the skater's feet; its "at" axis comes out of the deck (= skater up), its "up" axis points to the tail. In flips the deck normal turns over (138 to 166 degrees seen); after a pop shove-it the bone is turned 180 degrees about the deck normal. (10 samples per second catch only parts of a flip.)

**Step R4-2 + 5-3b (one step, written 2026-10-09):**
- link version 4 (`Local\SkateAndreas_v4`, 212 bytes): the THAW plugin reads facing and board every pass (about 65 times a second) and sends the facing (3 rows) and the board bone (4 rows) with each skater sample, flag `kPoseValid`.
- GTA board placed from the bone each frame: skater orientation x bone = THAW world direction, mapped to GTA like any THAW movement (scale, y mirror). The bone sits 9 cm above the wheels' contact; CJ's feet = the skater's origin. The old slope guess stays only as fallback when the bone is missing.
- CJ's heading from the skater's facing ("at" row) when it points enough sideways (not straight up a wall); from the speed otherwise.
- Not changed: CJ stays upright (no tilt on ramps yet) and keeps the roller-skating poses.

**If it doesn't work:** two real attempts, then `STATUS.md`.

**R4-2 + 5-3b result (user test 02:08): works.** Tricks show on the board; board follows reTHAWed's board bone in every frame. Odd look in bowl airs: the board turns with the skater while CJ stays upright.

### 8.16 Phase 5-3c plan: CJ turns and tilts with the skater (written 2026-10-09, user OK'd 02:10)

**Why:** on vert and in airs reTHAWed's skater leans and rotates (up axis up to 90 degrees off); the board copies that, CJ didn't.

**Step (one):** each frame, after GTA's heading is set, CJ's matrix gets the skater's up (from the facing's "up" row) and forward (the "at" row, made square to up), both mapped to GTA like any THAW direction; right = forward x up. His position is moved so his feet stay where the skater's feet are (feet + up x his height above the feet). GTA makes him upright again on its next update and we turn him again; when skate mode goes off he is set upright (`SetHeading`). Switch: `kTiltCj` in skatemode.cpp. The per-second log line says how many frames he was turned and the biggest lean.

**Risks:** GTA might not like a tilted ped (shadow, collision, camera); if so, the switch turns it off.

**5-3c result (user test 02:20): works.** Board stays with CJ as he turns; no more turning around at rail ends; log: CJ turned every frame, leaning up to 90 degrees on the bowl walls, no errors.

### 8.17 Phase 5-3d plan: CJ's body copies the skater's body (written 2026-10-09, replaces the hand-made stance of 5-3a)

**Why this instead of a hand-made stance:** R4-1 showed the whole skater skeleton is readable every frame. Copying it gives the sideways stance, pushing, crouching, grabs, grinds, manuals and bails exactly as reTHAWed plays them, instead of a few poses made by hand.

**THAW side:** 18 bone positions in the skater's own space (Pelvis, Stomach_Upper, Neck, Head; Bicep, Forearm, Wrist left and right; Thigh, Knee, Ankle, Toe left and right) read every pass and sent; link version 5 (`_v5`, 428 bytes), flag `kBodyValid`.

**GTA side (`src/gta/body.cpp`):** skate mode turns them into GTA world spots each frame (skater orientation x position x level scale, from the skater's origin = where the board's wheels touch). Right before GTA draws CJ (plugin-sdk `pedRenderEvent`, after GTA's own animation and `CPed::PreRenderAfterTest` updated his bone matrices; checked in gta_sa.exe: the bone update 0x532B20 is called from 0x5E6664 inside PreRenderAfterTest, not from the drawing), his bone matrices are changed, parents before children:
- hips (bone 1) moved to the skater's pelvis and turned so pelvis-to-neck and left-to-right hip match;
- spine (bone 3) turned to match stomach-to-neck and the shoulder line; neck (5) to match neck-to-head;
- upper arm, forearm (22, 23 / 32, 33), thigh, shin, ankle (41-43 / 51-53) each turned so it points at the next joint like the skater's.
Children follow their parents (local matrices kept). Bone parents come from RenderWare's push/pop flags; works whether GTA keeps the matrices in world space or CJ's own (checked at run time, logged once). If anything comes out not-a-number, GTA's pose is left alone. Left and right swap automatically if the level's mirror would put the skater's left side on CJ's right. Switch: `kCopyBody` in body.cpp.
- Tested in the cloud on a made-up skeleton with a real standing pose from the R4-1 log: every limb within 0.03 degrees of the skater's, hips exactly on the target, feet 2 cm from the ground spot.
- Not covered: twist of arms and legs around their own length (knees may point a little off), hands, fingers, face. CJ's proportions differ from the skater's, so feet may not sit exactly on the deck.

**5-3d first test (user 02:39):** works "pretty good"; shoulder shimmy and stutter at take-off. Fix 1: base animation held on the standing pose while copying; chest twist from the collar bones; half-updated samples (bone lengths off by more than 15%) skipped.

**Fix 1 result (02:44): no change.** Fix 2: sample time = when that reTHAWed frame was first seen (repeat reads made GTA's look-ahead jitter CJ along his path); chest twist from the skater's chest axis (link v6, 20 body points: + chest and a point on its right axis).

### 8.18 Phase 5-3e: frame recorder (written 2026-10-09, user OK'd 10:22; see STATUS.md)

**Why:** two fixes for the shimmy / stutter failed; measure instead of guessing.

**Step:** new `src/gta/recorder.h/.cpp`. While skate mode is on, F9 (GTA in front; the F9 mark is still logged too) records the next 180 GTA frames (3 s) into memory, then writes `logs/gta_frames_N.csv` (N counts up per session). One row per frame: time (high-resolution clock), GTA frame number, reTHAWed sample number, its age and the look-ahead used, the skater's position and speed, where skate mode wanted CJ and where he ended up, his up direction, the board, the body targets (hips, chest direction, right upper arm), and when GTA drew CJ with the copied body plus the hips as drawn. Read only.

**Test:** skate mode on; push along flat ground, press F9, keep pushing 3 s; then F9 again and jump off a ramp. Claude reads the files.

**Noticed while writing it:** the sample age comes from `GetTickCount64` on both sides, which only moves in steps of about 15.6 ms; the look-ahead (speed x age) then jumps in steps of up to 12 cm at 8 m/s. The recording will show whether that is the stutter.

**8.18 result (10:31):** the recording showed the look-ahead causing the stutter (0 / 16 ms ages give 0.07 / 0.68 m steps while the skater moves 0.39 m evenly). Fix 3: look-ahead off; neck no longer aimed (it tipped the face up); neck, head and face held in the first frame's pose so GTA's look-at and talking don't show.

**Fix 3 result (10:40):** better; still stutter (recordings: a third of GTA frames get no new reTHAWed frame), head still looks up / mouth moves (the neck is bone 4, not locked).

### 8.19 Phase 5-3f: blending (written 2026-10-09)
- THAW plugin notes when each reTHAWed frame appears (position change, checked about every 1 ms, high-resolution clock), reads it 4 ms later, sends the time (link v7).
- GTA keeps the last 8 frames and shows the skater 40 ms late, blended between the two frames around that time: even motion even when frames arrive unevenly; costs 40 ms of delay.
- Head: calm pose for neck (4), head (5), jaw / brows (6-8), taken after 20 frames; GTA's look-at and talking tasks stopped every frame while skate mode is on.
- F10: body copy on / off, to compare.

**5-3f/g results (10:51, 11:00):** F10 proves the copy is visible; reTHAWed runs at 60; blending works; remaining jerks come from the skater's orientation changing in steps on curved ramps (flat faces) and from GTA's animation rolling the arms. 5-3h: orientation smoothed (40 ms); every bone starts from a calm pose captured at the 20th frame, then is turned to the skater.

**5-3i result (11:39):** CJ looks much more natural. Both games at 30 = no stutter.

### 8.20 Phase 5-3j: even 60 fps for GTA (written 2026-10-09)
GTA's own limiter at 60 gives frames 14 / 19.5 ms apart in turn. Our pacer (`src/gta/pacer.cpp`) holds each frame to exactly 1/60 s on the high-resolution clock, only while GTA's own Frame Limiter is off (so they don't fight). reTHAWed's frames reach GTA blended (8.19), so reTHAWed's own rate matters less. If 60 still stutters, the next look is reTHAWed's animation setting (it only offers 30 or its default).

### 8.21 Research while the user was away (2026-10-09 11:45, read only, nothing run or changed)

**reTHAWed's frame rate** (from `rthw.dll`, reTHAWed's own code):
- reTHAWed's game runs on a fixed 1/60 s step: the patch for the `[Debug] UnlockFPSValue` setting in `rethawed.ini` rewrites exactly those numbers to 1/120 s and 120 ("Attempting to Cap Framerate to 120..."). Without it the game stays at 60. So "unlimited" in its menu does not make the skater move more than 60 times a second; our logs agree (60 new frames a second while skating).
- The animation frame-rate option is a separate script command, `RT_SetEmulatedAnimationFPS`: it lowers how often the skeleton animation updates (e.g. 30, the console look). It doesn't change the game's 60.
- Conclusion: the remaining 60 fps stutter is GTA's uneven frame spacing (14 / 19.5 ms), which the pacer (8.20) addresses. reTHAWed can stay on its defaults.

**The "Out of HashItems" limit (whole-map test T4) is a reTHAWed setting:**
- reTHAWed hooks the game's pool creation (`Mem::CCompactPool`). For each pool it first reads `rethawed.ini`, section `[Memory]`, key = the pool's name; if that is set it uses it, otherwise its own table. Its table (in rthw.dll) has, among others: `HashItem` 30000, `CComponent` 360000, `CStruct` 100000, `CVector` 30000, `Out set RailPoint` 20000, `Out set RailString` 5000, `CAP Rail CComponent` 50000. (The original game had HashItem 10000.)
- So the user can raise the limit in reTHAWed's own settings file, e.g. under `[Memory]`: `HashItem=100000` (12 bytes each: about 1.2 MB). No patching needed; our code doesn't touch reTHAWed's files (the user edits their own ini).
- The rail limit is very likely the same thing: `Out set RailPoint` = 20000; our map with 19,476 rail nodes hung while loading (8.12). `Out set RailPoint=60000` (and maybe `Out set RailString`) would allow far more rails than our 9000 budget.
- Not tested. Next whole-map test: T4 with the 200 m tiles AND `HashItem=100000`; then a rail test with a bigger `--rail-budget` and a bigger `Out set RailPoint`.

**5-3k result (user 14:43): skating smooth now.** (User also met an invisible wall on the LSE map: that map's own collision, not ours; the final map will be our own.)

### 8.22 Phase 6 plan (draft, written 2026-10-09 14:50, not approved yet): reTHAWed's HUD and body flips on the GTA side

**Asked (user 14:43):** manual balance meter, grind balance meter, trick score, special meter, and body flips (they don't show in GTA).

**Body flips:** CJ copies the skater's bone positions, so flips should come along; they don't. Needs a frame recording of a flip (F9) to see what the data does (does reTHAWed turn the whole skater object, the root bone, or only the skeleton; does a check of ours throw the samples away).

**HUD, two ways:**
- A. Copy reTHAWed's own HUD picture into GTA (exact look). Needs reTHAWed's 2D drawing caught in a separate picture and shared with GTA every frame: heavy, a lot of unknown ground.
- B. (recommended) Read the numbers from reTHAWed and draw our own HUD in GTA (GTA's font, simple bars in reTHAWed's colours). Each element is one memory-reading step (like the skeleton was).

**Research so far (read only, reTHAWed.exe):** the score object = skater `+0x2B0` -> `+0x18`. Script command `SkaterGetScoreInfo` (0x005A7690) returns `scorepot` = int at score `+0x14` x float at score `+0x60` (base points x multiplier), `totalscore` from 0x00569850, and a third value at score `+0x20`. Balance and special: not found yet (they are member functions known only by checksum; next: the HUD's own update code).

**Order proposed:** 1. body flips (recording first); 2. trick score + multiplier + trick names; 3. special meter; 4. balance meters (manual, grind). Each: log test in reTHAWed first, then drawing in GTA.

**User chose A (14:52): copy reTHAWed's own HUD picture.** Plan:
- H1 (research, log only): reTHAWed's Direct3D 9 device is at 0x00852044 (stored at 0x0052CDC0; reTHAWed draws through DXVK, its `_d3d9.dll`). The THAW plugin wraps 17 of the device's functions (Present, SetRenderTarget, Clear, SetTransform, SetViewport, draws, vertex formats, shaders, a few render states) and, on F11, writes every call of one whole frame to `logs/thaw_frame_N.txt`. Goal: find where the 3D world ends and the HUD begins (e.g. a switch to 2D vertex formats, an orthographic projection, z-test off).
- H2: at that point, send the HUD's drawing into a picture of our own as well (transparent background), still drawing reTHAWed normally.
- H3: copy that picture to GTA each frame (shared memory; size and rate to be measured).
- H4: GTA draws it over its screen after its own HUD.
- Body flips: wait for a recording of one (F9).


### 8.23 Phase 6 H2: catch reTHAWed's HUD in a see-through picture (user OK 18:03)

**H1 result (4 frame logs, 2026-10-09 18:00):** every frame ends the same way: 3D world on a full-size target (3840x2160 here), glow passes on small targets (1920x1080, 960x540, 240x135), back to the full-size target for one full-screen glow quad, then **all of the HUD**: 51-62 `DrawIndexedPrimitiveUP` calls with 28-byte screen-space vertices (FVF 0x144: position already in pixels, colour, one texture). Then one switch to a second full-size target (a final copy) and EndScene. In all 4 frames, `DrawIndexedPrimitiveUP` appears **only** in the HUD part.

**Rule:** the HUD starts at the frame's first `DrawIndexedPrimitiveUP` while a full-size target is set, and ends at the game's next `SetRenderTarget`, `EndScene` or `Present`.

**What H2 does (`src/thaw/hud.cpp`, using drawlog's wrapped device functions):**
- At the HUD start: remember the game's target and viewport, switch to our own full-size A8R8G8B8 picture, clear it to fully see-through. The HUD draws land there instead of on reTHAWed's screen.
- While it lasts, alpha is kept as "coverage" (separate alpha blend: ONE / INV_SRC_ALPHA), so the picture has proper see-through edges; colours come out pre-multiplied (to be drawn in GTA with ONE / INV_SRC_ALPHA). The game's own values for those 3 states are put back afterwards.
- At the HUD end: put the game's target, viewport and states back.
- Only while reTHAWed is in a level and **not** the front window (so its menus, and its screen when the user looks at it, stay normal). Touchpad + D-pad Left switches it off/on (temporary, for comparing).
- Before the device is reset (window or resolution change) our picture is released, so the reset can't fail because of us.
- **Test picture:** the frame logged by touchpad + D-pad Up / F11 also saves the HUD picture, half size, as `logs/thaw_hud_N.tga` (32-bit with see-through). Every 10 s thaw.log says how many frames had a HUD caught.

**Result (19:33): H2 works.** The game draws its HUD with colour write mask 0x7 (no alpha); we force 0xF while catching. Pictures show the whole HUD (score, special bar, trick names, multiplier, manual balance meter, messages) on a see-through background.

**Not in H2:** nothing goes to GTA yet (H3: share the picture; H4: GTA draws it). reTHAWed's hidden window shows no HUD while H2 is on (it is behind GTA anyway).

**Risks:** the rule may not fit other screens (e.g. trick names, pause or end-of-run screens); the test picture shows that. DXVK may not like a picture switch in the middle of its last pass (would show as a black/garbled HUD or no change).

### 8.25 Phase 6 H3 + H4: reTHAWed's HUD picture shown in GTA (user OK 19:35)

**H3, sharing (reTHAWed side, `src/thaw/hud.cpp`, new `src/common/hudlink.h`):**
- A second shared memory block, `Local\SkateAndreas_Hud_v1` (~18 MB): a small header + 2 picture slots of up to 1920x1200 (BGRA, colours pre-multiplied by alpha).
- Each frame, right after the HUD was caught: the GPU shrinks our full-size picture to fit 1920x1200 (here 3840x2160 -> 1920x1080, smooth filter) and starts copying it to memory. The copy is picked up **one frame later** (so reTHAWed doesn't wait for its graphics card), then written into the slot GTA isn't reading, with a counter that tells GTA if a slot changed while it was reading (same idea as the link's seqlock). The header says which slot is newest, its size, a frame number and a time stamp.
- Cost per frame: ~8 MB copied by the processor. thaw.log reports the time per frame every 10 s.

**H4, drawing (GTA side, new `src/gta/hudshow.cpp`):**
- Right after GTA draws its own HUD (plugin-sdk `drawHudEvent`, call at 0x53E4FF), only while skate mode is on and the newest picture is under 250 ms old.
- When the frame number changed: copy the newest slot into a RenderWare texture (RwRasterCreate 8888, RwRasterLock write). Then draw it over the whole screen with RenderWare's 2D drawing (RwIm2D, blend ONE / INV_SRC_ALPHA because the colours are pre-multiplied, no depth test), and put RenderWare's states back.
- gta.log every 10 s: frames shown, new pictures, half-written pictures skipped, upload time.

**Delay:** about 1-2 frames behind reTHAWed (fine for a HUD). **GTA's own HUD** stays for now (radar, money); hiding it while skating can come later. **Not in H3/H4:** the special-mode screen effect (8.24).

### 8.26 Body flips: reTHAWed's "display rotation" (user 20:00)

**Found (8 GTA recordings, 20:00):** in the air the skater's facing (`+0x88`) and all body bones stay completely steady during backflips, front flips and rolls. So the flip is not in the data we read.

**Where it is (read from reTHAWed.exe 5.0.0.1):** script command `RotateDisplay` (checksum 0xA4C25C2F) belongs to the **model component** (checksum 0x286A8D26, "model"); handler 0x004552B0. It doesn't move the skeleton; it queues a display-only rotation that is applied to the whole drawn model (body and board) when it is drawn:
- 3 queues in the model component, for the x, y, z axes, at +0x38, +0xCC, +0x160 (0x94 bytes each): 6 slots of 0x18 bytes, then a byte "anything active" at queue +0x90.
- Slot: +0x00 duration (ms, float), +0x04 start time (ms, int), +0x08 start angle, +0x0C total angle (degrees), +0x10 easing (0 = linear, n > 0 = sin^(2^(n-1)) of t x 90°, n < 0 = 1 - cos), +0x14 flags (1 active, 2 hold the end angle, 4 real clock instead of game clock). Angle of a slot (0x004534A0): start + total x easing(elapsed / duration); after the end: start + total if "hold", else 0.
- Clocks: real ms = (dword at 0x008AAD00) x 1000 / (float at 0x007346B4) (0x0062C350); game ms = (real - dword at 0x00742C14) x (float at 0x00719EA0), plus dword at 0x00742C10 for the slots (0x00436180, 0x004534D0).
- Axis angle = sum of its active slots (0x004543C0). The drawn matrix (0x00454940): L = Rx · Ry · Rz (DirectX row-vector rotations, degrees), turned around the pivot at model +0x28 (x, y, z), then x the skater's matrix. A byte at model +0x1F4 additionally turns the model 180° (not used yet; logged).

**Plan (THAW side only, read only):** `pose.cpp` computes L and the pivot each sample (after its own code checks of the addresses above). Before the body bones and the board are published, each skater-space point q becomes (q - pivot) · L + pivot, and the board's axes are turned by L. GTA needs no change: CJ's body and the board follow the data. thaw.log once per second: frames with a display rotation, largest angles, the +0x1F4 flag.

### 8.27 Radio in skate mode: reTHAWed's soundtrack as its own station (user OK 20:29)

**Asked:** while skating, a station selector (like GTA V's radio wheel) with reTHAWed's playlist as one station and GTA's stations as the others.

**Idea (no music files copied; AGENTS.md rules 1 and 4):**
- "reTHAWed" station = reTHAWed keeps playing its own soundtrack (as now: its sound already plays while it is hidden). The other stations = GTA's own radio, played on foot (normally GTA only plays radio in a vehicle); reTHAWed's music is then muted while its skating sounds keep playing. "Off" = neither.
- Selector (user 20:24: GTA V-style wheel; must work on PlayStation and Xbox controllers): **hold Back** (Create / Share on PlayStation, View on Xbox) about 0.4 s -> the wheel opens; the right stick points at a station; letting go picks it; the choice is remembered. Works the same on every controller because GTA sees all of them as an Xbox pad, and every button reaches reTHAWed through our link.
- Back opens a menu in reTHAWed (user 20:28). So: while Back is down we hold it back from reTHAWed; a **tap** (released before 0.4 s) is then passed on to reTHAWed as a short press (its menu opens on release instead of on press); a **hold** never reaches reTHAWed. While the wheel is open the right stick is held back from reTHAWed too (no camera turn). GTA ignores the controller in skate mode anyway.

**Research needed first (log-only steps, like H1):**
1. reTHAWed: how to mute / pause only its music, and skip a track (its own music commands, e.g. the music volume setting or a pause command), without touching its sound effects. Muting the whole reTHAWed window would also mute the board sounds.
2. GTA: how to start a radio station without a vehicle (GTA's radio track manager, as the radio in clubs/houses does), and stop it.
3. Showing the current reTHAWed song name (reTHAWed's HUD may already show it; it would come through our HUD copy).

**Research results (read from the cloud copies, 20:35):**
1. reTHAWed: script commands with names in the exe (table at 0x0072F468...). `PauseMusic` -> 0x004B4DE0 (cdecl, 1 = pause, 0 = resume; keeps the song position; sets the byte at 0x00763B72 = "music paused", also read by `MusicIsPaused` 0x004B4E70). `SetMusicVolume` -> 0x004B4F10 (volume 0-100 stored at 0x0071C598). `SkipMusicTrack` -> 0x004B5180. Sound effects are separate (not touched by these).
2. GTA (plugin-sdk): `AudioEngine.StartRadio(station, bass)` 0x507DC0 and `StopRadio(nullptr, false)` 0x506F70 (the ones interiors use); `AERadioTrackManager.GetRadioStationName(id)` 0x4E9E10 gives the names in the game's language. Station ids 1-11 (Playback FM ... WCTR), 12 = user tracks (to verify by logging).

**Step R1 (this build): station switch, no wheel yet.**
- GTA, `src/gta/radio.cpp`: the Back hold/tap filter sits in the pad copy (padlink): Back is kept from reTHAWed; a tap is sent on release as 4 frames of Back; a hold of 0.4 s steps to the next station: reTHAWed soundtrack -> GTA's stations -> Off -> ... The name shows at the top of the screen for 2.5 s (GTA's font). While skate mode is on: GTA station = StartRadio(id) and reTHAWed's music paused; reTHAWed = GTA radio stopped and its music playing; Off = both quiet. Skate mode off: GTA radio stopped and reTHAWed's music **paused** (user 20:35: reTHAWed's music only plays in skate mode while GTA runs). GTA closed (no word for 5 s): reTHAWed's music back to normal.
- Link `reserved[1]` = wanted reTHAWed music (0 leave alone, 1 play, 2 pause), `reserved[2]` = GTA's tick (older than 5 s = GTA gone = 0).
- reTHAWed, `src/thaw/music.cpp`: on its own game thread (inside our controller hook, at most every 30 ms) calls PauseMusic(1) / (0); only resumes music it paused itself. Code checks first.

**R1 test (user 20:45):** Back hold steps stations, names right (log: 1 Playback FM ... 11 WCTR, 12 user tracks, 13 radio off); reTHAWed station plays, reTHAWed music pauses/resumes. GTA stations did not play and GTA stuttered: GTA's interior-radio code (CAEAmbienceTrackManager 0x4D6E60, read from gta_sa.exe) stops any radio every frame while CJ is outside a club/house (StopRadio calls at 0x4D71D2, 0x4D71FD, 0x4D7221), and our restart every 3 s loaded the station each time (hitch). **Fix 1:** those 3 calls go to our function, which skips the stop only while our skate-mode GTA station plays (calls checked first: E8 -> 0x506F70); at most one restart per station.

**R2 (next): GTA V-style wheel with station icons** (user 20:45). Icons are not shipped with the mod or put in the repo (game artwork, AGENTS.md rule 1). **GTA's own station logos are in the game** (found 21:20): `models\fronten1.txd` (the Audio Setup menu's file) has 128x128 DXT1 textures `radio_playback`, `radio_krose`, `radio_KDST`, `radio_bounce`, `radio_SFUR`, `radio_RLS`, `radio_RADIOX`, `radio_csr`, `radio_kjah`, `radio_mastersounds`, `radio_WCTR`, `radio_TPLAYER`. The mod loads that file at run time from the install (read only, through GTA's own texture store) and uses them directly; nothing is copied or downloaded. Optional: PNG files in `GTA San Andreas\modloader\SkateAndreas\radio\` (`1.png` ... `11.png`, `rethawed.png`) would replace them. A missing icon = the station name in its slot. The reTHAWed station's icon is reTHAWed's own logo, read at run time (read only) from `D:\Games\reTHAWed\ReTHAWed_NotifIcon.ico` (256x256, checked 20:50); a `rethawed.png` in the folder above would replace it.

**R2 result (21:28):** the wheel works. **Volume:** GTA's stations were much louder than reTHAWed's skating sounds; GTA's per-frame music fader call (0x50B815 -> 0x506E40) now goes through ours, which scales it by a skate-radio level (default 50%) while our GTA station plays; D-pad Up/Down on the open wheel change it.

**Skate sounds behind GTA (21:40):** reTHAWed's sound effects are DirectSound buffers made without "global focus" (flags 0x400B0 / 0x400E0 at 0x00443F48 / 0x00443F7B), so Windows mutes them while GTA is in front (its music is a separate stream path and plays). The launcher starts reTHAWed paused and adds the flag (0x8000) in memory before it runs.

**21:49 (user):** sound fix works. GTA radio default stays 50%. Added: User Track Player slot (station 12, GTA's own logo); D-pad Up/Down on the open wheel changes the volume of the pointed station (reTHAWed = its own music volume through SetMusicVolume 0x004B4F10, link reserved[3]/[4]; put back when GTA closes).

**21:55 (user):** works. User Track Player with no songs plays GTA's commercials; not tested with real songs: revisit only if players report problems.

**R3 (user OK 21:56): remember the station and the volumes.** File `radio-settings.ini` next to our plugin (`GTA San Andreas\modloader\SkateAndreas\`, an approved write path), section `[Radio]`: `Station` (rethawed / 1-12 / off), `GtaVolume` (0-100, default 50), `RethawedVolume` (0-100, or -1 = leave reTHAWed's own setting). Read once at the first world update; written with Windows' own ini functions when the wheel closes after a change. No other file is written.

**Then (maybe):** skip track (SkipMusicTrack).

### 8.28 Next features (user 22:08; key F6 22:11; order as proposed, no objection)

Asked before special-mode look / map / GTA HUD: (a) tags, (b) freak-out on bails, (c) BMX, (d) vehicle spawner.

**(d) Vehicle spawner (small, first: also needed to test BMX).** F6 (user 22:11) opens a list of all GTA vehicles, BMX at the top; D-pad / left stick to move, Cross to spawn in front of CJ, Circle to close (works with the controller once open; GTA ignores the pad while the list is open). Names from GTA's own text. Spawning through GTA's own vehicle-cheat code (CCheat::VehicleCheat, plugin-sdk), which handles every vehicle type. GTA side only.

**(b) Freak-out (probably small).** Our HUD copy and body copy should already carry it (the meter is part of reTHAWed's HUD, the animation is on the skeleton). First a test with a recording (touchpad + D-pad Up / Down during a freak-out) to see what's missing, then fix only that.
**Result (user 23:16):** nothing missing, no code needed. The freak-out is switched off by default in reTHAWed's own options; once the user turned it on, the meter (HUD copy) and the animations (body copy) all showed in GTA. For the README: players have to turn it on in reTHAWed.

**(a) Tags (medium).** Research: how reTHAWed starts a tag (button, script event) and where the tag goes. GTA side: CJ plays GTA's own spray-can animation and a Grove Street tag appears on the GTA wall in front of him, using GTA's own tag model / texture from its files (the collectible tags), placed by a collision ray to the wall. Read from GTA's files at run time; nothing copied. Doesn't affect the map plan (the tag is placed on GTA's wall where CJ is; reTHAWed's own tag stays in reTHAWed).

**(a) research (23:20, from reTHAWed's scripts decompiled in a scratch folder outside the repo, and its exe):** tagging is reTHAWed's "Graffiti Tagging" option (THAW preset: on, hold Triangle while off the board; "Graffiti On Ground" also on). Holding Triangle runs a script that calls the skater's graffiti command 0x2BA63327 -> C code 0x005F8B20, which finds a wall (or the ground), sets byte +0x1C of the graffiti component to 1 (tagging), and the script plays the spraying animation (our body copy already shows it on CJ). Each frame the component's update (vtable slot 0x006EE3C8 -> 0x005F9B50) continues it while +0x1C is 1; when the tag is finished it runs the score script 0x2B8E0B2B ("Graffiti Tag", call at 0x005F933A) and clears +0x1C. GTA side: the collectible tag model is 1490 `tag_01` (txd `tags2_LAlae`), whose see-through amount is GTA's tag "alpha" (255 = Grove tag fully sprayed). GTA crashes if a tag object exists that isn't one of its 100 collectibles (two functions look it up in the collectible list and read address 4 when it's not there: 0x0049CD50 when the object is built, called at 0x00533DCE; 0x0049CEC0 when sprayed with the spray can, called at 0x00565C5E). Only "mission" objects (type 2) go into GTA's save; ours are normal ones (type 1), so they aren't saved.

**(a) step T1 (plan):**
- THAW: our update hook in vtable slot 0x006EE3C8 (calls the original) notes "tagging" (+0x1C); our call at 0x005F933A (forwards to 0x004135E0) counts finished tags. Published in link `reserved[5]`: bits 0-15 finished tags, bit 16 tagging now, bit 17 "tag watch working". Code checks first; nothing is patched if they fail.
- GTA: the two calls above go through our functions: for one of GTA's 100 tags they do exactly what GTA did; for our tags they set the alpha to 255 (fully sprayed) instead of crashing. Then, in skate mode, each finished tag: a line from CJ's chest 1.5 m forward against GTA's buildings / objects; a wall hit (normal not steep) = tag on the wall, centred on the hit, 2 cm out; otherwise a line 2 m down = tag on the ground under CJ, top of the tag pointing where CJ faces. The tag's facing side comes from the model itself (its vertex normals, logged the first time). Object 1490, type 1, no collision, static, CJ's interior. Up to 50 of our tags; the 51st removes the oldest. They stay until GTA closes or a game is loaded; not saved.
- Correction (test 23:36): the score script call at 0x005F933A only runs when a combo score is going (0x005F92F6 skips it otherwise), so it isn't "tag finished". The tag is put in reTHAWed's world by the call at 0x005F92E0 (to 0x005F8680, thiscall, 12 arguments, the 2nd = component +0x1E: 1 wall, 0 ground). We count that call instead and pass wall / ground on (link bit 18); GTA then looks for a wall only when reTHAWed tagged a wall. reTHAWed's rthw.dll wraps 0x005F8B20 (both its callers point to its own function, which calls the original), so the original code still runs.
- **T1 result (user 00:03, 10-10): works for ground tags** (green Grove tag, flat, where CJ sprayed). The tag model (tag_01.dff, read from gta3.img at run time) is 2 x 2 m, facing -X. The tag-placing call's two vectors are a downward line (two points 24 units apart up / down), not spot + direction; and its 2nd argument said "wall" for these ground tags, so its meaning is probably reversed. Wall tags: not tried yet; to look at with a wall test (log the vectors of a wall tag).
- Later (T2, after T1 works): spray can in CJ's hand and spray mist while "tagging now".

**(c) BMX (large, several steps).** When CJ gets on a GTA BMX: switch reTHAWed's skater to its BMX mode, then GTA's bike and CJ follow reTHAWed's bike like CJ follows the skater now (position, facing, bike pose, CJ's body on the bike). Research first: how reTHAWed enters / leaves BMX mode (script command), the bike's bones, whether BMX is available in the user's save. Then the follow, then the animations.

**Proposed order:** (d) spawner, (b) freak-out check, (a) tags, (c) BMX. Each tested before the next.

### 8.29 (c) BMX mode (user 00:07, 10-10; written 00:20)

**Asked:** reTHAWed's BMX (physics, tricks, animations) starts as soon as CJ gets on a GTA BMX. The radio wheel works as in skate mode. GTA's first missions (riding the BMX with the gang) must still be completable.

**Research (reTHAWed, read only; scripts decompiled in a scratch folder outside the repo):**
- reTHAWed's own bike switch is script 0x27065DA5 (used by its Vehicles quick menu and the debug menu's "Toggle Bike"); without parameters it toggles (on the bike <-> off), loading the "bmx_anims" set.
- "Is on bike" (skater command 0xE91EAA16) = byte +0x524 of the skater's component 0x5BD63B29 (handler 0x005C74D0). Components are found with 0x0042E8C0 (thiscall, object, checksum).
- Scripts are run with 0x004135E0 (cdecl: script checksum, parameters or 0, object, 0, 0, 0), as reTHAWed's own C code does (e.g. 0x005F9263).

**Rules for missions (so they stay completable):**
- CJ stays *in* GTA's BMX the whole time (GTA vehicle, CJ the rider). Missions see "CJ on his bike" as usual; the bike is moved to follow reTHAWed's rider instead of CJ being taken off it.
- When BMX mode starts, reTHAWed's rider is moved to the GTA bike's spot (not CJ to the rider, as skate mode does), so where GTA / a mission put CJ counts.
- Only where reTHAWed's map lines up with GTA's world (Los Santos East, or our GTA map); elsewhere CJ rides GTA's normal BMX (logged).
- When GTA takes control (cutscene, mission switches player control off, CJ taken off the bike), BMX mode stops and the bike is GTA's again.
- F7 / L3 + R3 switches it off and on by hand (escape hatch if a mission misbehaves).
- To check with the first missions once B3 works (the "Big Smoke" bike ride).

**Steps (each tested before the next):**
- **B1:** link version 9 (`gtaBikeWish`, `thawBikeState`). GTA: CJ riding a BMX (model 481) -> wish "on bike", otherwise "off". THAW (on reTHAWed's game thread, at most every 1.5 s): runs the toggle script when the wish and the "on bike" byte differ, while in a level and not paused; publishes the byte. Logged both sides. GTA's own bike and controls unchanged in B1. Test: get on a BMX with F6, check reTHAWed's rider switched to the BMX (log, and look at reTHAWed).
- **B1 result (00:29):** works (rider gets on / off with CJ; a switch takes about 3 s, so one toggle per change).
- **B2:** move reTHAWed's rider to the GTA bike's spot when BMX mode starts. Found: reTHAWed's Obj_SetPosition (0x0042E9E0) writes the object's position (+0x68) and then calls every component's "teleported" function (component list +0x11C, next +0x10, vtable +0x24); facing is the matrix at +0x88 (rows right, up, at). We do the same from the THAW plugin on reTHAWed's game thread, plus facing and speed 0, when GTA asks through the link (version 10).
- **B2 result (00:38):** works (rider 0.00 m from the GTA bike).
- **B3 (written 00:55, not tested):** see MODLOG. Getting off: in reTHAWed with its own get-off-the-board buttons (L1 + R1; since fix 3 CJ then gets off the GTA bike too), or F7 / L3 + R3 then Triangle. While BMX mode is on, GTA doesn't push the rider back on the bike. GTA's replay (F1) is blocked in skate / BMX mode (fix 2). GTA's Triangle can't go straight through in BMX mode (it's a trick button in reTHAWed).
- **B3 result (01:25):** works: riding, jumps, radio wheel, bails, F7 / L3 + R3, L1 + R1 gets CJ off. Still to check: the first mission ("Big Smoke" bike ride) once the map covers its route.
- **B3 plan:** BMX mode itself: controller to reTHAWed (as skate mode), GTA's bike + CJ follow the rider (position, facing, lean); radio wheel as in skate mode; the rules above.
- **B4:** looks: CJ's body copies the rider (tricks), the bike's tilt / spins; the bike's own parts (bars, wheels) if possible.
  - **B4a (01:30):** CJ's body copies the rider's bones each frame with skate mode's body code (body.cpp), the rider's origin at the GTA spot the bike follows. The GTA bike itself still stays upright.
  - **B4a result (01:33):** CJ flips; the GTA bike doesn't and drifts from him in jumps.
  - **B4b (written 01:45, not tested):** the GTA bike's tilt / flips shown only when drawn (turn its picture right before GTA draws it, put it back after), so GTA's camera and physics keep an upright bike (test 00:51: tilting the real bike swung the camera). Lining up: at the 10th BMX-mode frame (rider standing on the bike) note reTHAWed's bike ("board" bone) and the GTA bike; then apply reTHAWed's bike's turn and shift since then to the GTA bike's picture (no guessing of reTHAWed's bike axes).
  - **B4b result (fix 3, user 12:41):** the bike stays with CJ and flips with him (the cause was GTA's `CMatrixLink::UpdateRW`, which does nothing for entities; `CEntity::UpdateRW` is the one). Two things left: riding stutters (like the old skate stutter), and the trick moves (tabletops, crank flips, bar spins) don't show.
- **B5 (written 12:50): smooth BMX riding.** Cause, from the code: BMX mode still shows the newest reTHAWed frame plus a guess ahead (speed x sample age), the exact thing that made skating stutter (8.18: ages only known in 16 ms steps). Skate mode's cure (8.19-8.20: keep the last 8 frames, show reTHAWed 40 ms late blended between two frames, on an even frame clock, orientation smoothed) moves into its own file `src/gta/blend.cpp` (same code, as a class) and both modes use it. Skate mode behaves exactly as before.
- **B6 (written 12:50, research, log only): which bike parts move in tricks.**
  - reTHAWed: the rider's skeleton has 5 "board" bones (Board_Root, Board_Nose, Trucks_Nose, Board_Tail, Trucks_Tail). On the bike, Board_Root is the bike's frame (B4b); the other four are probably the bars / fork, wheels and cranks. New `src/thaw/bikeparts.cpp`: while the rider is on the bike, every reTHAWed frame, each of the four bones' turn relative to Board_Root (angle, axis) and offset; logged when any turns more than 20 degrees from the riding pose (at most 20 lines a second), plus a per-second summary. Also: the whole bone list again whenever the skeleton changes (the bike may bring its own).
  - GTA: the BMX's parts (plugin-sdk `eBmxNodes`: chassis, forks_front, forks_rear, wheels, handlebars, chainset, pedals) with their names and positions logged once when BMX mode starts.
  - Test: ride, do a bar spin, a crank flip, a tabletop and a tailwhip (a few seconds apart); the logs show which bone does what.
- **B5 + B6 result (user 12:54, logs):** riding "a lot smoother"; slight stutter left in flips. The bike moves in tabletops. CJ's hands don't reach the GTA bike's bars (screenshots: arms hang down beside the bars, legs straight). Research log: Board_Nose and Trucks_Nose always turn together about one axis (fork + bars; up to 71 degrees in tight turns); Board_Tail and Trucks_Tail turn about the bike's sideways axis all the time while pedalling (the cranks). Offsets from Board_Root (THAW units; y = towards the back, z = up): nose 0 -15.6 9.4 (top of the fork), trucks nose -0.2 -22.5 -6.8 (front axle), tail / trucks tail ~0 2 -7 (crank axle). The wheels are not bones. GTA's BMX parts (in chassis_dummy's space, metres): forks_front at 0 0.361 0.280 (front wheel hangs from it), handlebars at the same spot (its own frame, not turned by GTA's steering), chainset at 0 -0.159 -0.211 with pedal_r / pedal_l at +-0.163 x and +-0.156 along the crank. chassis_dummy carries GTA's own lean (17 degrees seen).
- **B7 (written 13:05): bike parts and CJ's hands and feet.**
  - THAW side (`bikeparts.cpp`, now also sends): the fork's turn relative to the riding pose (3x3, in the bike frame's space), the crank's direction (the line from the crank axle towards the pedal on the bike's right, taken from the rider's foot in the riding pose and turned with the crank since), and for each side (bike's right / left) how much the rider's hand is on the grip and the foot on the pedal (1 = within 3 / 4 units of where it was in the riding pose, 0 = 10 / 12 units away; e.g. no-handers, crank flips). Link version 11 (`thawBike`, 16 numbers, flag `kBikeValid`), blended like the rest.
  - GTA side, only while the bike is drawn (put back after, like the bike's own turn): chassis_dummy unturned (reTHAWed's bike already has the lean), forks_front and handlebars turned like reTHAWed's fork, chainset pointed along reTHAWed's crank, pedals kept level.
  - CJ's hands and feet: where the grips are is measured once from the GTA bike's handlebar model (the outermost points of its mesh; fixed guess +-0.27 m, +0.10 m if that can't be read; logged). Each frame the grips and pedals are worked out in the world from the drawn bike. In body.cpp, after the copy, each arm / leg is bent (two-joint reach, the elbow / knee keeps the side it bends to) so the hand / foot lands on its grip / pedal, by the "on" amount from reTHAWed; the rest of the body stays copied.
  - Expected: hands on the bars, feet on the pedals, bars turning in turns and bar spins, cranks going round with pedalling and crank flips. Not addressed yet: the small stutter in flips.
  - **Gun in hand (user 13:08):** GTA gives CJ his drive-by weapon when he gets on a bike; that stays (a shoot mode for skate and BMX mode is planned after the map). So the hand placement must look right with a gun in the right hand too. B7 puts the hand bone itself on the grip and keeps the hand's turn relative to the forearm; GTA draws the weapon from the hand, so the gun goes with it (as it did with the plain copy, screenshots 12:54). To check in the B7 test; if the gun pokes through the bars, the right-hand grip point gets an offset for weapons.
  - **B7 fix 1 result (user 13:28, video + logs):** bars don't spin, frame doesn't whip in tailwhips. reTHAWed's Board_Nose turned at most 20 degrees all ride (several bar spins): the tricks aren't in the rider's bones. reTHAWed's bike is its own skinned model (`data\models\Veh\veh_BMX\veh_bmx.skin`), with its own skeleton and animations. Two attempts: STATUS.md.
- **B8 (written 13:50, research, log only): find the bike's skeleton.** New `src/thaw/bikefind.cpp`: 6 s after the rider gets on the bike, scan reTHAWed's writable memory once for skeletons (same layout as the rider's: bone count +0x14, bone list +0x18 with distinct name checksums, matrices +0x04, bind data [[+0x08]+0x04]+0x28); log each (the rider's should be among them: a check that the scan works); follow every one of 40 bones or fewer that isn't the rider's and log, each second, which of its bones turned most since the scan. The scan pauses our reTHAWed plugin for a moment (CJ may hitch once). Test: get on, ride straight 10 s, then bar spins, crank flips, tailwhips a few seconds apart.
- **B8 result (user 13:45, thaw.log):** the scan (1.6 GB in 15 s) found 5 skeletons: the rider's (52 bones), three false finds that never moved, and **the bike's: 8 bones, 0x3FE0 bytes after the rider's skeleton**. Bone names worked out from their checksums: (unnamed root), Bone_Bike_Frame, Bone_Bike_Crank, Bone_Bike_Pedal_R, Bone_Bike_Pedal_L, Bone_Bike_Front (fork + bars), Bone_Bike_Tire_Front, Bone_Bike_Tire_Rear. In tricks: Front turned 178 degrees (bar spin), Frame 180 (tailwhips), Crank and pedals round and round, tires spin all the time.
- **B9 (written 14:05): the GTA bike from reTHAWed's bike skeleton.** THAW side only (GTA side unchanged: it already turns its parts from the link):
  - `bikefind.cpp` now finds the bike skeleton quickly: through the rider's components (logged if so), else in the 2 MB around the rider's skeleton, by the Bone_Bike_Frame checksum; cached and checked every pass. The 15 s scan is gone.
  - `bikeparts.cpp`: in the riding pose, Bone_Bike_Frame is lined up once with the rider's Bone_Board_Root (offset logged); after that the "board" sent to GTA is the bike frame (so tailwhips turn the GTA bike; GTA's picture code unchanged). Fork = Bone_Bike_Front's turn relative to the frame since the riding pose (bar spins). Crank direction = from the Crank bone to the pedal bone on the bike's right (pedalling, crank flips). Hands: grips move with Bone_Bike_Front. Feet: on while each keeps its distance from its pedal bone (as in the riding pose).
  - Assumes the bike skeleton's bones are in the same space as the rider's (the old board bones carried the bike there); the riding-pose log shows the offset (should be a small fixed turn / shift).
- **B9 result (user 13:57, video + logs):** "the tricks are looking a lot better": tailwhips, bar spins (fork up to 179 degrees), tabletops and crank flips show on the GTA bike. Bike skeleton found at once (16 KB after the rider's skeleton, not through a component). Offset board vs bike frame = identity: same space, Bone_Bike_Frame sits exactly where Bone_Board_Root does. Feet still counted off most of the time: the riding pose was taken mid-trick (user didn't ride straight first), so the learned foot distances were wrong.
- **B9 fix 1 (14:20):** "straight bars" from the bike skeleton's default (bind) pose, not from the riding pose; the riding pose (hands on the grips) is taken only while the bars are within 10 degrees of straight (logged while waiting); a foot counts as on its pedal while it is within 22 THAW units of the crank axle (off beyond 32), no learned pose; per-second log of the feet's distance to the axle (to tune those numbers).
- **B9 fix 1 result (user 14:11):** tricks look good; feet look good most of the time; "slight awkward looking animations when CJ gets pedalling again"; still slight stutter. gta.log: GTA itself ran at 44-60 frames a second while riding (pacer: most 10 s windows had 370-500 late frames), so the stutter is GTA not keeping 60, not the BMX code.
- **B10 (written 14:30, asked by the user 14:11): L1 + R1 = from BMX to skate mode.** L3 + R3 stays "BMX mode off, GTA's normal BMX". When reTHAWed's rider gets off the bike (L1 + R1) BMX mode already ends and CJ gets off the GTA bike; now skate mode is also asked to start (`SkateModeRequestOn`, skatemode.cpp): it switches on by itself as soon as GTA allows (CJ off the bike, no script holding him, not paused), trying for 6 s; F7 / L3 + R3 meanwhile cancel the request. Skate mode then follows reTHAWed's rider as usual (on foot or on the board, whatever reTHAWed has him do after the bike). Also: the hands / feet "on" amounts ease in and out over ~0.1 s (time constant) instead of switching in a few frames (pedalling again).
- **B10 result (user 14:17, clip):** L1 + R1 to skate mode works; but while getting off, the GTA bike "rolled up like a board": during the 1 s before BMX mode ends, the GTA bike kept copying reTHAWed, whose rider was already off (so the "board" was his skateboard again).
- **B10 fix 1 result (user 14:25):** worked (bike stays). 
- **B10 fix 1 (14:25):** as soon as reTHAWed says the rider is off the bike, the GTA bike stops following: it stays where it was last placed, upright, not moving; CJ's copied body is dropped (GTA's riding pose) until he gets off. Logged once a second ("bike held still").
- **B11 (written 14:40, user 14:25): BMX <-> skate the quick way.** In reTHAWed, getting off the bike makes the board roll to the rider and he hops on; in GTA that was missed (1 s wait + GTA's get-off animation). And: getting on reTHAWed's bike from skate mode with its own Square prompt.
  - BMX -> skate: L1 + R1 held within the last 2 s + rider off the bike = getting off for real: no 1 s wait (that wait only tells bails apart). CJ is taken off the GTA bike at once (GTA's own "set ped out" task with its warp flag, then next to the bike); skate mode starts the same moment and copies reTHAWed's hop-on (body + board). If GTA keeps CJ on, the old get-off animation. The bike CJ left is remembered (GTA vehicle handle).
  - Skate -> BMX: in skate mode, reTHAWed's rider going from off to on the bike (Square) = skate mode off at once, a GTA BMX put where CJ is, facing his way, on the ground: the one he left (within 30 m), else the nearest free BMX within 15 m, else a new one (GTA's vehicle cheat); CJ put on it directly (`CCarEnterExit::SetPedInCarDirect`); BMX mode starts without its 3 s wait (that wait is for reTHAWed's own switch, already done).
- **B11 result (user 14:37, clip + log):** hopping onto the board from the BMX "a lot better"; getting on with Square works (CJ on the bike, BMX mode 0.1 s later) but looks off: CJ throws his board (reTHAWed's own animation, fine), then bends down to pick up a bike that isn't there (the GTA bike was elsewhere), then the GTA bike pops in under him lying on its side and he lifts it.
- **B12 (written 14:55): the GTA bike lies where reTHAWed's bike lies.** THAW side (`bikeparts.cpp` `DroppedBikeTick`): while the rider is off the bike, the bike skeleton's frame in THAW's world (board convention), sent as `thawDroppedBike` (link version 12, flag `kDroppedBikeValid`), but only once it is known to stay in the world (the rider moved 10+ units away and it moved under 3); if it moves with the rider instead (it's the rider's hidden bike, not the lying one), it is never sent and that is logged. GTA side (`bmxmode.cpp`): in skate mode, the GTA bike CJ left (remembered handle) is put there every frame (collision off meanwhile, like BMX mode's picture), so CJ reaches for a real bike and it lifts with reTHAWed's; when Square puts CJ on it, it isn't moved to CJ first.
- **Stutter, options for later (user to choose):** pace GTA at a lower even rate it can hold (e.g. 50 or 30; 30 + 30 was stutter-free in 5-3i), or lower GTA's graphics settings so it holds 60.
  - **B7 result (user 13:18, video + logs):** hands "better but slightly off in some moments"; feet and legs look unnatural on the pedals; trick moves "still not showing". Logs: grips measured from the bars' mesh (+-0.326, 0.342, 0.482 m); fork and crank numbers reach GTA (a 180 degree fork turn at a bar spin); but reTHAWed's feet counted "off the pedals" nearly the whole ride, so CJ's legs mostly kept the copied directions. Cause: the check assumed the feet go round the crank bone's own point; they don't (riding pose: both feet ~16 units below the bike frame, ~9-10 below the crank bone).
  - **B7 fix 1 (13:30):** a foot counts as on its pedal while it keeps its distance from the crank axle and its place along the axle (as in the riding pose), wherever round the circle it is (fully within 3 units, not at all beyond 8). The crank's angle now comes from the rider's feet while they're on (so CJ's feet and the GTA pedals go round together), and from the crank bone (continuing from there) while they're off. Logs added: how far the feet went round vs the crank bone each second (is Board_Tail really the cranks?), and the bars' turn as GTA will draw it (from GTA's own world matrices, to prove the parts reach the screen).

### 8.30 C1: smoother frames without lower graphics (user 14:47, written 15:00)

**Asked:** fix the occasional stutter without lowering graphics or frame rate.

**What we know:** the stutter is GTA not holding 60 frames a second while riding (gta.log pacer: 44-58 fps, most frames late), not the BMX code. Both games run at once on the same PC and graphics card; reTHAWed draws its whole 3D world every frame behind GTA, where nobody sees it (only its HUD is shown, inside GTA).

**C1 step 1: "lean drawing" in reTHAWed.** While reTHAWed is in a level and behind GTA, our THAW plugin drops reTHAWed's 3D world draws (DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP before the frame's first DrawIndexedPrimitiveUP); the HUD part (from the first DrawIndexedPrimitiveUP on, as found in 8.23) is drawn as before, so the HUD in GTA stays. reTHAWed's game, physics and animation don't depend on drawing. In menus, or with reTHAWed in front, it draws everything. On by default; touchpad + D-pad Right switches it (temporary, for an A/B test; to be removed before release). Logged: every 10 s draws dropped / drawn; GTA's pacer line shows the effect (frames per 10 s and how many were late).

**Test:** ride ~1 min with it on, then touchpad + D-pad Right, ride ~1 min with it off. Compare.

**C1 result (user 15:02, logs):** "bmx riding and skating were smooth and the hud was still there". thaw.log: ~1.1-1.6 million of reTHAWed's draws dropped per 10 s, ~27,000 drawn (the HUD): about 98% of its drawing gone. gta.log pacer: 585-600 GTA frames per 10 s (59-60 fps), before C1 440-580. (The pacer's "late" count stays high; it counts any overshoot, even tiny ones, so it isn't a stutter measure.)

**If it isn't enough, next candidates (measure first):** GTA side: the time our own code takes per GTA frame (HUD picture upload, body copy); reTHAWed's resolution (its own settings, the user's file).

### 8.31 Whole-map test fixes: invisible walls, skate mode anywhere, F8, crash log, GPS (user 15:34 + 17:55, written 18:10)

**T4 result (user, two sessions 15:22 and 16:11):** the whole map (798 tiles, 2.25M triangles) loads and skates. Problems: invisible walls on the way to San Fierro and Las Venturas, thrown into the water near SF, F8 to SF fell through the map and the game crashed; one more crash somewhere while skating.

**What the logs and the map file show (read only, map PAK and logs copied to the cloud):**
- The walls on the bridges are **big boxes in GTA's collision, standing across the road**: at the SF end of the Flint bridge (GTA -953, -274: 29 x 20 m, 16.7 m tall, the skater stopped against it and slid off the edge into the water) and at the start of the bridge to Las Venturas (1694, 395: 40 x 20 m, 10 m tall). A search of the whole map for tall box shapes that a GTA **car road** (the game's own path nodes, `data\Paths\NODES*.DAT`) runs straight through found the same kind on every bridge out of Los Santos (Flint, the two SF ends, the LV bridges, the Red County - Bone County bridges: 5.8 to 16.7 m tall). Cars drive there in GTA, so in the game these boxes are not solid: they are the "region closed" barriers that GTA's story removes. Our converter takes everything from the map files, so it kept them.
- The other stops in the logs were bails, braking, or a real 1 m kerb (GTA 2360, 331).
- The F8 crash isn't in any log (gta.log is started fresh on every launch, so that session's log was gone). F8 asked GTA for the ground height before the area's collision was loaded: GTA then answers a default height, CJ lands under the hills of SF and falls.
- The new GTA log ends in GTA's pause menu with no error; our plugin doesn't log a normal exit or a crash, so a crash can't be told from quitting.

**D1. Converter: leave out the barriers.** `gta_to_thaw_map.py` reads GTA's car roads from `data\Paths\NODES*.DAT` (read only; format checked: 64 files, 30,587 car nodes, 143,622 links, every link valid). A collision **box** of a GTA model is left out when: it is at least 4 m tall, its ground plan is at least 5 m across one way and 12 m the other (so lamp posts and pillars stay), and a car road passes through its ground plan at a height inside the box. Each one left out is listed in `removed_barriers.txt` (model, where, size) next to the map, and counted in `map_info.txt`. Not touched: triangle-mesh collision, small boxes, boxes above or beside roads.
**Test:** rebuild the map, skate over the Flint bridge to SF and over the bridge to LV (1694, 395).

**D2. Skate mode on anywhere.** Today, turning skate mode on in our GTA map moves CJ to wherever reTHAWed's skater is. New: the skater is moved to CJ instead, with the same request BMX mode uses (link `gtaPlace*`, done by the THAW plugin on reTHAWed's game thread, 8.29 B2): CJ's feet (GTA's ground under him), facing where CJ faces. Skate mode starts when reTHAWed confirms (`thawPlaceDone`), at most 1.5 s later; if it doesn't, the old way (CJ moves to the skater), logged. Skipped when the skater is already within 3 m of CJ (BMX -> skate keeps the board roll-in). Not inside interiors (GTA area code not 0): refused with a log line, since our map has no interiors.
**Test:** walk somewhere away from the skatepark, L3 + R3: CJ stays, the skater appears there and CJ skates off from that spot; L3 + R3 off and on again somewhere else.

**D3. F8 teleport fixed.** On F8: load the scene and its collision at the waypoint (GTA's `CStreaming::LoadScene` + `LoadSceneCollision`), then ask for the ground from 1000 m down (`FindGroundZFor3DCoord`, which says whether it found ground). Only when ground was found is CJ moved, 1 m above it. If not, it tries again every frame for up to 3 s, then gives up with a log line (CJ stays where he was).
**Test:** waypoint in SF hills (e.g. near the Gant Bridge), F8: CJ lands on the ground.

**D4. Crash and exit logging.** The GTA plugin keeps the previous log as `gta.prev.log`, logs "GTA closing normally" when it unloads, and installs a last-chance crash handler (Windows' unhandled exception filter, chained to whatever was there before) that writes the error code, the program address and which file it is in (e.g. gta_sa.exe + offset, or another mod), CJ's position and whether skate / BMX mode was on. The THAW plugin keeps `thaw.prev.log` too.

**D5. GPS route to the waypoint (like GTA V).** New `src/gta/gps.cpp`. At start it reads GTA's car roads from `data\Paths\NODES*.DAT` (read only, ~4 MB, the same files GTA ships). While a waypoint is set: the shortest route along the roads (A* search) from the road nearest CJ to the road nearest the waypoint, worked out again when the waypoint changes or CJ is more than 40 m off the route (at most twice a second). Drawn as a purple line on the radar (clipped to the radar's circle, under the blips) and on the pause-menu map, from CJ along the route to the waypoint. Reaching the waypoint (within 15 m) removes it. Boat-only roads (water nodes) are left out. Works on foot, skating, BMX and in cars.
**Test:** set a waypoint across the city, check the line on the radar and the pause map, follow it; set one in SF and LV.

**8.31 test 1 (user 18:37, two GTA runs, logs):**
- **Both crashes: our bug, found.** gta.log's new crash lines: both at gta_sa.exe 0x691238, writing to address 0x10, with the same arguments (7, 0xAF0). That is GTA's ped speech code (call at 0x4E5E7C) telling the ped's talking-face task to talk for 2.8 s, without checking the task exists. Since 5-3f our skate mode removed CJ's talking-face task every frame ("look-at / talking stopped 1 times" in each session); GTA creates it only once, so the next time CJ said something, even with skate mode off, GTA crashed. Likely also the earlier crashes. **Fix:** only the look-at task is stopped now.
- **GPS line wobbly: our bug, found.** The "how far along" search started one piece too late, so the line ran ahead of CJ every frame and the route was worked out again every half second (1,043 times in one run). Fixed (the search includes the piece CJ is on); a simulated 5.8 km trip along the route now finishes with no re-routing.
- **Skate mode on at CJ: worked when the skater was already near or the way was clear, failed twice** (once just after loading the level, once after F8): reTHAWed confirmed the move, but its skater stopped at the first wall on the straight line between where it was and CJ (first run: on a roof 150 m from the skatepark, exactly on that line). The skater's collision sweeps from its previous position. **Fix:** the THAW plugin also sets the previous position (+0x78, between position +0x68 and matrix +0x88 in the engine's object layout; only if it looks like a position; logged before / after to confirm the guess), and the GTA side asks up to 3 times before falling back to moving CJ. **F8 itself worked** (CJ landed in Las Venturas, log); the jump back came from that fallback.
- **Barriers:** the rebuilt map left out 17 boxes, one of them wrongly (a Las Venturas car park deck, venetiancpark01: a road on top of a box), and some barriers were missed (the user hit walls on a bridge to the countryside). GTA's IDE files name them: models 4504-4527 in `leveldes\seabed.ide` (`cuntw_roadblock*ld`, `sfw_roadblock*ld`, `sfn_roadblockld`, `cn2_roadblock0*ld`, `CE_Makospan1ld`, `CE_Fredbar*ld`, `CE_Flintwat01ld`, `CE_Flintintld`, `sfse_roadblock1-5`), all drawn with the texture sets "roadblkx" / "barrierblk". **D1 replaced:** every placement of a model using one of those texture sets (and named "roadblock" or ending in "ld"; one sea bed piece shares the set) is left out, all its collision. Checked on the user's IDE files: exactly those 24 models.

**8.31 test 2 (user 19:13, logs):** no crash (skated to Las Venturas, F8 to SF, Gant Bridge and back); F8 fine; skate mode on at CJ worked all 3 times. The +0x78 guess is right: just before a placement it held the position itself (standing) or the position a moment earlier (moving). The rebuilt map left out exactly the 24 barrier models. Invisible walls: the stops and bounces in thaw.log all have real GTA collision at that spot (the Gant Bridge's side wall 1.2 m high, a post at its south end, Las Venturas building walls and pillars 10-20 m tall); the loops on the LV bridge were steering, not walls. So any walls left are GTA collision without a matching picture, or a picture without collision; which ones can't be told from positions alone. **Next:** a touchpad click on its own in skate mode logs "WALL MARK" with the spot, and the converter writes `placements.csv` (every GTA placement: model, map file, box) so a marked wall can be named. GPS: CJ standing 45 m from the nearest road re-routed every half second; fixed (his starting distance from the road is allowed until he reaches it).

**8.31 test 3 (user 19:41, 11 wall marks):** "the invisible walls only happen with skate mode on". At every mark the skater, on flat ground, reversed exactly (same speed, opposite direction) 1 to 5 m before **the end of the map's collision**: beyond it there is nothing at all, no wall. So the walls are **holes in our map**: THAW turns the skater back at the edge of nothing. A picture of the whole map's collision shows large holes inside the land (parts of Las Venturas, Red County, the desert, Flint). Example: at GTA (830, 690) GTA's map files place only a far-away stand-in (`lod_se_bit_20`); the real ground (`se_bit_20`) is placed in a "stream" map file inside gta3.img. **Likely cause, found in our reader:** a placement's "interior" number holds the area code in its low 8 bits and flags above (text map files use 256, 512, 1024, 2048, 4096); we required the whole number to be 0 or 13, so every flagged outdoor placement was dropped. **Fix:** only the low 8 bits count. **To confirm:** the converter now writes `skipped.csv` (every placement in the area it doesn't use, with the reason: interior, no model name, far-away stand-in, no collision found) so the remaining holes can be explained.

**8.31 fix 3 result (user 20:07):** "skated to Las Venturas and back without running into any invisible walls". Rebuilt map: 855 tiles, 2.58M triangles (was 798 / 2.25M: about 335,000 triangles of flagged outdoor ground were missing). `skipped.csv`: 6,772 placements left out, all as intended: 6,150 far-away stand-ins, the rest interiors (area codes 1-18) and 6 small interior props without collision. 8.31 done.

**Not now (noted):** water: in GTA the skater should splash into water, not ride the sea bed (THAW has no water here); later, from GTA's water level file.

**After the map is right (user wishes, 2026-10-10; each planned here before any code):** grind, stall and skitch every GTA car; reTHAWed's park editor to place skateable ramps, bowls and rails across San Andreas; reTHAWed's board whack hitting GTA peds like a melee weapon; shoot mode for skate and BMX.

### 8.32 Rails across the whole map (user 20:10: "how do we get around the rail cap"; written 20:20)

**Where the cap comes from:** the converter keeps at most 9,000 rail nodes (longest rails first) because a 19,476-node map hung while loading (8.12). That limit is very likely reTHAWed's memory pools, which reTHAWed reads from its own `rethawed.ini`, `[Memory]` section (8.21): `Out set RailPoint` 20,000, `Out set RailString` 5,000, `HashItem` 30,000, `CStruct` 100,000, `CComponent` 360,000, `CVector` 30,000 by default. Every rail node costs one of each kind of item in the level script (a struct of ~8 items with a position and angle vector) plus a rail point. The user's `rethawed.ini` has no `[Memory]` section yet. Editing it is the user's job (allowed: it's reTHAWed's own setting, no patching).

**Steps:**
- **R1 (cloud, done):** the converter now prints how many rails / nodes / km the whole map would have before the budget (`rails found:`), accepts `--rail-budget 0` (no limit), and prints the `[Memory]` values this map needs.
- **R2 (user):** build with `--rail-budget 0` once to see the total. Then, with the printed `[Memory]` values in `rethawed.ini`, try budgets upward (e.g. 20,000, 50,000, all), noting: loads? load time? frame rate? grinding works?
- **R3 (only if R2 hits a wall we can't raise: loading too slow, a fixed limit, or frame rate):** a **rail pool** that follows the skater: the map keeps a fixed number of rails (e.g. 9,000 nodes) and our THAW plugin moves them, in reTHAWed's memory, onto the GTA rails near the skater (from a full rail list read at start). Needs finding reTHAWed's rail list in memory first (read only, like the bike skeleton search: look for our known rail node positions). Research step; not started.
- **R1 result (user 20:19):** the whole map has **101,404 rails, 245,951 rail nodes, 1,508 km** (plus 21 hand-made). Needs under `[Memory]`: RailPoint 266,032, RailString 106,425, HashItem 286,032, CStruct 346,032, CComponent 2,820,320, CVector 522,064 (27x today's 9,000). Average 2.4 nodes per rail: many are short. **R2 plan:** the user sets those `[Memory]` values once (memory cost estimated well under 100 MB; if reTHAWed won't start, halve them), then loads maps built with `--rail-budget` 30,000, 60,000, 120,000 and 0 (all), stopping at the first that hangs or loads too slowly.
- **R2 test 1 (user 20:38): 30,000 nodes "froze".** reTHAWed's debug.txt: the level PAK loaded completely (collision 77.8 MB, level script 6.2 MB), `zone_init: mpgtalse` ran, then nothing; our thaw.log: reTHAWed's game loop stopped producing frames; the user closed it after 30 s (no "out of" pool message). So not a pool running out: something after loading takes very long with many rail nodes. Likely the game linking its rails (e.g. every rail end checked against every other rail node: 30,000 nodes = 11x the work of 9,000 if it grows with the square). **Next:** load the same 30,000 map again and wait up to 5 minutes: if it finishes, the time tells how it grows (and whether R3, a rail pool near the skater, is needed for the whole map: 246,000 nodes would be ~70x slower again).
- **Decision (user 20:40): go to R3, the rail pool**, as long as every rail can be ground. Plan:
  - **R3a, research (read only, coded 20:55):** the map tool writes `rails_in_map.csv` (every rail node's exact position as stored in the level). Our THAW plugin, 15 s after our map has loaded, searches reTHAWed's memory (own thread) for 6 of those positions (rail 0's first three nodes, the first node of rails 1 and 2, the last node), logs every place each is found with the bytes around it, and every pointer that points just before or into the first two. From that: reTHAWed's rail node layout (where position, links to next / previous node, flags and bounding box sit), the list that holds them, and whether a lookup grid exists. Nothing is written.
  - **R3a result (user 20:49, thaw.log):** reTHAWed keeps **one 0x60-byte record per rail node**: +0x00 box min and +0x10 box max (of the piece to the next node), +0x20 position (each with w = 1.0), +0x30..+0x3F and +0x4C packed bits (patterns like 0x24924924 / 0x9249: every third bit, which looks like interleaved x / y / z bits, i.e. spatial sort keys), +0x40 = 4, +0x44 = the node's name checksum (checked: `MPGTALSE_Rail0_0` = 0xBAE7797C), +0x48 = 0x2A, +0x50 = next node's record, +0x54 = previous node's record. The records lie in one block (about 0x3625xxxx-0x3633xxxx for 9,000 nodes) but **not in node order**: probably sorted by those spatial keys, which would mean a sorted lookup that moved rails must keep valid. Only the neighbour records point at a record (no per-node pointer list). Other copies of the positions are the level script's own data (not used for grinding).
  - **R3a-2 (coded 21:10, read only):** find the block's start and length, save it to `logs\rails_array.bin` (to work out the packed bits from all 9,000 records against their known positions), and log what points at the block (the rail manager and its count).
  - **R3a-2 result (user 20:55, thaw.log + rails_array.bin, decoded in the cloud against rails_in_map.csv):** the block is 0x362307C0..0x36303660 = **8,999 records = every rail node in the map**, each matched to its node by position. All of the layout is now known, and it is simpler than feared:
    - the block is **sorted by the name checksum** (+0x44, strictly rising), not by place: it is a lookup by name, so moving a rail does not disturb the order;
    - box min / max = exactly the min / max of the node's position and its next node's position (the last node of a rail: both = its own position);
    - next / previous links: all 17,334 correct (same rail, point +1 / -1), 0 at the 332 rails' ends;
    - +0x40 always 4, +0x48 always 0x2A; +0x30..+0x3F, +0x4C, +0x58, +0x5C are left-over bytes (thousands of different values with no pattern: whatever was in memory before; the "every third bit" patterns are just common left-overs);
    - one object (0xE8 bytes) holds {block, count 8,999, count 8,999}; nothing else points at the block; no sign of a separate lookup grid.
    So **moving a rail = writing new positions and boxes** into its records (and, for the pool, new next / previous links). Not known yet: whether reTHAWed caches anything about rails elsewhere (e.g. a grid built at load) that would keep a moved rail from being found; the test below answers that.
  - **R3b test (coded 21:20):** F5 in our level moves one far-away 2-node rail (its 2 records: positions + boxes, nothing else) to start 150 units (~4 m) in front of the skater, 400 units (~10 m) long, along the way he is moving (or facing, when slow), 12 units (~30 cm) above his feet. Then for 20 s, every 0.1 s, the log says how far the skater is from it, how far above it, and how far along it. Grinding it shows as ~0 distance with "% along" rising. F5 again moves it again. The records are found again after every level load and forgotten as soon as the level goes (never write to freed memory). Written from our plugin's thread right after a reTHAWed frame (not the game thread): fine for one test rail; the pool (R3c) may need the game thread.
  - **R3b test 1 result (user 21:07: "I was able to grind with f5"; thaw.log):** not shown yet. F5 worked twice (rail moved, no problem, no crash), but both times the skater rolled straight under the test rail on the ground (11-12 units below it, ~2 units to the side) and then ground a **different, real map rail** about 150 units (~4 m) to the side, parallel to it (constant speed 920 / 830, lasting well past the test rail's end). The test rail can't be seen, which makes it easy to miss. So whether a moved rail can be ground is still open.
  - **R3b test 2 (coded 21:30):** the test rail is now **shown in GTA**: a row of 13 orange lights (GTA coronas) along it while skate mode is on (the THAW plugin puts its ends in the link, version 13; GTA's `testrail.cpp` maps them with the skater-to-CJ mapping). It is also longer and higher (6 m ahead, 25 m long, 50 cm up, above kerb height), and the log marks every sample with the skater on it ("ON THE TEST RAIL": within ~25 cm sideways, not below, part way along, moving) and sums up after 20 s ("... = GROUND IT").
  - **R3b test 2 result (user 2026-10-12 10:43, thaw.log + gta.log):** **passed.** The orange lights showed in GTA ("test rail: showing it in GTA ..." / "test rail: hidden" with skate mode off); F5 moved the rail twice (no crash); the log marks "ON THE TEST RAIL" 10 of 182 samples in test 1 = "GROUND IT" (0.0 from the rail, 0.0 above it, 4% -> 86% along it, speed 842-934) and 10 more samples in test 2 (27% -> 93%, speed 857; its 20 s summary line was not written: the game closed first). So a **moved** rail is fully grindable — reTHAWed keeps no hidden note of where rails are that would block it. **The rail pool (R3b/R3c) can go ahead.** (The tester noted the test rail is placed from the skater's heading and does not snap to GTA ledges; that is by design: the pool is what puts rails on real ledges.)

  - **R3b/R3c plan (written 2026-10-12, for the user's review before coding):**
    - **Goal:** every kerb / ledge / sidewalk edge in the whole of San Andreas is grindable, without the level holding all 245,951 nodes at once (30,000 froze it; the whole-map figure would be far worse).
    - **What makes it easy:** rails average **2.4 nodes** each, so nearly all of them are a single straight 2-node piece. A pool of 2-node rails therefore covers almost every GTA rail 1:1, with no splitting.
    - **The pool:** the level keeps a fixed pool of about **4,500 rails x 2 nodes = 9,000 nodes** (the size that loads quickly), parked far below the map at load, so none of them can be ground until the plugin moves them into place. Check the `[Memory]` numbers the tool prints: `Out set RailString` must be at least the pool's rail count (default 5,000 is close, so raise it in `rethawed.ini`).
    - **The full rail list:** the map tool writes the whole map's rails (all 101,404 / 245,951 nodes, exact THAW positions) to a file beside the map for the plugin to read at start — the same idea as today's `rails_in_map.csv`, but for the whole map. This is only read by us; it costs reTHAWed nothing.
    - **New tool mode:** the pool map is built with a new option that emits N two-node rails parked below the map (not the "longest rails first" set used for playable maps), so each pool rail is a uniform, interchangeable slot.
    - **R3c — filling the pool (the plugin, on reTHAWed's game thread):** only when the skater has moved about 40 m since the last fill (not every tick): take the GTA rails within about 150 m of the skater whose whole span is one 2-node piece, copy their two positions (and the box from node to node) into free pool rails, and park the rest below the map again. Longer local rails (3+ nodes): split into consecutive two-node pieces that **share their endpoints**, so the whole chain is placed and a grind should carry across the seams the way a Tony Hawk rail does (2026-10-12; being tested). The finder also now allows ledge tops up to `RAIL_DROP` = 5 m (was 1.6 m) so a wallride-top ledge gets a rail.
    - **Never disturb a grind:** never touch the rail the skater is on or about to land on — skip the rail he is grinding and any within about 10 m ahead; if simpler, only re-fill while he is airborne or standing still.
    - **What to watch:** the rewrite is a burst of writes every ~40 m of travel, so watch for a hitch when it happens; the parked rails must be far enough that their grind checks cost nothing; moving does not disturb the record block's name-checksum order (R3a-2), so the lookup stays valid.
    - **Rail sources (user 2026-10-12: "we want to be able to grind bridges, power lines, railroad tracks etc."):** the pool does not care where a rail came from — it only needs every rail in the full list. So the tool's rail finder must grow past collision edges into these tiers:
      - **A. Collision edges (today):** kerbs, ledges, sidewalk edges, bowl coping — derived from GTA's collision shapes.
      - **B. Thin vertical collision:** railings, fences and barrier tops. The 8.31 bridge/barrier rule removes the blank collision walls that cross roads — it must keep their grindable **railings and edges**, and the hand-made `gta_extras.py` list covers the known ones (the East LS skatepark wall/fence already work).
      - **C. Power / telephone wires and bridge suspension cables:** these are visual models, not collision, so they cannot come from the collision finder. **Deferred (user 2026-10-12).** Finders: the map has only **8 wire models** (`ce_telewires01-04`, `cemulwire01-03`, `tcemulwire01`), and their geometry is stored **quantized/compressed** in the DFF (102 verts pack into small ints), so reading the wire curves is real work for little rail. The pragmatic alternative (to revisit) is to **connect nearby pylon/pole tops**: `pylon_big1_` (89 pylons, top ~53 m) and `telgrphpole02`/`telgrphpoleall` (1,534 poles), all of which we have with boxes. Not done.
      - **D. Railroad tracks (done 2026-10-12):** GTA ships its tracks as text node lists `data\Paths\tracks*.dat` (a count, then `x y z flag` lines). Each file turns out to be **one smooth polyline** — no turn in any file is over 45 deg, and the long jumps (up to 256 m) are *straight* stretches the game samples sparsely, not branches (an early version wrongly split on those and gave short grinds, user 2026-10-12). So each file becomes **one rail**, emitted **twice** — onto the two steel rails, offset by half the 1.435 m gauge — for **8 rails / 3,898 nodes** total. No DFF work.
      - **E. Hand-made:** the `gta_extras.py` list, for anything the automatic finders miss.
      - **F. Natas spins — ESSENTIAL but BLOCKED (tried 2026-10-12).** In THAW a Natas spin is done by ollieing onto a vertical pole/narrow object and holding **Flip while pressing Grind** (user). It is a **THAW-object** mechanic: the park piece called `pole` is spinnable by nature. Our map is GTA collision turned into THAW collision/rail nodes — it has **no THAW objects**, which is what Natas keys off. Ruled out by test: (a) a **degenerate rail** (F4 probe, ~10 cm) — gives an ordinary "bonk grind", no spin; (b) the rail's **TerrainType** set to `TERRAIN_METALPOLE` (from the THPS key list) on every rail node — no spin; (c) a **`Spinnable` flag** added to every rail node (that string exists in rthw.dll beside the other object properties) — no spin. The park editor has no per-object Natas toggle, and a park save is a fixed-size, mostly-zero binary with hashed fields (`reTHAWed\save\*-Park`, `data\customparks\*.prk`), so the marker can't be read off cheaply. **The real requirement is a new capability: give the map actual THAW objects** (a real `pole` model + placement carrying the spinnable property), i.e. object/scene nodes that reference THAW models, not just collision. Project-sized; not started. The tool groundwork (`--rail-terrain`, `--rail-flag`) and the F4 point-rail probe stay for when this is picked up.
      - **G. Building and structure tops (user 2026-10-12):** grind the roof edges of buildings, wall tops and bridge structures, the way a Tony Hawk map does. The finder's only blocker was its drop limit (`RAIL_DROP` upper bound, 1.6 m, raised to 5 m on 2026-10-12), which keeps rails off rooftops; the new map-tool option **`--rail-drop-max 0`** (no limit) includes every edge whose drop is at least 0.08 m, so building tops get rails. Watch the pool's local rail count: a dense downtown may want more than the pool's 4,500 slots (the plugin places the nearest, so the near area is still covered). **Verified from the data (2026-10-12):** the San Fierro suspension bridge (`baybridge1-4_sfse`, GTA x -1678..-1120, y 534..1118) has 2,937 rail nodes above 25 m, 326 above 120 m, and tower-top rails at 147 m (towers at GTA -1255,953 and -1545,699); the bridge's north end (`ggbridgeend_sfn`) has 66 rails. **The Gant Bridge** (the red suspension bridge, models `ggbrig_01-05_sfw`, GTA x -2710..-2654, y 1598..2149; the user's point -2681,2042) has 2,099 rail nodes above 20 m, 983 above 60 m and tower-top rails at ~225 m (towers near GTA -2664,1594 and -2702,1935). Still not covered: the suspension **cables** (visual models, no collision) — that is source C. **Verified 2026-10-12:** all 61 GTA bridge models present in the map carry rails (7 to ~700 nodes each, decks up to 147 m), so bridges work generally, not just the Gant.
    - **Bridges specifically:** the 8.31 fix removed the invisible crossing barriers; verify it kept the bridge **deck edges, railings and cables** as rails — that is the point of "grind the bridge".
    - **Steps:** (0) rail sources — grow the finder with tiers B–G above (research the no-collision wires/cables, the train paths, and the Natas marker first, one tier at a time); (1) R3b tool — full rail list + the pool map mode; (2) R3c-1, log only — the plugin finds the pool records by name checksum, parks and reports the pool; (3) R3c-2 — copy local 2-node rails in and out as the skater moves (and, for F, one-node rails); (4) R3d test — grind kerbs, ledges, **bridges, power lines, railroad tracks, building tops and Natas points** anywhere; long runs across a re-fill; no stutter from the updates.
- **Quality, alongside:** rails are now guessed from shapes (kerb / ledge edges). Once many more fit, the guesses that feel wrong (rails on roof edges, in the middle of slopes) will show; tune the finder then.

### 8.33 GTA cars: grind, stall and skitch (user 2026-10-12; on the wish list since 2026-10-10)

**Asked:** in skate mode, be able to (a) **grind** a GTA car (a rail along its roof/edges) and (b) **skitch** it (hold on and be towed); "stall" alongside.

**What reTHAWed has:** skitching is a reTHAWed/THAW mechanic — rthw.dll has `RT_ToggleSkitching`. But **our level has no vehicles of its own**: the cars are GTA's, and reTHAWed only knows the converted GTA collision. So both features must be driven by our **GTA plugin**, with reTHAWed's skater moved to follow (the same idea as BMX mode, where the GTA bike follows reTHAWed's rider).

**Plan (one step at a time; each tested before the next):**
- **C1, research (log only, GTA side):** in skate mode, log every GTA car within ~40 m of CJ a few times a second: pool index, model id, position, velocity, whether it has a driver, and the distance. Read only. Tells us what is around, how big the roofs are, and what a skitch would grab.
- **C2, grind a car:** the pool already moves rails, so add a few **"car rails"** rewritten every frame from a nearby car's matrix — two rails along the roof's long edges at roof height. It is a rail that moves under the skater, so start with a **stopped/slow** car and decide afterwards about moving ones. Never touch the rail the skater is on (as with the pool).
- **C3, skitch:** when CJ/the skater is behind a moving car and the player asks (a held button), drive the skater to follow the car (position behind its rear, velocity matched) and show CJ holding on. First try reTHAWed's own skitch: if `RT_ToggleSkitching` on makes reTHAWed skitch a moving object, we may be able to feed it the car's proxy rather than faking the tow ourselves.
- **C4, stall:** decide what "stall" means here (reTHAWed's stall is a trick on a ramp lip) and whether cars can host it; likely last.

**Risks:** a rail moving under the skater while he grinds it (we write from our plugin thread); the car driving away mid-grind; CJ's own collision with the car while he is supposed to be holding on.

**State so far (2026-10-12, committed):** the car scan (C1) works; parks/slow cars give roof rails and **that grind works**; buildings/rails unaffected. **A moving rail cannot be ground** — reTHAWed's grind maths goes to NaN and takes GTA's screen with it, so car rails are stopped/slow only, and a reserved car rail near the skater is never moved (that teleport was the trigger). The velocity tow (C3) does not save a moving grind. The solid push-out (C5) works against the car's collision box, but a **moving** car still drives through CJ (his position is re-imposed every frame). The car-hit bail fires but is unreliable. **Next:** skitch (a grab / tow on a moving car, no rail).

**Order:** C1 (data) → C2 (grind) and C3 (skitch); C4 later.

### 8.34 GUI launcher (user 2026-10-12; plan)

**Goal.** One window, one button, for people who are not us. Today's launcher is a
black console window that starts reTHAWed and loads our THAW plugin into it; GTA is
started by hand. An end user should instead get a small window with a **Play** button,
a few **status lines**, and one or two options.

**What Play does** (user's choice, 2026-10-12 — "both games, one click"):

1. check the setup and show each result as a green OK or a red reason;
2. start reTHAWed (paused, the sound fix, the controller environment) and load
   `SkateAndreas-thaw.dll` into it — exactly what the console launcher does now;
3. start GTA San Andreas (`gta_sa.exe` in its own folder);
4. stay open and show what is happening, then once both games have closed, be
   ready to Play again.

**On the window** (user's choice):

- **Start reTHAWed hidden** — a tick box, so only GTA is on screen.
- **Open logs folder** — a button, so a user can find the logs to report a problem.
- **Status lines** — for reTHAWed (present + the expected 5.0.0.1 build),
  `SkateAndreas-thaw.dll` (next to the launcher), `SkateAndreas.asi` (in GTA's
  modloader folder), and `gta_sa.exe`. Red lines say what is missing.

**How it is built (plain language).** A plain Windows window using the controls that
are part of Windows itself (buttons, text, a tick box) — no new libraries, nothing to
install, the same 32-bit MSVC build as the rest of the project. The slow work
(starting a game and waiting for it) runs on a **background thread** and sends short
notes back to the window, so the window never freezes. Everything is still written to
`logs\launcher.log`, so a problem can be read afterwards even though the window is
gone. A hidden `--selftest` switch does the checks and writes the log **without**
starting anything, so the launcher can be tested without opening a game.

**New build setting:** `GTA_DIR` (the folder holding `gta_sa.exe`), default
`D:/Games/GTA San Andreas`. Read only; nothing is written into either game install
(AGENTS.md rules 1 and 4).

**The game folders can be pointed out by hand (added 2026-10-12, user's ask).** The
window shows a **Game folders** row for each game with a **Browse...** button, and the
choices are remembered next to the launcher in **`SkateAndreas.ini`**
(`[Paths] GtaDir=` / `RethawedDir=`). The setup checks and Play both use them. With no
ini, the paths the build was made with are used. This is what makes the launcher
usable on someone else's PC without a rebuild, and it is where the controller-mode
option (section 11) will live.

**Not doing yet:** no auto-update, no profiles, no graphics options. The controller
mode (section 11) is a later tick box on this window.

### 8.35 Start in our map on Play (free skate, a silent skater) — plan (user 2026-10-12)

**Wanted.** When the player hits Play, reTHAWed should end up in **free skate, on our
map**, as a **created ("New Skater") skater**. The user's reason for the skater
specifically: a created skater has **no voice lines**, so a story character's lines
cannot play over GTA (reTHAWed's sound effects are audible behind GTA on purpose,
section 8.27).

**What reTHAWed offers.** Nothing to switch on: `rethawed.ini` has no "start in level"
key, and neither `reTHAWed.exe` nor `rthw.dll` has a command-line flag for it (both
searched 2026-10-12). What reTHAWed does expose are the game's own **script
functions** — `LaunchLevel`, `RestartLevel`, `LaunchGame`, `CareerStartLevel`,
`RT_CreateLevelLoadScript` — and we already run reTHAWed's scripts from our plugin on
its game thread (that is how BMX mode drives the bike toggle, section 8.29). So this
is reachable by the same road.

**The lead.** Our level's ID, `0x9F014B98`, *is* the checksum of the name
**`load_MPGTALSE`** — reTHAWed's own "load this level" script for a custom level
(`src/common/convert.h`). So the first thing to try is running that script from
reTHAWed's game thread while it sits at the front end, and watching for reTHAWed's own
`Started Level Prefix: %s` line.

**A rule that shapes it.** Nothing may be written into `D:\Games\reTHAWed`
(AGENTS.md rule 4), so we cannot edit its save or its ini — this has to be done **in
memory**, from our plugin. (Its own files are reTHAWed's business, not ours.)

**Plan, one step at a time:**

1. *(research, needs one game run)* From reTHAWed's front end, run `0x9F014B98`
   through the existing RunScript call (as `src/thaw/bmx.cpp` does) and watch
   `thaw.log` and reTHAWed's `debug.txt` for `Started Level Prefix: MPGTALSE`.
   If the level loads, this step is done.
2. *(research)* Find how the front end picks the skater, and whether "free skate" is
   a separate step from loading the level.
3. *(build)* Our THAW plugin does it once, shortly after reTHAWed reaches the front
   end: the level, then the skater, logging every step. If a step cannot be
   understood after two real attempts, stop and write it up in `STATUS.md`
   (AGENTS.md).

**Risks.** This is the fragile kind of work — driving the game's front end. The
scripts may need a particular game state, and a wrong call could leave reTHAWed
sitting in a menu it will not leave. **Fallback if it will not work:** the player
loads the level by hand once per session (the pad is reTHAWed's until skate mode
starts), or we **mute reTHAWed's skater voice lines** instead of changing the skater.

**Result (2026-10-12): the two roads we tried, and why this is a dead end.**

1. **Run reTHAWed's own level script from the front end.** The map *does* load — reTHAWed's
   `debug.txt` says `LOADING CUSTOM LEVEL...` / `Loading career level MPGTALSE...` — but the
   game's "current level" field (`0x008A3B5C + 0x20`, what `ReadGameState` reads) **keeps
   pointing at the front end's own level**: its value is exactly `qbkey("load_z_mainmenu")`.
   So the plugin cannot recognise the level: skate mode answers "reTHAWed not in a level" and
   CJ floats (the alignment is never applied). Running that script is **not** the same as the
   front end's free-skate flow. *(A comparison run settled it: the manual free-skate load
   gives level `0x9F014B98` and `skate mode: ON ... ABSOLUTE (level lined up with GTA)`.)*

2. **Replay the player's own pad presses.** The sequence was captured from their manual run
   (`Down ×3, Cross ×3, Right ×3, Down ×2, Cross held`) and played back through the input
   hook. This cannot work, and the reason is the one worth remembering: **reTHAWed's front end
   only polls the pad once the player touches it** — in a 69 s run the input hook ran for
   **2 s**, and only after a real press. Our synthetic presses go in through that same hook,
   so the replay can never start itself (chicken and egg). The front end also reads no input
   at all while GTA is the front window.

**Decision (user 2026-10-12): stop here.** The manual free-skate load works and stays. The
real complaint behind the request — a story character's voice lines playing over CJ — is
better solved **directly, by muting reTHAWed's skater voice lines**, which needs none of this.
The experiment's code (`src/thaw/autostart.cpp`, the recorder and the replay) is left in place
but switched **off** (`kReplayFrontEnd`, `kRecordFrontEnd`), documented here so nobody repeats it.

**Not doing:** writing to reTHAWed's files, or changing reTHAWed itself.

### 8.40 The release carries its own Python, and a licence (user 2026-10-13)

**Asked:** "players will need Python installed?" — and, if the download can make it easier, bundle it.

**What was done.** The release package includes the **Windows embeddable Python** (3.11.9, amd64,
~11 MB) under `tools\python\`. `FindPython` in the launcher tries, in order: the bundled one
(`<home>\tools\python\python.exe`), then `python`, `python3` and `py -3` from PATH — so a machine
where the bundled interpreter cannot run (a **32-bit Windows**) still works with an installed
Python, and vice versa. Python's own `LICENSE.txt` ships inside it, as the PSF licence requires.
**Result: a player installs nothing.**

**The package is built by a script, `tools\make-release.ps1`**, so it is reproducible rather than a
hand-assembled folder: it copies the three built binaries, the map tools, `INSTALL.md`, `README.md`
and `LICENSE`, downloads and unpacks the embeddable Python, and zips the lot to
`release\SkateAndreas.zip` (~11 MB). `release\` is covered by the `.gitignore` whitelist, so the
package can never be committed by accident.

**Why not ship the map instead:** it is GTA's own collision, converted — redistributing that is
what AGENTS.md rule 1 forbids (see 8.39). Bundling a runtime keeps the install a single download
without that.

**Licence:** the project gained an **MIT `LICENSE`** so the code can be reused freely (compatible
with the zlib-licensed plugin-sdk it builds against). The copyright line reads "SkateAndreas
contributors" — put a real name or handle there if you want one.

**Tested (2026-10-13):** the bundled Python runs the map tool on its own —
`release\SkateAndreas\tools\python\python.exe release\SkateAndreas\tools\gta_to_thaw_map.py --help`
→ exit 0, so the sibling imports resolve too; the **packaged launcher run from a folder outside the
project** picks its *own* bundled Python and resolves home/logs/extracted to that folder
(`--where`); the dev launcher still picks the system `python` and the project folder. `--where`
now prints the Python it chose, so this is checkable at any time.

### 8.39 "Build the map..." in the launcher (user 2026-10-12)

**Asked:** a way to make the map without a command prompt.

**Why the map cannot simply be shipped.** It is **GTA's own collision, converted** —
`tools/gta_to_thaw_map.py` reads the player's installation. Shipping it would redistribute
Rockstar's data, which AGENTS.md rule 1 forbids and which is what mod takedowns are made of. The
two `_railpool` csv files are the same (rails found in GTA's geometry). So the player builds it
from their own copy — this button just takes the command prompt out of the way.

**What the button does:** finds Python 3 (`python`, `python3` or `py -3`), runs the tool in its own
console window so the player can watch the progress, waits, then opens `extracted\thaw_maps\` and
says where to copy the map folder. The arguments are the whole-map pool build:
`--box -3000 -3000 3000 3000 --no-geometry-nodes --rail-pool 4500 --rail-drop-max 0`.

**A fifth setup check**, "SkateAndreas map built", looks for
`<home>\extracted\thaw_maps\_railpool\rails_in_map.csv` and goes red with "press Build the map"
when it is missing — so an install that has not built its map explains itself, instead of the
player later finding that nothing grinds.

**Deliberately not done:** the launcher does **not** copy the map into reTHAWed (nothing of ours
writes inside the reTHAWed install — AGENTS.md rule 4), so that stays a drag-and-drop. Python is no
longer a requirement: the release carries its own (**8.40**).

**Tested (2026-10-12):** `--selftest` shows the fifth check green on a machine where the map is
already built, and `python --version` resolves (3.11.0) — which is exactly what the probe tests.
**The button's own run is not tested**: it would rebuild the ~80 MB map and rewrite the rail pool,
so it is the user's to press.

### 8.38 Where our files live, so a downloaded copy works (user 2026-10-12)

**The problem.** The folder the project was compiled in was baked into the plugins
(`MP_LOG_DIR`). That is fine on the machine that built it, but a copy someone downloads would
look for `D:\Mods\my-passthrough\...`: the logs would land in a folder that does not exist, and
**the rail pool could not open its rail list, so nothing would be grindable** — the one thing
that has to work. Found while writing `INSTALL.md`.

**The fix.** The launcher sets **`SKATEANDREAS_DIR`** to the mod's home folder before starting
either game, so both inherit it. The plugins (`src/common/paths.h`) then use:
- **logs** → `<home>\logs`
- **the map tool's output** → `<home>\extracted\...` (the rail pool's two csv files)

With no launcher (a build started by hand, or one of our tools) they fall back to the compiled
folder, so **nothing changes on the machine that built it**.

The home is the launcher's own folder, **except** when the launcher is running from a build
output folder inside this project (`..\..\logs` exists, which is how the dev layout looks) — then
the project folder itself is the home, so our logs, `extracted\` and habits all stay put.

The map tool already treats "the folder containing `tools\`" as home, so an install needs no
`--out`: keep `SkateAndreas-launcher.exe`, `SkateAndreas-thaw.dll` and `tools\` together and
everything lines up.

**Tested (2026-10-12):** `--where` (`SkateAndreas-launcher.exe --where`, a small switch that
writes the resolved folders to `launcher.log` and exits) run two ways: from `build\Release` it
reports home = the project, logs = `<project>\logs`, extracted = `<project>\extracted`; and from
a copy in another folder it reports home/logs/extracted all under that copy, and writes its log
there. The four setup checks still pass. **Not tested end to end on a second machine** (it would
need the games) — the plugins use the same header and the same rule.

### 8.37 On-screen hints (user 2026-10-12)

**Asked:** the standard San Andreas black and white message box, saying something like
"Press L3+R3 to Skate", and on a BMX "Press L3+R3 to toggle BMX mode".

**How:** GTA's own help box — `CHud::SetHelpMessage`, the same black box its own tips
use — so it looks native by construction. It is shown when **no mode is on**: on foot
`Press L3+R3 to Skate`, on a BMX `Press L3+R3 to toggle BMX mode`. It stays quiet while
GTA is showing a message of its own (`CHud::HelpMessageDisplayed()`), shows nothing
while a car is being driven, and waits ~10 s after the plugin loads so the game is up
first. **The skate hint appears once per run of the game** (the player has to load free
skate by hand anyway, so once is enough); the **BMX hint repeats at most once a
minute** while CJ is on a bike with no mode on.

**Where:** `src/gta/hint.h` / `.cpp`, called each world update from `src/gta/main.cpp`.
The "is CJ on a BMX" test is exported from BMX mode (`BmxCjRidesBmx`) so there is one
copy of it, not two.

**Tested (user 2026-10-12):** both messages show as intended.

### 8.24 Wish list: reTHAWed's slow motion ("Focus") look in GTA (user 19:04, after the H2 test)

**Asked:** the slow motion already works in GTA (CJ follows the skater, who moves slower), but it should also look like reTHAWed's: its screen effect. Not planned yet. To find first: how reTHAWed knows Focus is on (a flag or time scale we can read), and what its effect draws (probably a full-screen pass near the end of the frame; the H1 frame log taken during Focus will show it). Then choose: copy the effect's look in GTA with our own full-screen pass, or catch reTHAWed's. Sound (if Focus changes it) to check too.

**First look (8 frame logs, 19:12, some in special mode):** every frame has the same targets and the same glow passes at the end; the busier frames only draw more 3D world. So the special-mode look is not an extra pass. Likely the same glow passes with other values (shader constants, e.g. tint or glow strength), which the frame log doesn't record. Next research step: also log `SetPixelShaderConstantF` during the glow passes, and log whether special / Focus is on, then compare a normal and a special frame.

## 11. Before release (to do later)

- **Done (2026-10-12): the folders are no longer baked in.** The launcher names the mod's home
  (`SKATEANDREAS_DIR`) and both plugins keep their logs and find the map tool's output under it,
  so a downloaded copy works wherever it is unzipped — see **8.38**. *(This was the #1 release
  blocker: the rail pool could not open its rail list on anyone else's PC, so nothing was
  grindable.)*

- **There is nothing to download yet.** The repository is source only (`.gitignore` is a whitelist,
  AGENTS.md rule 1 keeps game data out), so a GitHub visitor has to build it with Visual Studio and
  plugin-sdk. Publish a **release** containing `SkateAndreas.asi`, `SkateAndreas-thaw.dll`,
  `SkateAndreas-launcher.exe`, `tools\gta_to_thaw_map.py` and `INSTALL.md`. The map itself cannot be
  shipped — it is made from the player's own GTA (INSTALL.md step 3). `INSTALL.md` was written
  2026-10-12 for those players.

- **Remove the temporary controller shortcuts** (PS5 touchpad + D-pad Down = frame recorder, touchpad + D-pad Up = draw log, touchpad + D-pad Left = HUD catch, touchpad + D-pad Right = lean drawing (keep lean drawing itself on); `src/thaw/touchpad.*`, link `reserved[0]`; added 2026-10-09).

- **Controller fix must become an option** (asked by the user 2026-10-07). Right now the launcher always tells reTHAWed to ignore Xbox 360 controllers (045E:028E) and turns off SDL's raw input reading. That is right for a PS5 controller + DualSenseX, but a player with a **real Xbox 360 controller** would have no controller in reTHAWed at all. Plan: a settings file next to the launcher (for example `SkateAndreas.ini`, `[Controller] Mode=`), with at least:
  - `ps-with-dualsensex`: today's behaviour (reTHAWed uses the real PlayStation controller, ignores the virtual Xbox 360 one).
  - `xbox`: no controller changes at all; reTHAWed and GTA both use the Xbox controller directly.
  - Possibly `auto`: pick one by checking which controllers are plugged in. Not designed yet.
  Xbox One / Series controllers report different IDs than the Xbox 360, so the ignore list doesn't hide them, but raw input off still changes how SDL reads them. Not tested with any real Xbox controller.
