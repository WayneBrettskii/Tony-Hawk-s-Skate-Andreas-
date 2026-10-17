# NOW (2026-10-15, night): skitching route A started (DESIGN 8.51) - step 1, the vehicle load test

Whole pool map + `--proxy-cars 3 --thaw-dir D:\Games\reTHAWed`: do three reTHAWed cars load 15 m east of the skatepark?
Try 1 crashed (vehicle's rail data missing); try 2 crashed reading the pak (the model's .tex missing); try 3 copies all five
model files as z_el.pak does (.tex .qb .mdl .rnb .col). **Try 3 PASSED: the user skitched an invisible parked reTHAWed car.**
Step 3 PASSED (F2 moved a car, skitched it). Step 4 (F2 follow mode, F1 nose): reTHAWed's real skitch towed the skater, then
reTHAWed FROZE (twice): its game thread loops in reTHAWed's script interpreter. Our rotation writes don't turn the car.
Test 3 logs the looping script and compares three cars standing at different turns to find the real turn.
Test 3: no freeze; skitch grabbed the left side. Test 4 (turn test): reTHAWed put 6 of 7 turned matrices back within 1 s - the
real turn is kept elsewhere. Test 5: car 2 is the test car; one angle candidate found, turning it changed nothing.
Test 6: a 2 s hardware write watch on the matrices that get put back logs the reTHAWed code that rewrites them.
Result: copy 1 is rewritten every frame from a matrix on the stack (THAW.exe+0x62F0); 5-7 hits look like reads.
Test 7: the second F1 arms the watch at once and logs code bytes + dr6/dr7.
Result: both are real writes, every frame, from one matrix on the stack. THAW.exe in memory differs from disk.
Test 8: the code around the callers is saved from memory to extracted\veh_code\ for offline reading.
Result: the parts are rebuilt from the object's own turn at object+0xDC (0x74 after a position copy), never written so far.
Test 9: that turn is found and turned too. Result: it is copied every frame from object+0x88.
Test 10: object+0x88 is turned too. Result: the car TURNS on screen (9 of 9 hold); the skitch still grabs the same side; the freeze came back.
Test 11: twin scan for places still holding the old turn; the watchdog saves the frozen code. Result: no freeze; scan filled with NaN (bug).
Test 12: the same with the NaN bug fixed. Result: nothing holds the old turn; still the same side.
Test 13: near-point scan around the car. Result: no world points near the car: its rails are local and turn with it.
Open question: is "same side" the same side of the CAR (a nose offset) or the same compass side?
Test 14: reTHAWed's own view shows the skater on the car's LEFT, not its back. Freeze again: a script bytecode reader
(THAW.exe+0xEF50) stuck on one spot (esi 0x18085AEA). User chose: one more round (test 15) then decide. Test 15: script bytes saved on a freeze; skater's offset in the car's frame logged.
Result: freeze without any turn, at the release, in reTHAWed's skater animation script (a symbol at THAW.exe 0x719AE8 read as a call, the reader loops).
Grab point measured at the car's back-left corner (93 in behind, 42 in left). Asked the user: switch to the 8.42 tow, or one more symbol test. User: one more test.
Test 16: write watch on the THAW.exe entry 0x719AE8 for the whole level, to catch what changes it.
Result: never written; it is a built-in C function. FREEZE CAUSE FOUND: a line in reTHAWed's skater animation script uses that
function's name as a setting, and the script reader loops on it. GRAB POINT IS RIGHT (back-left or back-right corner).
Test 17: the plugin blanks such lines in reTHAWed's memory once per run ("script fix" in thaw.log). PASSED: blanked 1 of 1, no freeze
over many skitches at 0/90/180/270 degrees.
User: the hold should be centred on the rear bumper; ours is ~1 m to the side (open).
Step 5 (go-ahead given): smooth following - the car is placed every reTHAWed frame from the last GTA sample + its velocity.
Test 18: smooth (new samples 1-2 in off), but the hold was off (inside a limo, beside a bike) and cars braked for CJ.
Step 6: link v18 gtaSkitchBack; the proxy's hold lands on the GTA car's rear bumper, centred once he holds; thawSkitching while he holds.
Rebuild BOTH plugins (link version 18).
Test 19: cars keep driving, regular cars look good; limo off to the right, bike wobbles (the hold flapped on/off).
Test 20: wider let-go box once holding (no flapping) + hold offsets logged twice a second.
Result: centred after the grab, then he drifts sideways. User: not the balance (meter shows in GTA; centred meter, still on the side).
Cause: the drift follows the car's TURNS (reTHAWed does not turn him with the proxy). Step 7: the proxy is steered so he stays
10 in behind the GTA car's bumper on its centre line.
Test 21: steering works through turns (20 s / 18 s holds within ~10 in). Fixes for test 22: never switch cars under a held skater;
wider grab test; faster steer.
Test 22: follow mode never came on (F2 pressed during the scan, dropped): the user got the old 8.42 tow.
Test 23: early F2 kept; the script-fix search no longer delays "ready". Result: "getting better"; 9 holds 3-29 s; limo held on the spot.
Left: he faces the old way after turns; CJ shoves braking cars. Step 8: turn the held skater to the car's heading; GTA ignores
CJ-vs-held-car collisions. Test 24: no more pushing; floated uphill; once hung to the side.
Test 25: proxy height follows the car's slope; wider grab test (moving with the car); facing fix < 90 deg only.
Result: NO hold recognised (the speed test can't pass: reTHAWed doesn't update velocity while skitching) -> none of the hold fixes ran.
Test 26: grab = stays put relative to the proxy for 0.3 s; proxy parks 0.4 s after the car is gone (the air grab).
Result: holds recognised; a 50 s hold up a steep hill, on the spot. Stutter = the steer chasing a short drop-back. One air grab (not logged).
Test 27: gentler steer (along only trimmed, 150 in/s, 4 in dead zone); logs the skater near the proxy when no hold is seen.
Video (2 clips): mostly right; CJ inside braking cars; grabs on passing/oncoming cars; thrown over the car at the end = reTHAWed's normal skitch jump (user confirmed, not a bug).
Test 28: prediction 0.1 s, spot 20 in back, follow only cars he is behind and heading his way; CJ has NO collision while
holding (the uphill drag: the taxi slowed to 0 while held). Rebuild BOTH plugins.
Video (2 clips): uphill drag gone (the held car crawled only with the queue in front); holds within 4 in of the spot.
Grab/let-go seen late (let-go up to 2.9 s: he rode beside the car). reTHAWed's own skater speed reads exactly 0 while he skitches.
Test 29: grab = own speed 0 while moving with the car (seen in 0.1 s); let-go = own speed back (2 reads). Also: CJ looked off to one side of the bumper in clip 2 (11 s, 24 s) though THAW had him on the spot; GTA now logs
"skitch where" (CJ relative to the held car, as GTA has it). Rebuild BOTH plugins.
Test 29 video: grab/let-go now instant. Measured: CJ 0.65-1.15 m behind the spot at speed, up to 0.56 m aside in turns;
twice stood on a stopped car's roof (push-out took him up). Test 30: CJ put on the spot behind the held car in GTA (0.2 s blends);
push-out from the road goes to the side, not the roof. Rebuild the GTA plugin only.
User: being stopped at a car is fine for now; bails on hitting a car maybe later.
Test 30 (logs, no video): CJ on the spot behind the bumper on every hold sample after the 0.2 s blend (61/61), centre line;
moves needed 0.1-1.25 m back, up to 0.68 m aside in turns. Not tested: speeds above 0.30 m/frame, how it looks. Possible
jump: two 0.5 s side grabs pull him up to 2.5 m. Codex review: 4 code bugs in the move + push-out (DESIGN 8.51 test 30).
Test 31 (GTA side only, built and installed 2026-10-17, not run): the move comes before the ground and car checks, eases
from where it is on every goal change (no jumps at 3 m, re-grab, car switch), cleared on turn-off. Roof landing left for later.
Test 31 played: holds 27-34 s, the move steady. In turns CJ keeps facing the old way (45-81 deg off; reTHAWed undoes our
facing fix). Uphill: reTHAWed's skater stopped dead ~0.9 s, then let go; CJ stuttered (goal flipping at 3 m). Cause of the stop unknown.
Test 32 (built, both plugins, not run): CJ faces the held car (GTA side); the move comes back only after 0.3 s within
2 m; THAW stall log (`vehicles: STALL`) records what the skater does when he falls off the spot.
Test 32 PASSED (user + logs): 7 holds up to 50 s, CJ on the spot and facing the car (0.0 deg off), no hill stutter.
Not tested: faster than ~55 km/h; the stall log (never triggered). Next: a pool of proxies for several cars (DESIGN 8.52); bails on the back burner (user 2026-10-17).
Pool step 1 built (link v19, both plugins, not run): GTA publishes up to 4 moving cars; reTHAWed only logs the list (test 33).
Test 33: list OK on both sides. Found: the parked proxy carried the skater (on its roof) to its parking spot =
East Los Santos; a side grab left CJ holding air (move dropped at 3 m); car-to-car slow (one proxy).
Test 34 (built, reTHAWed plugin only, not run): the proxy waits instead of parking while he is on / next to it. Then pool step 2.
Test 34 played: the wait fired once and parked when he was clear; no map jump. Landing on a car roof pushes CJ off
the side (dead roof branch, deferred). Next: pool step 2.
Pool steps 2 + 3 built (reTHAWed plugin, not run): all 3 proxies follow their own GTA cars; grab any of them (test 35).
Test 35: no freeze; holds on proxies 0 and 1 work. Pool mostly idle (only cars he is already behind get a proxy);
clipping only when not skitching: cars without a proxy are not solid in reTHAWed.
Test 36 (built, both plugins, not run): every car within 20 m (moving or not) can get a proxy, no behind-it test.
Test 36 PASSED for back to back (user: "way better"): 3 proxies busy most of the time, holds on all three. Often a
4th car had no proxy (more proxies = map rebuild). Clipping about as often as before.
Research: reTHAWed levels hold up to 49 vehicle nodes (London), but only up to 11 made at load; no vehicle cap in
rethawed.ini. Suggested: 6 proxies next (map rebuild, link v20), then 8.
8 proxies built (user: "straight to 8"): link v20 (8 cars published), map rebuilt with 8 proxies in extracted (3-proxy build kept
as a backup there). Not run: user installs the map and plays (test 37).
Test 37 (8-proxy map installed): works, up to 6 proxies busy, never short; scan 31 s. Misplaced CJ on a lowrider:
side grab, reTHAWed held him beside the rear wheel; our 120-in let-go ended the hold while reTHAWed still towed him.
Test 38 (built, both plugins, not run): no distance let-go while towed; GTA move limit 4.5 m, back within 3 m. User skipped the test (hard to reproduce): NOT TESTED.
Before the release (built, not run): follow mode turns on by itself after the scan (no F2); the old 8.42 tow is removed.
Release still needs: the launcher building the map with --proxy-cars 8 --thaw-dir, then tools/make-release.ps1.
Launcher Build the map now passes --proxy-cars 8 --thaw-dir (built; the button not run yet).
Committed 1400200. User tests the button and no-F2 skitching tomorrow, then releases.
Release zip built: release/SkateAndreas-d8d79d8.zip (from d8d79d8; no shooting code). User tests it on the Desktop install.
Desktop test: reTHAWed there is 5.0.0.2; the controller join failed (no control, CJ sent to East LS). Fixed: both
add-player addresses accepted; zip rebuilt. Shooting step 1 code is still uncommitted (kept out of the release).
Next feature: shooting on the board, plan in DESIGN 8.53 (no code; waiting for the go-ahead and the aim button).
Shooting step 1 built (GTA plugin, not run): skate-mode weapon log once a second; F12 = one test shot via CWeapon::Fire (test 39). Next: video at speed; then bails on riding into a car; proxy pool.

