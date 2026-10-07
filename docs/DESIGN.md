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
| 2-2 | *(written 2026-10-07 in `src/gta/skatemode.cpp`. **First test 2026-10-07:** CJ follows 1:1, toggles work; left/right was mirrored (now ySign −1) and GTA's pause menu, opened by the shared controller, turned skate mode off (now holds). Fix 1 not yet retested. L3 + R3 is not hidden from GTA)* **Skate mode moves CJ.** F7 or L3 + R3 (only while GTA is the front window) toggles it. While on, after each GTA world update: CJ placed at the mapped position (plus velocity × age of the sample, for smoothness), speed zeroed, heading set, our controls-off flag set. | **Yes: CJ's position, heading, speed, one control flag** | Logs show CJ's position matches the target each second; user sees CJ move as the skater moves |

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

## 11. Before release (to do later)

- **Controller fix must become an option** (asked by the user 2026-10-07). Right now the launcher always tells reTHAWed to ignore Xbox 360 controllers (045E:028E) and turns off SDL's raw input reading. That is right for a PS5 controller + DualSenseX, but a player with a **real Xbox 360 controller** would have no controller in reTHAWed at all. Plan: a settings file next to the launcher (for example `my-passthrough.ini`, `[Controller] Mode=`), with at least:
  - `ps-with-dualsensex`: today's behaviour (reTHAWed uses the real PlayStation controller, ignores the virtual Xbox 360 one).
  - `xbox`: no controller changes at all; reTHAWed and GTA both use the Xbox controller directly.
  - Possibly `auto`: pick one by checking which controllers are plugged in. Not designed yet.
  Xbox One / Series controllers report different IDs than the Xbox 360, so the ignore list doesn't hide them, but raw input off still changes how SDL reads them. Not tested with any real Xbox controller.
