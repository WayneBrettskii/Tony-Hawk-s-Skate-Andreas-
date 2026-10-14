# Tony Hawk's SkateAndreas

Some quick Notes:

-First thing show some support for the rethawed team they've created the ultimate Tony Hawk Experience with their mod
-There is no car collision yet but that should be added in the future
-The launcher may take a couple of tries at first if both games don't launch close and retry
-Please share any bugs or glitches you run into I plan on adding some updates this weekend.
-In the future I plan on adding Natas Spins, skitching, park editor features, shooting on your board and more. 
- You should be able to grind majority of the map I want to fine tune this in the next update but most rails and ledges should work for now.
-You can skate the bowl in the east Los Santos skate park like you would any bowl in Tony Hawk as well as the half pipes. 

Skate through GTA San Andreas with reTHAWed's real skating. reTHAWed runs hidden in the background and does the skating; GTA draws everything and keeps its story. Modelled on SkyCraft. How it works: `docs/DESIGN.md`. Change history: `MODLOG.md`.

**Status: playable, but early.** The launcher starts both games and puts the skater into free skate on the map by itself (tested 2026-10-14). Rails, ledges, grinds, BMX, the radio and the first skitch grab work; balance, skitch animation and the rest of the list below are still in progress.

## What works

(Only things tested on the real games are listed here.)

- **GTA plugin loads** (phase 0a, tested 2026-10-07): builds with Visual Studio 2026, Mod Loader loads it, it writes its log to `logs\gta.log` in the project folder.
- **plugin-sdk linked** (phase 0a-2b, tested 2026-10-07): built with `tools\build-plugin-sdk.ps1`; the plugin logs plugin-sdk's game version check: `GTA SA 1.0 US 'HoodLum'`.
- **Hooks into GTA's loop** (phase 0a-2c, tested 2026-10-07): counts drawn frames and world updates per second. While playing: 60 and 60. Main menu: 0 and 0. Pause menu: 0 drawn, 60 world updates.
- **Launcher starts reTHAWed** (phase 0b-1, tested 2026-10-07): `build\Release\SkateAndreas-launcher.exe`; reTHAWed starts and exits normally, controller detected.
- **GUI launcher** (2026-10-12; window, checks and Play path tested): the launcher is a window — **Play** starts reTHAWed (with the plugin loaded) and then **GTA San Andreas**, and closes reTHAWed when GTA exits. It shows **five setup checks** in green / red, a **Game folders** section where each game's folder can be pointed out by hand (remembered in `SkateAndreas.ini` next to the launcher, so another PC needs no rebuild), a **"start reTHAWed minimised"** tick, a **"Build the map..."** button (it carries its own small Python, so a player installs nothing), and an **"open logs folder"** button. `--selftest` runs the checks only (no game started) and `--where` prints the folders and the Python it resolved. `logs\launcher.log` now keeps every run.
- **Our plugin runs inside reTHAWed** (phase 0b-2, tested 2026-10-07): the launcher loads `SkateAndreas-thaw.dll` 0.3 s after start; it writes `logs\thaw.log` with a heartbeat. Tested on the menus only.