# EARLIER (same night): natas everywhere WORKS with 100 posts (8.50); default is 100

300 natas spares stalled reTHAWed's map load; 20 and 100 load fine (most needed anywhere: 83). The launcher's
Build the map now makes 100. Uncommitted: 8.46-8.50. Next (waits for the user): commit; Gant tower names; then
skitching / solid cars, wires, shooting (research doc).

# EARLIER (same night): 300 natas spares HUNG reTHAWed's load; narrowed down with 20 and 100

# EARLIER (same night): natas everywhere (8.50) + honest drop counter (8.49) - rebuild map + plugin, test

Natas posts on props all over the map (and the pyramid's tip) through 300 one-point spares in the pool; the drop
counter now tells ollies, bails and touches from real drops. Cloud-tested (harness 20/20); not run in game.
Rebuild the map (send its `natas:` / `landmark:` lines), RailString ~10,800, build, natas around, send thaw.log.

# EARLIER (2026-10-15, night): long rails built into the pool map (DESIGN 8.48) - TESTED: WORKS

User test 17:58: 136 points inside long rails ridden through; the few flagged drops were ollies, bails on landing or
the detector's false alarms (ground rolling by a kerb); about 1 real drop. Uncommitted: 8.46-8.48. Next (waits for the
user): commit; optionally a cleaner join log; then natas everywhere.

# EARLIER (same night): the 8.48 build notes

The pool map now carries long spare rails (3-16 points) that reTHAWed links itself at load; the plugin puts long
GTA rails into them whole. Cloud-tested (map tool read-back; plugin 14/14); not run in game. Needs: rebuild the
whole map, put it in reTHAWed's Maps, check `rethawed.ini` [Memory] (RailString ~10,500+), build, grind, send thaw.log.

# EARLIER (2026-10-15, night): real long rails by run-time links (DESIGN 8.47) - did not help, replaced by 8.48

