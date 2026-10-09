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

- **GTA:** our plugin is an `.asi` file. The ASI Loader 1.3 and Mod Loader are already installed. The plugin lives in **`D:\Games\GTA San Andreas\modloader\my-passthrough\`** (approved by the user 2026-10-07; recorded in `AGENTS.md`). Only our files go there; deleting that folder removes the mod.
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

1. **`my-passthrough-thaw.dll`, the THAW plugin.** Like the first GTA plugin: when loaded, it starts its own thread and writes `logs\thaw.log`:
   - "loaded", the game exe path and size (expect `reTHAWed.exe`, 3,612,672 bytes),
   - the list of program files loaded in the game at that moment (to confirm `rthw.dll` is there, i.e. reTHAWed is active),
   - a heartbeat once per second.
   No plugin-sdk (it's GTA-only). No hooks.
2. **`my-passthrough-launcher.exe`, the launcher.**
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

**Test procedure (both steps):** user runs `build\Release\my-passthrough-launcher.exe` (from the project folder), plays reTHAWed's menus for ~30 s, quits normally. Claude reads `logs\launcher.log`, `logs\thaw.log` and reTHAWed's `debug.txt` (read-only).

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

- Name: `Local\MyPassthrough_v1` (`Local` = only programs in the user's own Windows session can see it; nothing on the network).
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
| 2-1 | **Link v2: reTHAWed publishes the skater.** Shared block grows to version 2 (`Local\MyPassthrough_v2`): skater position, velocity, in-level, paused, level ID, sample time, protected by a sequence counter so GTA never reads a half-written set. THAW plugin publishes about every 15 ms (logs still 10/s). GTA plugin reads it and logs it once per second. Plus `convert.h` and its unit test. | No | `gta.log` shows the same skater numbers as `thaw.log`; unit test passes (**passes in the cloud: 20/20**). **Done 2026-10-07:** GTA gets 93 to 96 samples/s, 0 to 16 ms old, values on reTHAWed's path, states and pause on/off arrive correctly |
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
- link version 4 (`Local\MyPassthrough_v4`, 212 bytes): the THAW plugin reads facing and board every pass (about 65 times a second) and sends the facing (3 rows) and the board bone (4 rows) with each skater sample, flag `kPoseValid`.
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

## 11. Before release (to do later)

- **Controller fix must become an option** (asked by the user 2026-10-07). Right now the launcher always tells reTHAWed to ignore Xbox 360 controllers (045E:028E) and turns off SDL's raw input reading. That is right for a PS5 controller + DualSenseX, but a player with a **real Xbox 360 controller** would have no controller in reTHAWed at all. Plan: a settings file next to the launcher (for example `my-passthrough.ini`, `[Controller] Mode=`), with at least:
  - `ps-with-dualsensex`: today's behaviour (reTHAWed uses the real PlayStation controller, ignores the virtual Xbox 360 one).
  - `xbox`: no controller changes at all; reTHAWed and GTA both use the Xbox controller directly.
  - Possibly `auto`: pick one by checking which controllers are plugged in. Not designed yet.
  Xbox One / Series controllers report different IDs than the Xbox 360, so the ignore list doesn't hide them, but raw input off still changes how SDL reads them. Not tested with any real Xbox controller.