- **Link between the two games** (phase 0c-1, tested 2026-10-07): both sides see each other's once-per-second beat through shared memory.
- **Reading the skater in reTHAWed** (phase 1-1, tested 2026-10-07, read only): position and velocity, 10 times per second, confirmed by the distance-vs-speed check (ratio 1.00 to 1.02 skating straight). In THAW, Y is up; pushing speed ~500 units/s.
- **Menus vs level vs paused in reTHAWed** (phase 1-2, tested 2026-10-07, read only): "in menus" YES/no and the level ID change correctly; pause flag turns on and off with pausing.
- **reTHAWed sends the skater to GTA** (phase 2-1, tested 2026-10-07): ~95 samples per second over the link, 0 to 16 ms old, same values as reTHAWed's own log. THAW → GTA conversion unit test: 20/20 pass (cloud and Windows).
- **Skate mode** (phase 2-2, tested 2026-10-07, GTA free roam at the airport): F7 or L3 + R3 in GTA turns it on and off; CJ follows the reTHAWed skater's path 1:1 and faces where he's going (left turns are left turns). Pausing stays on hold instead of switching off. You can't watch it live yet: you see the result when you switch back to GTA.
- **reTHAWed keeps running behind GTA** (phase 3-1, tested 2026-10-07): our plugin hides "lost focus" from reTHAWed's window, so it no longer pauses itself when GTA is in front. Side effect for now: your controller also reaches reTHAWed while you're in GTA.
- **Controller from GTA to reTHAWed** (phase 3-2, tested 2026-10-07): every button, stick and trigger GTA reads arrives in reTHAWed within 0–32 ms. Logged only; reTHAWed doesn't use it yet.
- **Skate live from GTA** (phase 3, tested 2026-10-07): with skate mode on and reTHAWed behind GTA, your controller drives the hidden skater and CJ follows live. GTA ignores the controller meanwhile: Start pauses only reTHAWed (skate mode waits), L3 + R3 still toggles.
- **Skating on GTA's real streets in East Los Santos** (phase 4-1, tested 2026-10-08): with the "Los Santos East" reTHAWed map (thpsgoat.com mod 598) loaded, skate mode puts CJ at the matching GTA spot, so the skater's ramps, rails and walls are GTA's. Lined up to about half a metre. Only that area so far.
- **Test helper F8** (tested 2026-10-07): moves CJ to the waypoint marker on the map. (2026-10-10: now waits for the ground to load first; not tested yet.)
- **The whole of San Andreas as a skate map** (tested 2026-10-10): `tools\gta_to_thaw_map.py` with `--box -3000 -3000 3000 3000 --no-geometry-nodes` makes a reTHAWed map of all of GTA's collision (in 200 m tiles); it loads and skates. GTA's region barriers (the invisible blocks on the bridges, which GTA's story removes) are left out; skated LS to Las Venturas and back with no invisible walls (tested 2026-10-10).
- **Controller fix** (tested 2026-10-07, menus only): reTHAWed started by our launcher ignores DualSenseX's virtual Xbox 360 controller and uses only the real PS5 controller. Downside for now: a real Xbox 360 controller would be ignored by reTHAWed too; an option for Xbox players is planned before release.
- **reTHAWed's HUD in GTA** (tested 2026-10-09): score, combo meter, balance meters etc. are copied from the hidden reTHAWed into GTA while skating.
- **Flips and rolls** (tested 2026-10-09): CJ's body turns with the skater's flips and rolls.
- **Freak-out on bails** (tested 2026-10-09): the meter and animations show in GTA. Switch the freak-out on in reTHAWed's own options first (it's off by default there).
- **Radio wheel** (tested 2026-10-09): hold Share / Back to open it; pick the reTHAWed soundtrack, any GTA station, the User Track Player or Off with the right stick; D-pad Up / Down sets the volume of the station you point at. reTHAWed's music only plays in skate mode. Skate sounds stay audible behind GTA. Choices are remembered in `radio-settings.ini`.
- **Graffiti tags** (tested 2026-10-10, ground tags): in skate mode, get off the board and hold Triangle (reTHAWed's "Graffiti Tagging" on); a green Grove Street tag appears in GTA where CJ sprayed. Up to 50; not saved. Tags on walls not tested yet.
- **BMX mode** (tested 2026-10-10, East Los Santos): get on a BMX in GTA and, in an area reTHAWed's map lines up with, your controller rides reTHAWed's BMX while the GTA bike and CJ follow it (CJ stays on GTA's bike, so missions still see him riding). Radio wheel and reTHAWed's HUD as in skate mode. L1 + R1 (reTHAWed's get off) gets CJ off too and then starts skate mode (tested 2026-10-10: CJ hops onto the board like reTHAWed's rider; in skate mode reTHAWed's Square prompt gets him back on the bike); F7 / L3 + R3 switches it off / on for that ride. Off by itself while GTA takes control (cutscenes, missions). CJ's body copies the rider and the GTA bike turns and flips with reTHAWed's bike (tested 2026-10-10). Smoothed like skate mode; tailwhips, bar spins, tabletops and crank flips show on the GTA bike, taken from reTHAWed's own bike skeleton (tested 2026-10-10). CJ's hands go onto the grips; feet on the pedals being tuned.
- **Smooth frames** (tested 2026-10-10): while reTHAWed runs behind GTA, it skips drawing its unseen 3D world (only its HUD is drawn), so GTA holds 60 frames a second. Graphics unchanged.
- **Tested 2026-10-10:** skate mode can be turned on anywhere on the whole-map level (reTHAWed's skater is moved to CJ); a GTA V style GPS (purple route line to your map waypoint on the radar and the pause map; the waypoint disappears when you arrive); crash and exit lines in `gta.log`, and the previous run's logs kept as `gta.prev.log` / `thaw.prev.log`.
- **Wall marks** (new, not tested yet): in skate mode, click the touchpad on its own when you hit an invisible wall; gta.log gets a "WALL MARK" line with the spot.
- **Vehicle spawner** (tested 2026-10-09): F6 (skate mode off) opens a list of all GTA vehicles except trains, BMX at the top; D-pad / stick to move, Cross spawns, Circle or F6 closes.

- **On-screen hints** (tested 2026-10-12): GTA's own black message box says **`Press L3+R3 to Skate`** when you are on foot (once per session), and **`Press L3+R3 to toggle BMX mode`** while you are on a BMX (at most once a minute). It never covers a message GTA itself is showing, and stays quiet in cars.

## What doesn't work / not tested

- **Skitching, reTHAWed's way** (8.42, not tested yet): press **Up** directly behind a moving car to grab on, **Down** to let go. Balance (Left and Right) is not in yet, and the tow point and speeds are ours, not reTHAWed's. Before 8.42 it was a tow on **L1** from any side.
- **Free skate on Play** now works (8.35 road 3, tested 2026-10-14): about 8 seconds after reTHAWed opens, the map loads in free skate by itself. If it ever doesn't, load it by hand: alt-tab to reTHAWed and pick Free Skate, your map, and your skater. The earlier attempts that did not work are in `docs/DESIGN.md` 8.35.

- **GTA in exclusive full screen freezes reTHAWed** when you switch back to reTHAWed (not caused by our code; happens without it). Run GTA in a window instead (see Requirements). With GTA in a window our full setup works (tested 2026-10-07, GTA on its main menu; not yet tested with GTA in a loaded game).
- (Fixed 2026-10-07: GTA now keeps running at 60 fps while reTHAWed is in front, via `autoPause=0` in the Windowed Mode plugin's settings.)
- No hooks inside reTHAWed yet.
- THAW's unit size is not confirmed (probably inches).
- Pressing Start in reTHAWed also opens GTA's pause menu (both games read the controller). Skate mode holds while it's open, so CJ misses anything skated until you close it. To be fixed in phase 3.
- CJ's skating poses are GTA's roller-skating animations (phase 5-0, not tested yet). CJ rides a simple stand-in skateboard drawn by the plugin (phase 5-1, not tested yet); reTHAWed's own board is planned.
- Fixed 2026-10-10 (not tested yet): GTA crashed the first time CJ spoke after a skate session (skate mode removed his talking face, which GTA needs).
- Water: the skater rides along the sea bed instead of falling into the water (THAW knows nothing of GTA's water). Planned later.
- F7 sometimes needs an extra Alt+Tab into GTA before it works (found 2026-10-07, not understood yet).
- Everything else in `docs/DESIGN.md`: not started.

## Tools

- `tools\thaw_level_dump.py` (Python 3): looks inside a reTHAWed map and writes its collision and rails as 3D files (`.obj`) to `extracted\<level>\` (gitignored; read only, nothing changes in the game). Example: `python tools\thaw_level_dump.py "D:\Games\reTHAWed\data\mod\UserMods\Maps\Los Santos East" --gta lse`
- `tools\gta_area_dump.py` (Python 3): writes GTA SA's own collision for one area as a 3D file to `extracted\gta_<area>\` (read only). Example: `python tools\gta_area_dump.py "D:\Games\GTA San Andreas"`
- `tools\gta_world_stats.py` (Python 3, read only): collision statistics for all of San Andreas per 500 m square, into `extracted\gta_world\` (for the map size research). Example: `python tools\gta_world_stats.py "D:\Games\GTA San Andreas"`
- `tools\gta_extras.py`: hand-made list of walls / fences GTA draws but has no collision for (used by `gta_to_thaw_map.py`; first: the East LS skatepark wall and fence, with rails on both ledges and the fence top: tested 2026-10-09, works).
- `tools\gta_to_thaw_map.py` (Python 3): makes a reTHAWed map from GTA's collision of an area, in `extracted\thaw_maps\` (invisible: collision only, no rails yet; vert in the East LS skatepark: bowl and both half pipes; wallride on every vertical wall; grind rails on kerbs, ledges and the bowl's coping, at most 9,000 rail nodes; grinds and wallrides tested 2026-10-09: work). **Tested 2026-10-08: loads and skates; the bowl skates like Los Santos East's, the half pipes skate well.** Copy the map folder into `D:\Games\reTHAWed\data\mod\UserMods\Maps\` to play it. Example: `python tools\gta_to_thaw_map.py "D:\Games\GTA San Andreas"`

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

The build copies `SkateAndreas.asi` to `D:\Games\GTA San Andreas\modloader\SkateAndreas\`.
To remove the mod, delete that folder.