The pieces of one GTA rail in the pool are now linked into one real reTHAWed rail (chained records), so grinds
should no longer drop at the joins (8.46 test: 9 of 88 dropped). Cloud-tested (25/25); not built or run in game.
Test: whole-map pool map, grind long kerbs / ledges / walls, send `logs\thaw.log`. Off switch: `kChainRails`.
**Result (user, 14:03):** safe (no crash, nothing refused) but joins still drop: 24 of 187 (12.8%, was 10.2%).
reTHAWed seems not to follow the new links.
**Check done (DESIGN 8.48):** build long rails into the pool map instead (slots of 3-16 nodes, ~14,700 nodes);
would leave 2.3% of today's joins. Build waits for the user's go.

# EARLIER (2026-10-15, evening): rail pool safety (DESIGN 8.46) - tested: queue works, joins drop 1 in 9

Rail writes now go through a queue applied on reTHAWed's game thread (checked first); a join test logs whether
grinds carry across the joins of long rails. User test: 0 writes dropped, no crash; 79 joins carried, 9 dropped.

# EARLIER (2026-10-15, later): NATAS WORKS (DESIGN 8.45)

User: natas spun on the two Grove Street hydrants in a test map (`--natas-test --natas-low --natas-at 2495 -1670
--box 1700 -1900 2700 -1150`). A one-point rail is a Natas post. The audit fixes (8.44) were built, tested and
committed by the user. **Next (waits for the user's go):** rail pool safety (writes on reTHAWed's game thread, the
join test), then natas everywhere (one-point spares in the pool, the whole-map finder, the pyramid tip).
Remember: the natas test map replaced the installed map in reTHAWed's Maps folder - put the normal one back.

# EARLIER (2026-10-15): security audit fixes coded (DESIGN 8.44) - built, tested, committed by the user

A friend's audit of `d4ca7f6` (PDF) found 6 issues; checking the code found 4 more. All are coded
(not built, not run on Windows): NaN checks on everything read through the link (N1), the
launcher's `SkateAndreas.asi` comparison (N2), checked road-file and gta3.img reading (F2, with
`parser_tests.exe`), the launcher uses only the bundled Python by full path (F4) and quotes paths
safely (N4), no personal paths (F1: STATUS.md; F1b: release builds carry no PC folder; the radio
logo now follows the launcher's reTHAWed folder), and `tools\make-release.ps1` / `build-plugin-sdk.ps1`
rebuilt for clean, checked, fingerprinted releases with Python 3.14.8 (F3, F5, F6, N3).
**Decisions (user OK'd the recommended ones):** bundled Python only (+ `PythonExe=` by hand), no
history rewrite, no paid signing, nothing committed without the user. **Next:** build + test (see
MODLOG 8.44), then the clean release needs a commit (ask first). Feature research (skitch, rails,
shooting, natas) is in the doc "SkateAndreas research" - not started, waits for the user's go.

# FIX (2026-10-14, afternoon): free skate froze while loading - fix 2b

User: "the launcher isn't turning on skate mode in san andreas". Cause: fix 2 (the radio unmute) sent
reTHAWed its "focus gained" event while the map was still loading, and the load froze (logs 01:44 and
01:47). Fix 2b in `src/thaw/autostart.cpp` waits until the map has been up for 8 s with steady frames.
**Tested 2026-10-14 16:51 (dev build): works** - map loaded, `the load is over` at 20 s, sound on,
skate mode on with L3+R3. The no-skitch zip (`release\make-noskitch.ps1`) carries it.

---

# HANDOFF (2026-10-14): read this first

**Git:** nothing committed since `2a6d08a`. Uncommitted (all intended): `INSTALL.md`, `MODLOG.md`,
`README.md`, `STATUS.md`, `docs/DESIGN.md`, `src/common/link.h` (layout v17), `src/gta/cars.cpp`
(skitch part only; the car-rail gate is untouched), `src/thaw/bmx.cpp` (skitch 8.42), and
`tools/gta_to_thaw_map.py` (proxy vehicles). AGENTS.md: do not commit unless asked.

**Builds:** `build\Release` holds the 8.42 plugins (`SkateAndreas.asi` 12:31, `SkateAndreas-thaw.dll`
00:41 on 14 Oct). **`release\SkateAndreas.zip` is still the old build (before 8.42).** Rebuild it
(`tools\make-release.ps1`) only after the skitch test is accepted. The launcher is unchanged.

**The map in reTHAWed:** only one SkateAndreas map can be installed (all use the prefix `MPGTALSE`,
level ID `0x9F014B98`). Check which one is in `D:\Games\reTHAWed\data\mod\UserMods\Maps\`. The full
map is `my-passthrough GTA SA pool`. Do not write into the reTHAWed folder; the user copies maps.

**Open threads, in order:**
1. **Skitch, step 1 (DESIGN 8.42).** Up on the D-pad, or the left stick pushed up (below -80), directly
   behind a moving car (1 to 8 m back, within about 31 degrees of its nose, same level) grabs on. Down
   lets go; the car stopped for 0.5 s, more than 12 m away, or lost lets go too. Link v17 holds the car
   by its pool index. The user said "good start" after the grab test; the stick grab (8.42b) is built but
   not reported. Not built: balance (Left and Right), the animation and the balance meter.
2. **Proxy vehicles (DESIGN 8.42c).** `tools\gta_to_thaw_map.py --proxy-cars N --thaw-dir <reTHAWed>`
   copies one vehicle record from the player's own packs and writes N of them 15 m east of the start.
   The first load **crashed** in `Obj::CRailManagerComponent::InitFromStructure` (EAX = EBX = 0; see
   `reTHAWed\debug.txt`). The fix drops the record's unnamed items (the rail manager):
   `_keep_vehicle_item`, 6 of 10 items kept. **The fix is not yet tested.** Test map:
   `extracted\thaw_maps\proxytest` (name "Proxy test"). To test: replace its four files
   (`MPGTALSE.pak.wpc`, `_sky`, `_sfx`, `level.ini`) in `UserMods\Maps\proxytest`, load it through the
   launcher, look for three small cars 15 m east of the start, and read `debug.txt`.
3. **Requirement:** CJ must be able to skitch the real San Andreas cars. That needs a pool of proxy
   vehicles that follows GTA's cars near CJ, not one test car. Risk: a vehicle moving under the skater
   (the rail pool showed that a moving object under him can NaN the grind maths).
4. **Animation and balance meter (not built).** reTHAWed's balance arrow, in `rthw.dll` 5.0.0.1:
   float `0x6D321680` (angle) and flag byte `0x6D321684`. The setter is `RT_SetBalanceArrowRotation`
   (handler `0x6D107CB0`). The draw routine at `0x6D107380` uses the arrow when the flag is set. Not
   yet known: what calls the draw routine, and the angle's units. The skitch pose lives in reTHAWed's
   scripts.
5. **Scripts (blocked).** NodeQBC (`D:\Mods\thps-tools\nodeqbc-master`) needs Node. A portable Node
   22.12 is in the approved temp folder. `qb.pab.wpc` is header-only for NodeQBC; level packs decode
   further but fail on THAW item type `0x73`, which NodeQBC doesn't know.

**Facts that cost time to find:**
- **qbkey:** a CRC variant with the table from NodeQBC `Internal/Keys.js`. Start at 0xFFFFFFFF, lowercase
  the name, no final XOR. Checked: `load_MPGTALSE` = 0x9F014B98, `load_z_mainmenu` = 0x99CE4D72. The
  repo has no qbkey yet; the working copy is in the temp folder (`qbkey.js`).
- **THAW pak items:** 16-byte headers (kind, name, value, next). `next` is relative to the pak body; the
  body base is the Class item's address + 16 minus its `next`. Kinds: `0x1B00` checksum (name 0 = flag),
  `0x0D00` vector (data = a `0x100` word + 3 floats), `0x0700` string, `0x0300` integer.
- **Vehicle record:** items for position, rotation, name, `Class = Vehicle`, `type`, flags, the model path
  (`veh\veh_...\veh_....mdl`), and a rail manager. The model path is inside the record, so no type
  table is needed.
- **Map tool:** `QB` takes raw checksums as names (`_k`) and writes integer items.
- **rthw.dll 5.0.0.1:** image base `0x6D0C0000`. Disassembly from `dumpbin /DISASM` is in
  `%LOCALAPPDATA%\Temp\opencode\rthw_disasm.txt`. The addresses only hold for 5.0.0.1.
- **Rail lists:** every test build overwrites `extracted\thaw_maps\_railpool\rails_in_map.csv` and
  `all_rails.csv`, which the plugin reads for the full map. Back them up first. Backups are in
  `%LOCALAPPDATA%\Temp\opencode\railcsv_backup`, and they were restored after each test.
- **Game files:** none are tracked in the repo (checked). Copies made for the tests are in temp or in the
  gitignored `extracted\`.

**Next session:** ask the user for the proxytest result and read `debug.txt`. If the proxies load, the
next step is one proxy following one GTA car near CJ (the moving-object test). If they still crash,
look at the other kept items (the rotation and the flags).

---

# WHERE WE LEFT OFF (2026-10-13 01:30)

**Now (2026-10-13, Claude):** working on free skate on Play again — **8.35 road 3**, a new road (not a
repeat of roads 1-2): run what reTHAWed's level menu runs (`SetGameType freeskate`,
`SetCurrentGameType`, `change_level level = load_MPGTALSE`) from reTHAWed's `Present`. **Tested: the
level loads by itself**, but the pad never joined reTHAWed as player 1, so no input. **Fix 1 coded**
(join the first open controller via rthw's own "add player") **works**. Then no reTHAWed sound:
MuteOnUnfocus kept it muted. **Fix 2 coded** (send reTHAWed a "focus gained" event while muted in our
level). **Tested 2026-10-14: works** — free skate loads by itself, reTHAWed's sound comes back first
try, no alt-tab needed; the sources that make the shipped DLL are committed and the release carries
the exact build the user ran (SHA-256 checked).
User 2026-10-13: reTHAWed 4.5 and 5.0.0.2 were tried and are fine (thread 2 below is answered).

**Read in this order:** `AGENTS.md` (the hard rules — they are not optional), `README.md` (what
works / what doesn't, kept honest), then this. `docs/DESIGN.md` is "how it works", one numbered
section per feature; `MODLOG.md` is the change history, newest first. Where this file and the
code disagree, the code and the logs win — and fix this file.

**What the project is.** reTHAWed (a Tony Hawk's American Wasteland reimplementation) runs
hidden behind GTA San Andreas: GTA draws the world and keeps its story, reTHAWed does the
skating. The two talk over a named shared-memory block (`src/common/link.h`) — **both plugins
include it, so they must always be rebuilt together.**

**Names (renamed 2026-10-12).** The mod is **Tony Hawk's SkateAndreas**. Files:
`SkateAndreas.asi` (GTA), `SkateAndreas-thaw.dll` (reTHAWed), `SkateAndreas-launcher.exe`. GTA's
Mod Loader folder is `D:\Games\GTA San Andreas\modloader\SkateAndreas\`. **The repo folder is
still `D:\Mods\my-passthrough`** (deliberate — it is not worth the churn). Anything that reads
like a *folder* path still says `my-passthrough`; do not "fix" those.

**Build and test.**
- Build: `cmake --build build --config Release`, with CMake at
  `C:\Program Files\Microsoft Visual Studio\18\Community\...\CMake\bin\cmake.exe`. It copies
  `SkateAndreas.asi` into the Mod Loader folder.
- **GTA must be restarted** for a new `.asi`; reTHAWed must be restarted (through the launcher)
  for a new `.dll`. A stale build is the most common false result.
- MSBuild sometimes does not relink: if the `.asi` timestamp does not change, touch a source
  file and rebuild.
- **The launcher window holds its own `.exe` open** — close it before rebuilding or the link
  fails with LNK1104.
- Logs: `logs\gta.log`, `logs\thaw.log`, `logs\launcher.log` (previous run kept as `.prev.log`).
  `SkateAndreas-launcher.exe --selftest` runs the setup checks without starting a game, and
  `--where` prints the folders it resolved. Since 8.38 the plugins find their folder through
  `SKATEANDREAS_DIR` (the launcher sets it), so a copy of the mod works from any folder; with no
  launcher they fall back to the folder this project was built in.
- The agent cannot see the game. **Instrument and read the logs** (AGENTS.md). The user plays and
  reports; they are the only eye on the screen.

**Traps that cost the most time — read before touching anything.**
- **THAW units are inches** (`scale = 0.0254` m per unit). A range check written in "metres" is
  ~40x off. This wasted a whole round on the skitch.
- The plugin only recognises levels it has an alignment for (`src/common/convert.h`). With no
  alignment it falls back to "relative" placement and **CJ floats**. The whole-map level is
  `0x9F014B98` = `qbkey("load_MPGTALSE")`.
- `0x99CE4D72` is **`qbkey("load_z_mainmenu")`** — reTHAWed's *front-end* level, not a map. Do
  not add it as an alignment (that mistake is recorded in `convert.h` and DESIGN 8.35).
- A rail that **moves** under the skater blows reTHAWed's grind maths to NaN → GTA yellow screen
  + `drawn 0`. Never move a rail the skater is near (the pool has reserved slots for this).
- reTHAWed's front end **only polls the pad once the player touches it**, and reads nothing
  while GTA is the front window. Feeding it synthetic input cannot work (DESIGN 8.35).

**Working now (all tested on the real games).** Skate mode across the whole map; the camera fix
(the right stick works in skate / BMX mode); rails everywhere (a 4,500-rail pool that follows
the skater, long rails, tall ledges, building and bridge tops, GTA's railroad tracks); parked
and slow cars are grindable on their roofs; **skitch** was a tow on **L1** and is being changed to reTHAWed's grab (8.42, **not tested yet**: Up behind a car, Down to let go);
BMX mode with GTA's bike following reTHAWed's rider; the radio wheel; graffiti tags; the GPS
route line; smooth 60 fps; the **GUI launcher** (Play starts both games, setup checks, game
folders pickable, remembered in `SkateAndreas.ini`); and the **on-screen hints**
(`Press L3+R3 to Skate` once a session, `Press L3+R3 to toggle BMX mode`).

The car-rail fix (39b52ee) and the launcher's version warning (9be5211) are **built and
committed but not yet seen in game** — see the three open threads below.

**Dead ends and known-bad — do not re-tread.**
- **Grinding a moving car** → NaN. The skitch is the answer; do not try to tow the rail.
- **Starting in free skate on Play** — dead end, DESIGN 8.35, code in `src/thaw/autostart.cpp`
  switched off. Two routes tried; both explained there.
- **Natas spins** need THAW objects our map has no equivalent for (DESIGN 8.33 F).
- **Power lines / cables** as rails — deferred (the wire geometry is quantised).
- A moving car still drives through CJ, and cars brake for him: a re-posed ped cannot block a
  car. This is inherent, not a bug to chase.

**Wish list (user, 2026-10-10, DESIGN 8.28):** park-editor ramps across San Andreas, board whack
(a melee hit with the board), shoot mode for skate / BMX; then water.

**No work is half-done in the code** — the last change (9be5211) is committed, the tree is clean,
and the release package matches the build. The user is playing and reporting. Take whatever they
ask for next, write the plan into `docs/DESIGN.md` first (AGENTS.md), and do one change at a time.
The three open threads that need *testing*, not code, are listed below.

**Releasing (2026-10-13).** `tools\make-release.ps1` builds `release\SkateAndreas.zip` (~11 MB):
the three binaries, `tools\` with its **own embeddable Python** (so players install nothing),
`INSTALL.md` (the player guide), `README.md` and the **MIT `LICENSE`**. `release\` is gitignored.
The package is rebuilt after every change to a binary (verify: the packaged `.exe`/`.asi`/`.dll`
must be byte-identical to `build\Release\`).

**reTHAWed 5.0.0.2 is already out, and this mod is pinned to 5.0.0.1** (user 2026-10-13). As of
commit 9be5211 the launcher **warns instead of refusing**: a different `reTHAWed.exe` shows an
amber line, `!!  reTHAWed 5.0.0.1  (your copy is a different version - trying it anyway)`, **Play
still goes through**, and `--selftest` passes with that one `[WARN]`. The bet (user 2026-10-13:
*"let them try to launch at least maybe it will work"*) is that the plugin's own **code checks**
are the real authority — each one disables its feature rather than misbehave, and logs every
address that moved. What actually supporting an update costs: `docs/DESIGN.md` section 11, first
bullet. Do not assume "the version doesn't matter" — it does.

**Three open threads — all need a *run*, not code:**
1. **8.41 car rails** (39b52ee, untested in game). Grind a **parked** car — should work. Try a
   **moving** car — should find no rail at all, and `logs\gta.log` should say `NO RAIL (moving)`.
2. **reTHAWed 5.0.0.2** — press Play (the amber line lets you through), then read `logs\thaw.log`
   for `code check 0x... DIFFERENT` lines. That list is the work; there is nothing to change until
   the log says what moved.
3. **The player-folder hop** — never run end to end. The games have not been started from a
   player-style folder, so the rail reader finding `SKATEANDREAS_DIR` inside reTHAWed on someone
   else's PC is *reasoned* (shared `src/common/paths.h`), not *seen*. See DESIGN 8.38/8.40. Rebuild
   the temp test copy in one command when wanted; it was cleaned up.

---

# (older) WHERE WE LEFT OFF (2026-10-12 10:45)

Whole map done (8.31). **Rails (8.32):** whole map = 101,404 rails / 245,951 nodes; 30,000 nodes froze
reTHAWed after loading. User chose **R3, a rail pool** that follows the skater.
R3a / R3a-2 done: reTHAWed's rail node records fully decoded. **R3b proven (user 2026-10-12):** a
*moved* rail is grindable. F5's test rail (shown in GTA as 13 orange lights) was ground 10 of 182
samples in test 1 ("GROUND IT": 0.0 from the rail, 0.0 above, 4->86% along it) and 10 more samples in
test 2 (27->93%). reTHAWed keeps nothing that blocks grinding a relocated rail, so the pool can be
built. **Next:** the pool itself — R3b (the map tool writes the full rail list and the level keeps a
fixed ~9,000-node pool, parked below the map) and R3c (our plugin fills the pool with the GTA rails
near the skater). **Whole-map pool tested 2026-10-12: it works** — 4,500 pool rails, 172-430 placed
within 150 m as the skater moved; some rails that could not be ground before now can be. **Still missing
(rail supply, not the pool):** long rails (3+ nodes, 25% of rail nodes) are skipped because the pool only
places two-node rails; and ledges taller than ~1.6 m get no rail from the finder (so a wallride-top has
nothing to grind). Next: fix the long rails, then the tall ledges. Then the extra sources (bridges, power
lines, railroad tracks, Natas points). After rails: water.

After the map is right (user requests 2026-10-10, not started, plan in DESIGN first):
- grind, stall and skitch every GTA car
- park editor: place skateable ramps, bowls, rails across San Andreas
- board whack: hit GTA peds with the board like a melee weapon
- shoot mode for skate and BMX (earlier list)

---

# STATUS: BMX tricks (bar spins, tailwhips) don't show on the GTA bike (SOLVED 13:57: B9, from the bike's own skeleton)

**Written 2026-10-10 13:50 after two attempts didn't work (AGENTS.md rule).** No third guess.

## What was tried
1. B7 (13:05): the GTA fork / bars copy reTHAWed's Board_Nose bone, cranks Board_Tail (picked from the
   B6 log, where Board_Nose turned up to 71 degrees in turns and once 180).
2. B7 fix 1 (13:30, mostly feet): same source for the bars.

## What the test showed (user 13:28, video + logs)
- Bars don't spin, the frame doesn't whip round in tailwhips.
- reTHAWed's Board_Nose never turned more than 20 degrees in that whole ride, with several bar spins:
  the bar spin is not in the rider's bones. Our code did turn the GTA bars as asked (log: "bars as
  drawn" matched the small numbers), so the GTA side works; the data source is wrong.
- Feet still counted off the pedals most of the time; feet went round ~2x as far as the crank bone.

## What we know now
- reTHAWed's bike is a separate skinned model: `data\models\Veh\veh_BMX\veh_bmx.skin` (read only,
  looked at its file list; nothing copied into the repo). So it has its own skeleton and animations;
  the bike trick scripts (bike_scripts, decompiled in the scratch folder) play their animations
  through a different command (0x35197927) than the rider's tricks.

## Next (B8, research, log only)
Find every skeleton in reTHAWed's memory once the rider is on the bike (same layout as the rider's),
log them, follow the small ones that aren't the rider's and log which bones turn in tricks.
Then copy those bones to the GTA bike's parts (B7's GTA side already works).

---

# STATUS: BMX B4b: the GTA bike doesn't move with reTHAWed's bike (SOLVED 12:41: fix 3 works)

**Written 2026-10-10 01:55 after two attempts didn't work (AGENTS.md rule).**
No third guess: next is a logging-only test to learn how reTHAWed's bike sits.

## Update 12:40: cause found
F5 test (12:30): nothing changed on screen -> case (a). GTA draws the bike from its RenderWare frame;
our swap called m_matrix->UpdateRW(), which does nothing (that matrix isn't attached to the frame).
GTA's CEntity::UpdateRW (0x446F90) copies it. Fix 3 uses that. Fix 2's numbers (board bone) stay.

## Update 02:10: fix 2 didn't work either (third attempt) - stopped
User 02:04: "the bike is still not staying with CJ". gta.log: the bike was drawn at reTHAWed's
bike in ~55 frames a second, with sensible values (lifts to 33 units in hops, pitches up to
~50 degrees, turns sideways in spins). So either (a) changing the bike's matrix right before
GTA draws it doesn't change what is seen (GTA may draw the bike from data made earlier in the
frame, or later than our hook), or (b) it does, and the look is still wrong for another reason.
No more guesses: a test switch (F5 in BMX mode) draws the bike upside down and 1 m higher.
If the user sees that: (b), ask what exactly looks wrong. If not: (a), find where GTA really
draws the bike.

## Update 02:05: research log read
Riding normally, the board bone in the rider's space is right (1,0,0), "up" (0,0,-1) (towards the back),
"at" (0,1,0) (up), 17.8 units above the feet: exactly the skateboard set-up. It IS the bike (rises in
hops, turns in spins, flies off in bails). Fix 2 draws the GTA bike there with these fixed numbers.

## What the user sees
- B4a works: CJ's body does the rider's tricks and flips.
- The GTA bike doesn't flip with him and drifts away from him in jumps.
- Attempt 1 (bike picture follows reTHAWed's "board" bone): "same as before".
- Attempt 2 (bike picture follows the rider's hips): "still janky".

## What the logs show (test 01:48)
- The picture swap runs (~50 frames a second drawn turned): drawing isn't the problem.
- The board bone DOES move on the bike: 56-163 degrees from where it was when we lined up, about 90 degrees most of the time even when just riding.
- We lined up 1 s after BMX mode started. reTHAWed's get-on-the-bike animation takes about 2 s, so we lined up in the middle of it: the "start" we compare against was a random pose. Both attempts used that start, so both were off.
- The hips swing 50-170 degrees while riding (leaning, pedalling): a poor guide for the bike anyway.

## Next (research, logging only)
- Turning the picture switched off again (bike upright, as in B4a) until we know more.
- While BMX mode is on, twice a second log the board bone's axes and position in the rider's own space, with the rider's speed. Test: ride straight, do a flip, a barspin / tailwhip.
- From that: which board-bone axis is the bike's forward / up, and where the bike sits, as fixed numbers (no lining up at run time).

## Also found in the 01:48 log (fixed now, see MODLOG)
- BMX mode could start while CJ was still getting on or reTHAWed was still switching the rider: CJ was thrown off again (151-160 s).
- After switching BMX mode off, buttons still held for reTHAWed reached GTA at once (likely the "gun stuck shooting": on a GTA bike, a button reTHAWed uses for tricks fires).

---

# (older) # STATUS: BMX mode (8.29 B3): "replay" / camera, CJ off the bike, floating

**SOLVED 2026-10-10 01:14:** no replay, no floating (user test). Kept below for the record.

**Written 2026-10-10 01:10 after two attempts didn't fix it (AGENTS.md rule).**
The next change is driven by what the logs show, adds logging for what they
don't, and is not a guess at a third cause.

## What the user sees (tests 00:51 and 01:01)
- Riding and jumping work.
- GTA's camera goes to "weird angles" and it "kept doing a replay", noticed while using the radio wheel.
- After switching BMX mode off (L3 + R3), CJ floats in the air on the bike.

## Update 01:12 (user 01:06): it's GTA's real replay
"REPLAY" in the corner, then a playback of what CJ did, when changing radio
station. In gta_sa.exe: CReplay::Update (0x460500) starts a replay on the
keyboard's F1 (key 1001) when byte 0x8A6160 is set and no control flag is
set. Skate mode sets a control flag (so no replays there); BMX mode doesn't.
Something sends F1 at that moment (probably DualSenseX mapping the
Share/Create button). Fix: 0x8A6160 cleared while skate / BMX mode is on.
GTA restoring things after a replay may also explain the floating and CJ
leaving the bike: to see in the next test's log.

## What the logs show
- gta.log: "CJ got off the bike" while reTHAWed's rider is still on its bike (262 s, 271 s; the 2nd only 2 s after BMX mode started). Cause unknown: nothing logs why GTA took him off.
- gta.log: "the rider got off the bike in reTHAWed" at 198 s, and on again 2 s later without our asking: probably a bail (the "on bike" byte drops for a moment). BMX mode then switched itself off, which is wrong for a bail.
- Nothing in our logs mentions a replay. No radio station change was logged in the 01:01 test.

## What was tried (MODLOG)
1. Bike upright (no flips on GTA's bike), GTA's unique stunt jumps off, bike "not static" at the end. The replay and floating stayed.

## Best explanation now (to confirm with the next log)
- Our pad blanking makes GTA think nobody has touched the controller. GTA's "idle" cinematic car camera (Obbe camera, TheCamera.m_bObbeCinematicCarCamOn) then starts: dramatic angles that look like a replay. It's independent of the radio; it just takes a while of "no input".
- Floating: the bike is left at reTHAWed's height (reTHAWed ramps / bowl aren't in GTA), with GTA's physics not picking it up again.

## Next change (fix 2)
- While skate / BMX mode blanks the pad: tell GTA the pad was just touched (CPad LastTimeTouched); in BMX mode the cinematic car camera is switched off if GTA starts it (logged).
- A rider "off the bike" only counts after 2.5 s (bails).
- When BMX mode ends: the bike goes onto GTA's ground under it (GTA's ground finder), speed 0, physics woken.
- Logging: why CJ left the bike (ped state, knocked-off flag, bike health), GTA replay mode if it ever starts.

---

# (older) # STATUS: tags (8.28 a): no Grove tag appears in GTA

**SOLVED 2026-10-10 00:03:** fix 3 works (user: "working perfectly now"); ground tags tested, wall tags not yet. Kept below for the record.

**Written 2026-10-09 23:45 after two real attempts failed (AGENTS.md rule).**
Fix 2 (below) is built on what the logs showed, not a guess; waiting for its test.
If it fails too, stop and look at the logs with the user before changing more.

## What the user sees
- Skate mode, off the board, hold Triangle at a wall: CJ does the spraying
  animation (body copy), but no tag appears in GTA (tests 23:32 and 23:36).

## What was tried (MODLOG has the details)
1. **T1 (23:32):** thaw.log: our code check of the graffiti update failed
   (reTHAWed's rthw.dll points that update's call to its own wrapper at start),
   so the THAW side switched itself off. GTA side ready ("tag calls go through us").
2. **Fix 1 (23:36):** check loosened. Both logs: 6 tags *started*, 0 *finished*.
   Our "finished" hook was the score-script call 0x005F933A, which reTHAWed only
   runs when a combo score is going (skipped at 0x005F92F6); a standing tag
   never reaches it.

## Update 23:55: fix 2 worked partly; fix 3 written
Tags now appear in GTA (4 placed, no crash), but purple Ballas, in the street,
standing up. Fix 3 (MODLOG): reTHAWed's exact spot, the model's real shape
read from gta3.img, Grove value set every frame, picture layers logged.

## What fix 2 does
- Counts the call at 0x005F92E0 instead: it puts every finished tag in
   reTHAWed's world (0x005F8680, 12 arguments, 2nd = wall/ground). rthw.dll's
   wrapper (0x6D151970 in the file) calls the original 0x005F8B20 first, so
   this code still runs.

## What's known to work
- THAW side: start / stop of spraying reaches GTA (gta.log "started / stopped
  spraying", ~0.3-0.5 s each).
- GTA side: its two tag calls are redirected without problems; no crashes.

## Not yet known
- Whether GTA's tag model shows up, which way it faces, and whether GTA's
  object placement works at all (no tag has been placed yet).

---

# (older) # STATUS: radio while skating (8.27): GTA's stations don't play on foot

**SOLVED 2026-10-09 21:15:** fix 2 works in game (user: "Radio stations worked
great"). Kept below for the record.

**Update 2026-10-09 21:05: cause found by the diagnostics.** The stop comes
from 0x4EA5D2 in GTA's radio code (0x4EA590): with no vehicle radio it asks
whether an interior radio is playing (0x4EA5C3 -> 0x507280) and stops the radio
if not. Fix 2 answers "yes" while our station plays (MODLOG). Waiting for the
in-game result.

**Written 2026-10-09 20:58 after two real attempts failed (AGENTS.md rule).**
Stopped guessing. Everything else of the radio works: Back hold steps through
the stations, the names are right, the reTHAWed station plays, reTHAWed's music
pauses / resumes, a tap of Back still reaches reTHAWed.

## What the user sees
- Choosing a GTA station (Playback FM etc.) while skating: no music.
- Attempt 1 (20:45): also a stutter (our code restarted the station every 3 s;
  each start loads it). Attempt 2 (20:55): no stutter, still no music.

## What was tried (MODLOG has the details)
1. **R1:** `AudioEngine.StartRadio(id, 0)` (0x507DC0) on foot; restart every
   3 s if `IsRadioOn()` was false. gta.log: "GTA's radio had stopped" every 3 s.
2. **Fix 1:** found (gta_sa.exe) that GTA's interior-radio code (0x4D6E60)
   stops the radio outside clubs/houses; redirected its three StopRadio calls
   (0x4D71D2, 0x4D71FD, 0x4D7221) to skip while our station plays; one restart
   at most. gta.log 20:53: redirect installed, **0 stops skipped**, radio still
   off 3 s later. So it isn't that code.

## What we know now (read from gta_sa.exe, not tested)
- `CAudioEngine::StartRadio` (0x507DC0) does nothing if 0x4DBEC0 on 0x8AE554
  says yes (probably "a cutscene / mission audio track is active").
- `CAERadioTrackManager::StartRadio` (0x4EB3C0) can also return without
  starting: when flags at 0xB7CB48 / 0xB7CB49 are set and the manager's state
  (+0x68) is 7, or when `IsAmbienceTrackActive` (0x507210) says yes (it then
  plays a frontend sound 0x23 instead). Other stop calls: 0x4E9820 from
  0x4EA5D2, 0x4EB7DF, 0x4EB835, 0x4EBE2F and from the audio engine 0x507AB4,
  0x507CCA, 0x507E5B, 0x507F6B.

## Next step (measuring, not guessing) - user OK 20:57, built as "Radio diagnostics" (MODLOG)
A log-only build: right after our StartRadio and then every second while a GTA
station is chosen, log: the radio manager's state (+0x68), its flags (+0x00..
+0x07), `IsRadioOn`, `IsAmbienceTrackActive`, the 0x4DBEC0 answer and the bytes
at 0xB7CB48/49; and count calls of the stop functions above (which one stops
it). Then fix only what the numbers show.
Alternative if this stays stuck: GTA's radio only in a vehicle is how the game
is built; mods that play radio on foot exist, so it is possible, but it may
take a few rounds.

---

# STATUS: phase 5-3d, CJ's copied body: shoulder "shimmy" and stutter at take-off

**Update 2026-10-09 10:45: cause found by the frame recorder.** The look-ahead
(speed x sample age) made CJ jump back and forth: the age is only known in
16 ms steps. Fix 3 turns the look-ahead off (MODLOG). Waiting for the in-game
result.

**Written 2026-10-09 02:55 after two real attempts failed (AGENTS.md rule).**
Stopped guessing; next step is measuring. Nothing is broken otherwise: the body
copy, board, tricks, tilt and facing all work.

## What the user sees
- Copying reTHAWed's skater body onto CJ works "pretty good" (02:39).
- CJ's shoulders do a "shimmy" motion while skating.
- His body stutters when jumping off a ramp.
- Both still there after fix 1 (02:44) and fix 2 (02:50).

## What was tried (MODLOG has the details)
1. **Fix 1:** GTA's animation under the copy held on one standing pose (it
   switched 172 times a session before); chest twist from the collar bones;
   samples with changed bone lengths (half-updated reads) skipped.
   Log afterwards: 1 animation switch, 0 samples skipped. No change for the user.
   So: not the base animation, not torn reads.
2. **Fix 2:** each sample keeps the time its reTHAWed frame was first seen
   (repeat reads had made GTA's look-ahead jitter CJ); chest twist from the
   skater's chest axis instead of its shoulder joints (link v6).
   Log afterwards: about 34 repeat reads a second while skating (so reTHAWed
   runs about 60 frames a second, we read about 95 times); GTA sees samples
   0, 16 or 31 ms old. No change for the user.

## What we know
- GTA draws 60 frames a second; reTHAWed about 60; the two clocks are not in
  step. Sample age 31 ms means GTA sometimes gets no new reTHAWed frame for two
  of its frames and then a jump of two.
- Only CJ's position is moved ahead by speed x age. The body pose, the board
  and the tilt are NOT: they show the latest reTHAWed frame as is, so they
  move in uneven steps (0, 1 or 2 reTHAWed frames per GTA frame), and they can
  be up to one frame out of step with CJ's position.

## Ideas not yet checked
- A. Uneven steps (above). Usual cure: show everything about 2 reTHAWed frames
  late and blend between the two samples around that moment (a small buffer
  of samples with their times). Smooth, at the cost of about 33 ms delay.
- B. The "shimmy" may be real: reTHAWed's own pushing animation may move the
  shoulders like that; the copy would then just show it. Needs a look at the
  reTHAWed window side by side.
- C. Position, facing, board and body are read from reTHAWed at slightly
  different moments of its frame and could belong to different frames.

## Next step (user OK'd 2026-10-09 10:22; recorder built, DESIGN 8.18)
1. Measuring run: F9 in GTA starts a 3-second record of every GTA frame (which
   reTHAWed sample, its age, CJ's position, hips, chest axis, board) into
   `logs/gta_frames.csv`; Claude reads it and shows where the jumps are.
2. User looks at the reTHAWed window while pushing: do its shoulders move like
   CJ's? (idea B)
3. Then, depending on the numbers: the blend buffer (idea A).

---

# Earlier STATUS (phase 4-2d step 1, resolved 2026-10-08)


**RESOLVED 2026-10-08 12:52:** attempt 6 works: the map loads and skates (the
user skated the skatepark bowl). Kept below as the record of how we got
there; the fixes are listed in MODLOG and DESIGN 8.9. One open item: the user
got stuck in a corner once (not logged).

**Update 2026-10-08 11:20:** the user OK'd going on. Suggested step 1 below
was done (attempt 3: empty scripts + the two lookup lists); it crashed the same
way. A byte-by-byte comparison with LSE then found the real mistake: the node
list's array header had two bytes swapped (`00 0A 01 00` instead of
`00 01 0A 00`) since attempt 1. Fixed in attempt 4; waiting for the in-game
result. See MODLOG.

**Update 11:30:** attempt 4 worked for the main PAK: the whole level loads,
collision included. The crash moved to the sky (our sky PAK was empty).
Attempt 5 gives the map a sky shaped like LSE's (no textures, no meshes).

**Update 11:35:** attempt 5 loads without crashing. New problem: no solid
ground under the skater. Next: a logged run (position / speed) to see whether
he falls through the collision or hangs still.

**Update 12:45:** logged run: the skater hangs still at the start point, with
ground just below him. Our triangles faced the wrong way (ground facing down,
compared with LSE). Attempt 6 reverses the corner order of every triangle.

Below: written after attempt 2, when work stopped.

Written 2026-10-08 after two failed attempts, as AGENTS.md asks. (AGENTS.md
mentions `STATUS-handoff.md` as a template; that file does not exist in the
project, so this follows the MODLOG headings.)

## Goal of the step

`tools/gta_to_thaw_map.py` turns GTA San Andreas collision for the Los Santos
East area into a reTHAWed user map (`MPGTALSE`, folder
`SkateAndreas GTA East LS`). Wanted result: the map loads in reTHAWed and the
skater stands on invisible GTA ground.

## What works

- reTHAWed finds the map in `UserMods\Maps` and lists it ("GTA East Los Santos
  (SkateAndreas)").
- The sky PAK (empty) loads without complaint.
- Attempt 2: our texture file (0 textures) and our scene (no materials, 2,486
  empty sectors) both **load without crashing**.

## What fails

| Attempt | Map contents | Where reTHAWed crashed (`debug.txt`) |
|---|---|---|
| 1 | collision + level script, no parents | first asset, the collision (Type 5): `FATAL ERROR AT 0x0041bfbb`, read of address 0 |
| 2 | texture file -> scene -> level script, collision (parents as in LSE) | third asset, the level script (Type 13, `.nqb`, 348,292 bytes): `FATAL ERROR AT 0x00624fd0`, read of address 0 |

Both crashes are reads through an empty pointer (address 0), so the game is
looking for something it expects to exist and does not find it. Attempt 2
moved the crash one file further, so the scene fix was right; the level script
is the new problem. The collision has not been reached yet.

## What we know about the level script (`.nqb`)

Compared with Los Santos East's (read only, in the cloud):

- LSE's level script has **80 top-level items**: 77 compiled **scripts**
  (item kind 0x07) and 3 arrays (kind 0x0C). Ours has **one** item: the node
  array.
- Named so far among LSE's scripts (by checksum): `salseast_Startup`,
  `salseast_Setup`, `salseast_Goals`, `LoadTerrain`, `LoadCameras`,
  `LoadAllParticleTextures`, `load_level_anims`, `LoadObjectAnims`. Most
  others are still unnamed (probably trigger scripts the nodes call).
- One of LSE's two other arrays is still unnamed (checksums 0xF18AF486,
  0x699EB5C4).
- Our node array's name and header match LSE's form (`<prefix>_NodeArray`,
  kind 0x0C, the pak checksum of the prefix).

## Best guesses (not tested)

1. reTHAWed calls some of those level scripts while loading (for example
   `LoadTerrain` or `<prefix>_Startup`) and crashes when they are missing.
   Fix: add empty versions of the scripts LSE has. Needs the compiled script
   format (bytecode), which we have not decoded; NodeQBC (`D:\Mods\thps-tools`)
   documents it (learn only, no code copied: no licence).
2. Something in our node structs is written in a form THAW doesn't accept
   (for example the `CreatedAtStart` flag item, or `restart_types`), or a node
   needs a field we left out. Test: a script with only the Restart node.
3. One of LSE's other two arrays is required.

## Suggested next steps (need the user's OK before going on)

1. Decode the top-level item kinds and the 8 named scripts in LSE's level
   script; write empty versions of `LoadTerrain`, `LoadCameras`,
   `LoadAllParticleTextures`, `load_level_anims`, `LoadObjectAnims`,
   `<prefix>_Startup`, `<prefix>_Setup`, `<prefix>_Goals`.
2. If it still crashes, try a level script with only the Restart node, to
   split "missing script" from "bad node".
3. Other route: look at the crash address 0x00624fd0 in `THAW.exe` to see
   which name it was looking up (reading the game's code; nothing written).

## Files

- Tool: `tools/gta_to_thaw_map.py` (attempt 2 version).
- Map output: `extracted\thaw_maps\SkateAndreas GTA East LS\` (gitignored).
- Log: `D:\Games\reTHAWed\debug.txt` (overwritten on each run).
- History: `MODLOG.md` (both attempts), `docs/DESIGN.md` section 8.9.
- The map folder is still in `reTHAWed\data\mod\UserMods\Maps\`. It crashes
  only when that map is picked; the rest of reTHAWed is unaffected. It can be
  deleted at any time.
