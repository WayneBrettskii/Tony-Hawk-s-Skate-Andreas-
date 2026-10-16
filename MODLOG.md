# MODLOG

One entry per change, newest at the top. Template: `MODLOG-template.md`.

## [2026-10-15] 8.51 step 4, after test 3: turn test keys (Shift+F2, F1) and a check of whether our turn sticks

**Changed:** `src/thaw/vehscan.cpp` / `.h`, `src/thaw/main.cpp` (VehTestTick gets the skater's facing and speed again). Details in DESIGN 8.51 (test 3 result and the turn test).

**Why:** test 3: no freeze, but the skitch grabs the car's side: the car's turn does not follow the GTA car's.

**Tested how:** compiled for 32-bit Windows (clang + MinGW-w64): no errors. **Not run in game.**

**Turn test 4 (user 2026-10-15 21:27): "the car didn't seem to turn".** The log says why: 1 s after each F1 quarter turn only **1 of the 7** rotation matrices we wrote still held it (7 of 7 at 0 and 360 degrees, which equal the start). reTHAWed rebuilds the other six from a turn it keeps somewhere else, every frame; positions it never rebuilds (they always held). **Change for test 5:** the test car is now car 2 (when the map has three): it stands 225 (or 135) degrees round, so its half-angle sine and cosine are 0.924 and 0.383, easy to recognise. After the scan the plugin looks within 16 KB of each of car 2's position copies for a quaternion (four floats: two about 0, the other two |0.924| and |0.383|) and for an angle in radians (±2.356 or ±3.927), logs every candidate, and from then on turns those too whenever it turns the car (F1, follow mode). The 1 s check then shows whether the turn sticks. (Writing candidates found by value is a small risk of hitting the wrong memory; they are only near the car's own data, and the log lists each one.) Compiled for 32-bit Windows: no errors. Not run in game.

Build fix: the variable `small` is a Windows macro (`char`), renamed; the compile check now defines small, near, far and interface like Windows does.

**Turn test 5 (user 2026-10-15): "it didn't seem to change the position"** (the grab side stayed the same). The search found one candidate, an angle (3.92699 at 0x3725E9C0, near car 2's copy 10); turning it with the car changed nothing: still 1 of 7 matrices held at 90, 180 and 270 degrees. **Change for test 6, a write watch:** after the 1 s check, up to four of the copies that were put back get a hardware write watch (the CPU's debug registers, set on every reTHAWed thread except the plugin's) for 2 s. An exception handler notes each writer's code address, registers and stack, and lets it carry on; writes by our own plugin are only counted. The log then names the code in THAW.exe / rthw.dll that rebuilds the turn, which can be read offline to see where it takes the turn from. The 1 s check also lists which copies held. Compiled for 32-bit Windows: no errors. Not run in game.

**Test 6 result (user 2026-10-15):** no crash; the watch ran on 30 threads. reTHAWed's game thread wrote copy 1's rows every frame (124 times in 2 s) with a 16-float matrix copy at THAW.exe+0x62F0, from a matrix on the stack (0x001AF8D8). Copies 5, 6 and 7 were hit 124 times each at THAW.exe+0x2376A, inside an object copy (THAW.exe+0x23720, it sets vtables 0x68F418 / 0x68F480) whose registers say it was copying FROM the car's data (0x363BAD80, copy 5's rows at +0x20) TO the stack - a read, which a write watch should not see. Copy 10 held the turn. After the window copies 1, 5-7 were back at the car's start turn. **Change for test 7:** from the second F1 on, the watch goes on at the moment of F1 (so a put-back that happens once, right after our write, is caught); each different writer is kept with its registers, the code bytes around it as they are in memory, 40 stack words, and the raw debug status (dr6 / dr7) to settle the read-or-write question. Compiled for 32-bit Windows: no errors. Not run in game.

**Test 7 result (user 2026-10-15):** dr7 = DDDD0055 (four 4-byte write watches) and dr6 names the right watch each time, so both writers really write. The code bytes in memory differ from THAW.exe on disk (the file is packed or patched; disk disassembly there is junk). In memory, THAW.exe+0x23760 is a set-matrix (this = ecx, copies 12 floats from the argument into this+0x20); THAW.exe+0x62F0 copies a matrix. Both take the same matrix on the stack (0x001AF8D8), rebuilt every frame (first write 62 ms after the watch went on, then about 61 a second), from callers returning to THAW.exe+0x2937F (then +0x4CE9B) and +0x109E28 (then +0xD70C2). F1's own writes were counted (4) and then overwritten within a frame. **Change for test 8:** after each watch the plugin saves the code around every THAW.exe return address on the writers' stacks, as it is in memory (0x700 bytes each, read only), to extracted\veh_code\ (untracked), so the code that builds that matrix, and what it reads the turn from, can be read offline. Compiled for 32-bit Windows: no errors. Not run in game.

**Test 8 result (user 2026-10-15):** 14 pieces of code saved and read offline. The car's parts are rebuilt every frame by THAW.exe+0x4CD60 (a component update: this+0xC = the car object, this+0x18 = its model). It finds a component (checksum 0x286A8D26) and from it a local offset and turn, then gives the model position = object+0x68 + object turn x offset, and turn = local turn x object turn, where the **object turn is at object+0xDC** (rows at +0xDC, +0xEC, +0xFC); THAW.exe+0x29360 passes that on to each part (copies 5-7) and the draw code (THAW.exe+0x4D0D60 from +0x4D70C2) to copy 1. In this run the object was at 0x363B7460: its position object+0x68 is car 2's copy 2, so the object's turn is 0x74 bytes after a position copy, which the scan never looked at. **Change for test 9:** a copy with no rotation right before or after it is also checked 0x74 bytes on; one found there is marked "the object's own turn" and is turned with the rest (F1, follow mode, the 1 s check, the watch). The scan now lists the test car's copies. If the object turn holds, the parts follow it on the next frame; if it is put back too, the watch shows what writes it. Compiled for 32-bit Windows: no errors. Not run in game.

**Test 9 result (user 2026-10-15): "the car did not move".** The scan found the object's own turn (car 2's copy 3, object 0x363B9460, turn at +0xDC), but the watch shows that is also written every frame: THAW.exe+0x62F0 copies it from **object+0x88** (ecx = 0x363B94E8), called from THAW.exe+0x5B960, the end of a component update (this+0xC = the object) that also moves the object's height (object+0x6C) and sets the object's matrix from its +0x88. **Change for test 10:** for the copy with the object's own turn, the rotation 0x20 bytes on (object+0x88) gets its own entry ("the turn the object's is copied from") and is turned with the rest; when turns are put back, these two go first in the 4-place watch list. Compiled for 32-bit Windows: no errors. Not run in game.

**Test 10 result (user 2026-10-15): "Car physically turned it still skitched the same side then rethawed crashed when I let go".** With object+0x88 turned too, all 9 turns held (90 to 630 degrees) and the car turned on screen. reTHAWed's own skitch still grabbed the same side, so what it uses (most likely the car's own rails, the .rnb packed with it) keeps its own copy of the car's turn. About 9 s after the last F1 the freeze came back: the game thread stopped and the watchdog sampled it at THAW.exe+0xE343 / +0xE702 / +0xE95D, called from the script interpreter (+0x13796, +0x130D9, +0x12C0B); the "current script" global then held 0x358637BD (an odd address, all zeros). reTHAWed then closed with 0xCFFFFFFF and GTA with an access violation. **Change for test 11 (read only):** (1) a twin scan: after a turn that held, one pass through all of reTHAWed's memory for the test car's start turn in each form its copies had (rows of 4 or packed rows of 3, plain or turned over), skipping the known copies; up to 40 places logged with their offset from the car object. (2) The watchdog saves the code where the game thread is stuck, and the THAW.exe addresses on its stack, to extracted\veh_code\ (samples 1 and 3). Compiled for 32-bit Windows: no errors. Not run in game.

**Test 11 result (user 2026-10-15):** no freeze this time (no watchdog lines). The twin scan was wasted: its checks were written as "reject if further than 0.01", and any comparison with NaN is false, so memory full of NaN (at 0x001A88A8 onwards, the first stack-like region) matched every form and filled the 40-place list at once. **Fix for test 12:** every check in the twin scan is now written as "accept only if within", so NaN never matches (checked with a small program: NaN rejected, 0.705 against 0.7 accepted). Compiled for 32-bit Windows: no errors. Not run in game.

**Test 12 result (user 2026-10-15): "it looked like the same side".** The twin scan (4.1 s, NaN fixed) found **no** place in reTHAWed's memory still holding the car's start turn after a 90-degree turn. So the skitch does not read a stale turn. Left: (a) the car's own rails are kept as world points, moved with the car but never turned; (b) the skitch picks the side from the car's motion, which our moves never set. **Change for test 13 (read only):** a near-point scan right after the twin scan lists up to 64 float triples within 4 m across and 2.5 m up or down of the car (not its known copies; ground-height points only if they sit in a rail-like record), each as an offset along the car's start nose / side / up and along its turned nose / side, with a rail-record guess (links at +0x50 / +0x54 of a 0x60 record, the point at +0x20). Rail points still lined up with the start frame would be (a). Compiled for 32-bit Windows: no errors. Not run in game.

**Test 13 result (user 2026-10-15; he also skitched once by accident):** twin scan 0 again; near scan (4.1 s) found **no** float triple within 4 m of the car in world space apart from its known copies. So the car's rails are not kept as world points: they are local to the car and follow its turn. If so, "the same side" would be the same side **of the car**: reTHAWed's grab point (the back of the vehicle model) sits on the side of our model as we stand it, which is a nose offset (what F1 in follow mode is for), not a turn that fails. Asked the user which it is.

**Test 14 (user 2026-10-15): "rethawed crashed again but I hit f1 more than once. I don't think the skitch point of the rethawed is in the right spot"** (seen in reTHAWed's own window: the skater holds the car's left side, not its back). Three F1 presses (90-270 degrees), all 9 turns held; about 8.5 s later the game thread froze. The watchdog's code (saved from memory, read offline): the thread sits in THAW.exe+0xEF50, a script **bytecode reader** (it compares the byte at esi with 0x4A, 1, 2, 0 ...; called via +0x12C0B, +0x130D9, +0x13796 from the script update), with esi = 0x18085AEA in all four samples: it is looping on one spot of a script and never moving on. The "current script" global again held 0x358637BD (zeros), so which script is not known yet. Every freeze so far followed skitching the proxy car (follow mode or turned). **Decision asked of the user** before more work: keep digging (next: dump the script bytes at esi to name the script; the grab point's cause is still open) or go back to polishing the 8.42 tow.

**Test 15 plan (user 2026-10-15 chose "a": one more cheap round, then decide):** (1) on the first freeze sample the watchdog saves the script bytes around esi (0x1000 bytes), and 0x400 bytes around ebx, edi and eax, to extracted\veh_code\ (read only), and logs the 64 bytes at esi; the script's checksums can then be named offline. (2) While the skater is within 6 m of the test car (follow mode off), twice a second the log gives his offset in the car's own frame (object turn at +0xDC: row 0 right, row 2 nose) and which way he faces, so the grab point is measured, not judged by eye, before and after F1. Decision after this test: keep the proxy route if the frozen script can be avoided, otherwise polish the 8.42 tow. Compiled for 32-bit Windows: no errors. Not run in game.

**Test 15 result (user 2026-10-15: "rethawed crashed").** Only Shift+F2 and a skitch, **no F1**: the freeze does not need a turn. (1) **Grab point, measured:** while skitching, the skater stayed 93 inches behind the car's centre along its nose row and 42 inches to its left, facing the nose: the back-left corner (the car is about 4 m long), i.e. behind the car, just outside its left edge. That is a plausible skitch hold, not the side door; what looked like "the left side" may be right. (2) **Freeze, decoded offline** (script_1808F2EA.bin): at the release the skater's facing flipped and ~2.5 s later the watchdog fired. The stuck script is reTHAWed's skater animation helper (its strings: "skater_playanim received invalid value for param from", "you cannot use frames and percent at the same time"). The stuck line is `IF (BED84CA4 = 1) <name> = 1`, where `<name>` is a direct reference (bytecode 0x46) to the symbol entry at THAW.exe 0x00719AE8 (ebp in every sample). The reader treats that name as a call and parses "= 1" as its parameters: THAW.exe+0xEF50 calls +0xE340 on the '=' byte (0x07), which does not move on, so it loops forever. So at the release that symbol is no longer a plain value; why is not known (not our writes as far as we can tell: it is in THAW.exe's own data, which none of our copies are in). Checked against the user's rule for this round (keep going only if the frozen script can be avoided): the animation script runs on every release, so it cannot be avoided; only that symbol might be fixable. Put to the user: switch to the 8.42 tow, or one more test that saves the symbol entry at 0x719AE8 to name it.

**Test 16 plan (user 2026-10-15: "there has to be a way, we were able to skitch earlier and let go without a freeze").** True: Try 3 (no plugin writes) and tests 3, 11 and 13 skitched without a freeze. So something changes the entry at THAW.exe 0x719AE8. **Symbol watch:** once the vehicle scan is done (THAW.exe at 0x400000 only), the 0x40 bytes from 0x719AE0 are logged, and the 4 hardware write watches go on 0x719AE8..0x719AEF on every reTHAWed thread for the rest of the level (the turn watches stay off; all turns hold now). Each new writer is logged within a second: code address, registers, the THAW.exe addresses on its stack, the code around them saved to extracted\veh_code\, and the entry's bytes after the write. The watchdog also logs the entry's bytes at a freeze. Compiled for 32-bit Windows: no errors. Not run in game.

**Test 16 (user 2026-10-15/16, two runs: "I couldn't get it to freeze").** The first run used the test 15 plugin (not rebuilt); its log still measured the grab: the skater held the car's back-left **or** back-right corner (93 in behind, 42 in to the side, facing forward), whichever side he came from, before and after turns: reTHAWed's normal skitch. **The grab point is right.** The second run (rebuilt) watched the entry at THAW.exe 0x719AE8 all session: **no writes**. Its bytes at the start: name checksum 0xF8CFD515, then 0x0A DA4E74, then **0x0042EDB0, an address in THAW.exe's code**; the next entries (0x719B00, ...) look the same, 24 bytes apart. So it is reTHAWed's table of built-in C functions, and the entry never changes. **Cause of the freeze:** reTHAWed's skater animation script has the line `IF (<BED84CA4> = 1) <that function> = 1`: a built-in function's name used as a setting. When the line runs, the reader takes "= 1" as the function's parameters and never moves past the '=' (THAW.exe+0xEF50 / +0xE340): the game thread hangs. It runs only when that parameter is set, which is why letting go froze only sometimes. In the saved 4 KB of script it is the only such line. **Fix (8.51 script fix):** once per run of reTHAWed, after the vehicle scan, the plugin searches reTHAWed's private memory for `01 46 <address 0x700000-0x7FFFFF> 07 <16|17|1A>` (a line that is a direct function reference followed straight by '=' and a 5-byte value) and, on the game thread, overwrites those 11 bytes with 0x01 (new line), which the reader skips; the IF's jump length is unchanged. Nothing on disk changes. Logged: how many found, each address, the function entry's name and code address, and how many blanked. Checked offline on the saved bytes: exactly one line found (0x1808FAE5), the IF ... ENDIF left whole. The symbol watch is switched off (kSymWatchOn). Compiled for 32-bit Windows: no errors. Not run in game.

**Test 17 (user 2026-10-16): PASSED - "did not freeze".** thaw.log: `script fix: 1 line(s) ... found in 15078 ms` at 0x18087AE5 (function entry 0x719AE8, name F8CFD515, code 0x0042EDB0, bytes 01 46 E8 9A 71 00 07 17 01 00 00 00), then `blanked 1 of 1`. The user skitched and let go many times over about 2 minutes with the car at 0, 90, 180 and 270 degrees (181 position lines while he was at the car): no freeze. Turning, the grab point and letting go all work. (The search is a byte-by-byte pass and takes ~15 s; it runs once per run of reTHAWed, on its own thread.) **Next (not started, needs the user's go-ahead):** smooth following of GTA cars (follow mode writes one place per pass; the car's own speed is never set), then a pool of proxies for the cars near CJ.

## [2026-10-15] 8.51 step 4, after test 2: reTHAWed freezes in a script loop; find the script and the car's real turn

**Changed:** `src/thaw/vehscan.cpp` (watchdog records the running script; turn comparison of the three cars), `tools/gta_to_thaw_map.py` (`PROXY_TURNS`: cars stand at different turns). Earlier the same evening: the watchdog itself, holding the followed GTA car while the skater is within 10 m, no parking under him (`bmx.cpp` leaves `thawSkitchId` to vehscan in follow mode).

**Why:** test 1 and 2 froze reTHAWed while skitching a following car; the watchdog showed the game thread looping in reTHAWed's script interpreter. The car's turn did not follow ours. DESIGN 8.51.

**Tested how:** compiled for 32-bit Windows (clang + MinGW-w64): no errors. Map tool: the three proxy nodes come out with Angles yaw +0, +90 and +45 degrees on the copied record's (checked by reading the written level script). THAW.exe disassembled read only in the cloud (llvm-objdump) to read the watchdog addresses. **Not run in game.**

**Next:** user rebuilds the map (proxy command) and the plugin, tries follow mode once more; the log then names the looping script and where the car's turn lives.

## [2026-10-15] 8.51 step 4: a reTHAWed vehicle follows the nearest moving GTA car (F2), for reTHAWed's own skitch

**Changed:** `src/thaw/vehscan.cpp` / `.h` (follow mode, rotation rows, game-thread writes), `src/thaw/input.cpp` (calls `VehGameThreadTick`), `src/thaw/bmx.cpp` (our 8.42 skitch steps aside while follow mode is on), `src/thaw/main.cpp`. Details in DESIGN 8.51.

**Why:** user 2026-10-15: step 3 passed ("I was able to spawn an invisible car and skitch"); the skitching plan's next step.

**Step 3 result:** 11 copies per car found; all stayed where F2 wrote them; skitched there; no crash.

**Tested how:** compiled for 32-bit Windows (clang + MinGW-w64 headers): no errors in vehscan, main, input, bmx. **Not built with MSVC, not run in game.**

**Next:** user test: F2 on, get behind a moving GTA car (within 8 m), Up; F1 if the grab only works from the side or front.

**Step 4, test 1 (user 2026-10-15 20:55): "CJ skitched the side of a car for a second, stopped following it, then reTHAWed crashed."** From `thaw.log`: follow mode wrote car 0 onto GTA cars 50-60 times a second, 0 copies skipped. At 89.0 s it took GTA car 22 (turned 94 degrees); from 89.25 s the skater's own speed reads 0 while he travels about 1,200 units in 1.7 s with the car: **reTHAWed's real skitch towed him** - from the car's side, so the nose guess is 90 degrees out (F1 was not used). At 91.0 s the skater stops, and from then on reTHAWed's game thread never came through the controller hook again (0 moves written; the skater's numbers frozen): reTHAWed **froze** (no crash report in `debug.txt`, which ends with ordinary skater lines). Cause not known yet.

**Changes for test 2:** (1) a **watchdog**: when reTHAWed's game thread has not run for 2.5 s in our level, the plugin stops it for a moment, copies its registers and the top of its stack, lets it go, and logs where it is (module + offset; 4 samples 0.4 s apart), to find the loop it hangs in. (2) While the skater is within 10 m of car 0, the plugin holds the GTA car it follows (`thawSkitchId`, so the GTA side keeps publishing that car out to 16 m instead of switching to another passing car under him). (3) Car 0 is no longer sent back to its own spot while the skater is within 10 m of it. (4) Our 8.42 skitch no longer clears `thawSkitchId` in follow mode.

## [2026-10-15] 8.51 steps 2-3: find the map's reTHAWed vehicles in memory; F2 moves one in front of the skater

**Changed:** new `src/thaw/vehscan.cpp` / `vehscan.h` (in `CMakeLists.txt`), called from `src/thaw/main.cpp` (`VehScanSecond` once a second, `VehTestTick` every pass). `tools/gta_to_thaw_map.py` writes `_railpool\proxies.csv`. Details in DESIGN 8.51.

**Why:** user 2026-10-15 ("yes" to steps 1 and 2 of the skitching plan, after the parked reTHAWed car was skitched).

**Tested how:** compiled for 32-bit Windows (clang + MinGW-w64 headers), no errors; `main.cpp` too. Not run: the search needs reTHAWed's memory. **Not built with MSVC, not run in game.**

**Next:** the user rebuilds the map with proxies, builds, presses F2 in the level, tries to hit and skitch the moved car, sends `logs\thaw.log` and `debug.txt`.

## [2026-10-15] 8.51 skitching route A, step 1: vehicle model files go into the map

**Changed:** `tools/gta_to_thaw_map.py`: with `--proxy-cars`, the map's pak now also carries the copied vehicle's model files (`.qb` as type `.mqb`, `.mdl`, `.rnb`), read from the player's reTHAWed folder at build time (`vehicle_model_assets`). Nothing is copied into the repo.

**Why:** user 2026-10-15 ("Let's start with skitching"). The proxy test map crashed in `CRailManagerComponent::InitFromStructure` (DESIGN 8.51). reTHAWed's own level paks carry these files for every vehicle (`z_el.pak`, read only); ours did not.

**Tested how:** in the cloud: the three entries written from the real `veh_pickup` files and read back: types, full names, file name and parent as in `z_el.pak`. **Not loaded in game yet.**

**Next:** the user builds the pool map with `--proxy-cars 3 --thaw-dir D:\Games\reTHAWed` again and loads it.

**Try 2 (user 2026-10-15): crashed earlier, while reading the pak** (`debug.txt`: right after opening `MPGTALSE.pak.Wpc`, access violation reading 0 at 0x0041C36A, before `zone_init`). Read again, z_el.pak's full set for one vehicle is five entries in a row: `.stex` (the model's `.tex`), `.mqb`, `.mdl` (whose parent is that `.tex`), `.rnb`, `.mcol` (the model's `.col`). Try 2 had no `.tex`, so the `.mdl` pointed at a missing parent. **Try 3:** all five, in that order. Checked in the cloud by building veh_el_sign's five entries with the tool and comparing them with z_el.pak's: types, names, parents, flags, sizes and file bytes identical; only the `.mqb` header's second name hash differs (z_el's is not the qbkey of any name tried; our own level script's entry has always differed the same way and loads).

**Try 3 (user 2026-10-15): PASSED - "I could skitch the invisible car".** The map loads normally (load over at 21.6 s, the rail pool as before), and the skater skitched one of the three parked reTHAWed cars by the skatepark - reTHAWed's own skitch, since our 8.42 skitch logged only "Up pressed without a grab: no moving car in reach" (it only grabs moving GTA cars). So reTHAWed vehicles work in our map; GTA does not draw them yet. Note for later: both skitches answer Up; once the proxies follow GTA's cars, our 8.42 grab must step aside.

## [2026-10-15] 8.49 drop counter + 8.50 natas everywhere

**Changed:** `src/thaw/railscan.cpp`: (8.49) a grind that ends near a point is sorted into ollie / bail / short touch / real DROP from the skater's speed and vertical speed in the 0.3 s after; (8.50) the pool's one-point rails are natas spares, filled with the natas points within 100 m from `natas_all.csv`. `tools/gta_to_thaw_map.py`: (8.50) pool maps write `natas_all.csv` (every natas-able prop's top, plus pointed landmark apexes such as the pyramid, `pyrmd` / `pyramid` in the model name; `gant` models are listed for later) and add `--rail-pool-natas` spare one-point rails (default 300).

**Why:** user 2026-10-15 ("go ahead on both"). DESIGN 8.49, 8.50.

**Tested how:** in the cloud. Plugin compiled for 32-bit Windows (no new warnings); Linux harness with a fake block of 24 rails + 3 one-point spares: 20 / 20 checks (8.48's plus natas placement / parking and the three drop-counter cases). Map tool on made-up collision: spares written and read back; point merging and the apex test on made-up shapes. **Not run on GTA's files or in game.**

**Next:** the user rebuilds the whole map and the plugin, natas-spins around the map, sends the build output's `natas:` / `landmark:` lines and `logs\thaw.log`.

**Result 1 (user 2026-10-15 19:27): the map with 300 natas spares never finished loading.** CJ fell through the map in GTA because reTHAWed's skater never spawned (stuck at its pre-spawn spot for 68 s; in the 17:56 run he spawned after 13 s). reTHAWed's `debug.txt` stops right after `zone_init: mpgtalse` / `Ped avoid hack disabled`. Our scan still found the 16,584 rail records at 30 s, but with **no links at all** (every record looked like a one-point rail), so reTHAWed stopped somewhere between making the rail records and linking them. The map tool found 8,328 natas points; `rethawed.ini` [Memory] is high enough (RailString 10,800, RailPoint 266,032). The only change from the map that loaded is the 300 one-point rails (the 8.45 test map with 2 loaded fine), so many one-point rails seem to stall reTHAWed's rail setup. Fix for now: `--rail-pool-natas` defaults to 0 (the launcher's map builds as at 8.48 again); next: find how many one-point rails load (20, then more).

**Result 2 (user 2026-10-15 19:38 and 19:48):** with `--rail-pool-natas 20` and then `100` the map loads normally (load over at 21.6 s, as before), and the posts follow the skater (with 100: up to 43 natas points within 100 m, all placed). The most natas points within 100 m of any spot in the whole map is 83 (8,328 points: telegraph poles 1,533, lamp posts ~4,300, traffic lights ~1,580, bollards ~890, hydrants 46, road signs 26; the pyramid apex found at GTA 2323.7 1283.2 96.7). So the default is now **100** spares. reTHAWed seems to have a limit on one-point rails somewhere between 100 and 300; not narrowed further. The 8.49 drop counter in this run: 11 short touches, 0 drops.

## [2026-10-15] 8.48 long rails built into the pool map

**Changed:** `tools/gta_to_thaw_map.py`: a pool map (`--rail-pool N`) now also holds long spare rails of 3-16 nodes (`--rail-pool-long`, default `auto`: 1,006 rails, 7,284 nodes, sized from the whole map; `none` = as before). `src/thaw/railscan.cpp`: finds pool rails of any length by walking their links; cuts each GTA rail into pieces of at most 16 nodes; puts a piece whole into a free pool rail of exactly its length (else segment by segment into two-node ones, as before); writes only positions and boxes, never links. The 8.47 run-time chaining is removed. The 10 s log line now splits "inside long rails" from "joins between slots". The pool switches off if the map's rail or node count doesn't match `rails_in_map.csv`.

**Why:** user 2026-10-15 ("Okay let's build the map"), after 8.47 showed reTHAWed ignores links changed after loading (DESIGN 8.48).

**Tested how:** in the cloud. Map tool on made-up collision: the long rails come out linked (read back from the PAK: 111 segments for 50 two-node + 9 long rails). Plugin: compiled for 32-bit Windows (no new warnings); on Linux against a shuffled fake block of 24 pool rails: 14 / 14 checks (DESIGN 8.48). **Not run on GTA's files, not built with MSVC, not run in game.**

**Next:** the user rebuilds the whole map and the plugin and does the grind test.

**Result (user 2026-10-15 17:58, thaw.log, ~9 min, rebuilt whole map):** the map loads (5,506 pool rails / 16,284 nodes found, matching the file; long slots 3-16 nodes all present), no crash in reTHAWed, nothing refused. **Long rails work:** 136 points inside long rails ridden through. The log flagged 11 'DROPPED INSIDE a long rail', but checked one by one against the skater's speed and height: 4 were ollies off the rail (upward speed +350..+400), 3 were the skater rolling on the ground past a kerb-height rail (no height change, steady speed: the detector's false alarm), 3 were bails right after landing on the rail (on it 0.2 s, speed falling fast), 1 may be real (359.7 s, nearly straight point, 3 degrees). Joins between separate slots: 6 carried, 2 'dropped' (one steering off, one nearly stopped). So drops at joins went from about 1 in 9 to about 1 in 140 or fewer. Side notes: the first short run (17:56) had reTHAWed not polling the controller, so the 2 s fallback wrote the rails from our thread, as designed; GTA's exit code 0xC0000005 on closing is the same as in every earlier session (not new). Next (not started): make the join log ignore ollies and ground rolling so it counts only real drops.

**Also (same day):** the launcher found no Python to build the map (since 8.44 it only uses `tools\python` or `PythonExe=`; this PC's Python 3.11 in `C:\Program Files\Python311` is not on PATH). Added `PythonExe=C:\Program Files\Python311\python.exe` to `build\Release\SkateAndreas.ini` (local settings, not in git); the map then built.

## [2026-10-15] 8.47 real long rails: pool pieces of one GTA rail are linked into one reTHAWed rail

**Changed:** `src/thaw/railscan.cpp`. The write queue now writes single rail nodes (position, box, next and previous links) instead of whole two-point rails. After each pool fill, placed pieces that continue the same GTA rail are linked into a chain (`a -> a -> ... -> a -> b`), the spare `b` records of the inner pieces are parked with no links, and a slot that leaves a chain gets its plain links back. Unchanged chains are not re-written. All writes of one fill are queued as one batch and applied together on reTHAWed's game thread; if any record in it looks wrong, none of it is written and the pool switches off for that level load (logged). The fill log line now ends `N chains of M pieces linked (K link writes)`. Switch: `kChainRails` in `railscan.cpp` (true).

**Why:** user 2026-10-15 ("go ahead"). The 8.46 join test showed about 1 grind in 9 dropping where two pieces of one rail only touched (DESIGN 8.47).

**Tested how:** in the cloud. Compiled for 32-bit Windows (clang + MinGW-w64 headers): no errors. The real `railscan.cpp` on Linux against a fake block of 8 rail records: 25 / 25 checks pass (chain and back links right, inner `b` records parked, single rail stays plain, an unchanged refill writes nothing, the chain breaks and comes back as the skater rides away and back, a bad record refuses the whole batch, all 8.46 checks). **Not built with MSVC, not run in game.**

**Next:** the user builds, grinds long kerbs / ledges / walls on the whole-map pool map as for 8.46, and sends `logs\thaw.log`. Pass: `DROPPED at a join` near 0, no crash or NaN, grinds still work after riding far.

**Result (user 2026-10-15 14:00 + 14:03, thaw.log, ~5 min of grinding): safe, but the drops did not go away.** The user felt it was better. Chains were made every fill (e.g. 87 chains of 441 pieces), ~20,000 node writes per 10 s all on the game thread, 0 fallback, 0 dropped, 0 refused, no NaN, no crash. Joins: 163 carried across, 24 DROPPED (12.8%; 8.46 had 10.2%), 13 rail ends, 21 left mid-rail. Every DROPPED join was between two placed pieces of one rail, so it was inside a chain. In the one looked at closely (99.6 s), the skater went straight on past the join at the same height and speed, then bailed: reTHAWed did not follow the new link. So it seems reTHAWed does not use the records' next / previous links for a grind it is already on (perhaps its own per-rail list, the 'RailString' risk). Next: find what reTHAWed really follows (waits for the user's go).

## [2026-10-15] 8.46 rail pool: writes on reTHAWed's game thread, and the join test

**Changed:** `src/thaw/railscan.cpp`, `railscan.h`, `src/thaw/input.cpp`. The pool fill and the car rails no longer write reTHAWed's rail records from our own thread: they queue each write (newest per rail wins, so the queue never outgrows the pool), and `RailPoolGameThreadTick`, called from the controller hook on reTHAWed's game thread, applies them between frames without ever waiting. Before each write it checks that reTHAWed is still in our level, that it is the same level load, and that both addresses are rail records inside the found block; otherwise the write is dropped and counted. If the game thread takes nothing for 2 s, our thread applies the queue itself (logged once). New join test (log only): each two-point piece knows whether the next one continues the same GTA rail, and the log tells "carried across a join", "grind DROPPED at a join" (with where and how fast), real rail ends and jumps off mid-rail; every 10 s a `rail pool (10 s):` line sums joins and rail writes. The F5 / F4 debug rails still write directly.

**Why:** user 2026-10-15 ("Let's start step 1", research doc). Writes from a second thread could leave a rail half-written while reTHAWed checks grinds, or land after the level was freed; and long rails' joins were never tested (8.32 fix 3). Natas points and wires will use the same pool.

**Tested how:** in the cloud. Compiled for 32-bit Windows (clang + MinGW-w64 headers): no new warnings. The real `railscan.cpp` was also run on Linux against a fake block of 8 rail records in low memory and a made-up rail list (one 3-piece rail, one single piece): 14 / 14 checks pass - nothing written before the game-thread tick; then all 4 rails placed; a grind along the 3-piece rail = 2 joins carried + 1 rail end; falling off at the first join = DROPPED; jumping off mid-piece = left mid-rail; a write from an old level load, to a non-rail record, or while in the menus is dropped and the record untouched; a second write to the same rail replaces the first; the fallback writes after 2 s without the game thread. **Not built with MSVC, not run in game.**

**Next:** the user grinds long kerbs / ledges / walls on the whole-map pool map and sends `logs\thaw.log` (the `rail pool (10 s):` and `DROPPED` lines). If joins drop: real multi-point rails in the pool (re-linking records).

**Result (user 2026-10-15 13:46, thaw.log, ~2 min of grinding): the queue works; joins drop about 1 in 9.** Rail writes: ~10,500 per 10 s, all applied on reTHAWed's game thread; 0 by the fallback, 0 dropped, 0 refused (no crash, no NaN). Joins: 79 carried across, 9 DROPPED (one at speed 1: he had stopped), 3 rail ends, 11 left mid-rail. User: "some grinds stopped". The dropped joins (checked against `all_rails.csv`) are ordinary ones: turns of 1-17 degrees and slope changes of about 2 degrees between pieces 1-28 m long, the same as the 59,060 joins in the map (median turn 9.5 degrees). So no single shape causes it: reTHAWed mostly jumps from one unlinked rail to the touching next one, but not always. Fix: real multi-point rails (re-link the pool records into chains), waiting for the user's go.

## [2026-10-15] 8.45 Natas test map: one-point rails on real props near the skatepark

**Changed:** `tools/gta_to_thaw_map.py`: new `--natas-test`. While reading GTA's placements it notes every prop of a natas-able model (`NATAS_MODELS`: hydrants, parking meters, bollards, lamp posts, traffic lights, telegraph poles, road signs) with the top centre of its collision box (or a table height when it has none). With `--natas-test` the nearest 12 within 80 m of the skatepark start become **one-point rails** (one RailNode, no Links), listed in `natas_points.csv` with their GTA positions and printed. The shared `_railpool` rail lists are left alone for this map.

**Why:** user 2026-10-15 ("go ahead", research step 1). reTHAWed does Natas on single-point rails (its own menu text and park editor; DESIGN 8.45); the 2026-10-12 tests only ever used two-point rails.

**Tested how:** in the cloud, a made-up map (a flat square + fake hydrant / meter / far lamp post): the two near props became two rail nodes with no links ("0 rail segments" when read back with `tools/thaw_level_dump.py`), at the right THAW positions; the far one was left out; no `_railpool` files written. Los Santos East read back the same way holds 13 one-point rails, so reTHAWed loads them. **Not run on GTA's files or in game.**

**Next:** the user builds the test map, tries a Natas on a listed prop, and reports.

**Follow-up (same day):** the first build (East LS) listed only traffic lights, lamp posts and telegraph poles (6-7 m tall, no hydrants) near the skatepark: nothing to ollie onto (user). Added `--natas-at X Y` (test centre anywhere in the map's `--box`; refused when outside it), `--natas-low` (only props up to 2.5 m tall) and `--natas-post X Y Z` (a hand-placed point, e.g. a hydrant top found with F9); the build now prints how many of each natas model the whole area has. Checked in the cloud with made-up props (a hydrant and a hand-placed post kept, a lamp post and a far meter left out, an outside centre refused).

**Result (user, 2026-10-15): PASSED - "I was able to natas spin on the grove street hydrants".** The Grove Street build (`--natas-low --natas-at 2495 -1670`) found the two hydrants by their collision (`fire_hydrant` at GTA 2505.67 -1657.93 13.49 and 2495.27 -1653.66 13.48) and put a one-point rail on each; CJ Natas-spun on them in GTA. So a one-point RailNode is all reTHAWed needs, and the finder places them right. F9 / `--natas-post` was not needed.

## [2026-10-15] 8.44 security and privacy audit fixes (F1-F6 + N1-N4)

**Changed:**
- `src/common/link.h`, `src/gta/main.cpp`, `src/thaw/main.cpp`, `src/thaw/bmx.cpp` (N1): every number read through the link is checked for NaN / infinity. A bad skater position or speed makes the sample unreadable (skate / BMX mode skip it like a torn read); bad pose, body or bike numbers clear only their own flag; a bad car rail, skitch car or place request is ignored. Both logs say how many were dropped (`link: dropped ...` / `link: ignored ...`). Layout unchanged (still v17).
- `src/launcher/main.cpp` (N2): the `SkateAndreas.asi` check finds every copy GTA could load (Mod Loader folders 3 deep, `scripts`, GTA's own folder) and compares it byte for byte with the one next to the launcher: amber if different or if there are several; the log lists each copy. The launcher still never writes into the GTA folder.
- `src/launcher/main.cpp` (F4, N4): `FindPython` runs only `tools\python\python.exe`, or a `PythonExe=` full path from `SkateAndreas.ini`, each by full path as the program name (no `python` / `python3` / `py` search), from the mod folder; a probe that hangs is stopped after 10 s. Command-line paths are quoted with `QuoteArg` (a trailing `\`, e.g. `D:\`, no longer swallows the quote).
- `src/gta/gps.cpp` + new `src/gta/roadfile.h` (F2): road-file sizes are compared in 64-bit before reading; every read is bounds-checked; a damaged `NODES*.DAT` is skipped whole and logged. New `src/tests/parser_tests.cpp` (CMake target `parser_tests`): 15 made-up cases incl. the audit's `n = 0x40000000` file.
- `src/gta/tags.cpp` (F2b): `gta3.img`'s entry count is checked before the list is reserved; reads use 64-bit offsets.
- `STATUS.md` (F1): the two user-profile paths are now `%LOCALAPPDATA%\Temp\opencode\...` (history not rewritten, by the user's choice).
- `src/common/paths.h`, `CMakeLists.txt`, `src/thaw/railscan.cpp`, `src/gta/radio.cpp`, launcher (F1b): `MP_LOG_DIR` may be empty (release builds); then, without the launcher, logs go to `%LOCALAPPDATA%\SkateAndreas\logs` and the rail pool says there is no extracted folder. The launcher now passes reTHAWed's folder to the games (`SKATEANDREAS_RETHAWED_DIR`); the radio wheel's reTHAWed logo is read from there (before, only from the build PC's folder, so players never got it).
- `tools\make-release.ps1` (F3, F5, F6, N3): refuses uncommitted code unless `-Candidate`; fresh `build-release\` with `GTA_DEPLOY_DIR` and `MP_LOG_DIR` empty and generic game-folder defaults; runs `parser_tests` and `convert_test`; refuses binaries containing a user-profile path; Python **3.14.8** embeddable downloaded into the build folder and its SHA-256 (from python.org's release page) checked before unpacking; `--help` run with it; `BUILD-INFO.txt` + `SHA256SUMS.txt` in the package; `release\SkateAndreas-<commit>.zip` + `.sha256`.
- `tools\build-plugin-sdk.ps1` (F3): the SDK commit comes from `git rev-parse HEAD` and must match, and tracked files must be unchanged (no added programs or scripts), before `premake5.exe` runs; otherwise it stops.
- `.gitignore`: `/build-release/` and `/release/` blocked explicitly. `INSTALL.md`, `README.md`: Python and `.asi` notes; how releases are made and checked.

**Why:** user 2026-10-15: "start with the patches we need to make based on the audit" (a friend's audit PDF of `d4ca7f6`; plan in DESIGN 8.44 and the doc "SkateAndreas security fixes plan").

**Tested how:** in the cloud only. `parser_tests` built and run (64-bit Linux): 15 / 15 pass. The link checks run with made-up NaN / infinity values: NaN position refused, infinity in the body clears only the body flag, a NaN car rail gives 0 cars. Every changed C++ file compiled for 32-bit Windows (clang with MinGW-w64 headers + libc++): no new errors or warnings (the only errors are in plugin-sdk's own headers and the same on the unchanged code). Both PowerShell scripts parse (PowerShell 7.4); the release script's git / path / checksum steps run against a test repo. **Not built with MSVC, not run on Windows, not run in game.**

**Still to do / not tested:** a build with MSVC and a run of `parser_tests.exe`, the launcher and the games on Windows; the map built with Python 3.14 must be compared with the 3.11 one; the plugin-sdk's own source file names (inside `Plugin.lib`) still name the development folder in the `.asi` (the release script notes it; only a rebuild of plugin-sdk with path trimming would remove it). The clean release (F6) needs a commit: the user's call.

## [2026-10-14] 8.35 road 3 fix 2b: unmute reTHAWed only after the map has finished loading

**Changed:** `src/thaw/autostart.cpp`: the "focus gained" event (fix 2, which turns reTHAWed's sound back on) is now sent only after we have been in our level for 8 s **and** reTHAWed's window thread has drawn frames steadily (no gap over 150 ms) for 3 s. A line `autostart audio: in our level N s, frames steady N s: the load is over` marks the moment.

**Why:** user 2026-10-14: "the launcher isn't turning on skate mode in san andreas". `logs\thaw.log` (01:47) and `thaw.prev.log` (01:44) both stop the same way: `change_level` runs, the level ID changes, then reTHAWed makes no new frames for the rest of the run (`new reTHAWed frames in the last second: 0`), the skater never moves, and autostart logs nothing more (not even `1 s after`). GTA's L3+R3 then waits for a skater that never comes (`gta.log`: "cancelled while waiting for reTHAWed's skater"). The 23:52 run (fix 1, before fix 2) loaded in about 1.5 s. Fix 2 checked every 0.5 s and pushed the SDL event as soon as the level ID was ours, which is while the map is still loading; that push never returned. It worked in the user's one radio test only because that push landed after the load.

**Tested how:** syntax check (clang, i686 Windows target, stand-in headers). **Not built, not run in game.**

**Still broken / not tested:** the reason SDL_PushEvent hangs during a load is not known (most likely reTHAWed's event handler and the loader wait on each other). The radio now comes on about 8-10 s after the map loads, not at once. The release zip and GitHub copy still have the old fix 2.

## [2026-10-14] 8.42c vehicle proxies in the map: a copy of a THAW vehicle record, read from the player's own install

**Changed:** `tools/gta_to_thaw_map.py`: new `--proxy-cars N` and `--thaw-dir <reTHAWed folder>`. The tool reads one vehicle record from reTHAWed's own packs at build time (the item chain holding `Class = Vehicle`, which carries its model path, type and flags), and writes N copies as level nodes in a row 15 m east of the start point. The writer takes raw checksums as names (`_k`) and writes integer items. Nothing is copied into the repo; the output goes to `extracted\` (or the `--out` folder).

**Why:** user 2026-10-14: CJ must be able to skitch the San Andreas cars, so reTHAWed needs vehicle objects to stand in for GTA's cars (DESIGN 8.42c). Decoding the type-to-model link was slower than the copy; the record turned out to hold its own model path.

**Tested how:** test map built from a 400 m box around the start point with 3 proxies: the level pak holds 3 vehicle records with the same items as the template (`tools\` read-back check). The rail lists the plugin reads were backed up before and restored after, so the installed full map still matches its rails. **Not run in game.**

**Still broken / not tested:** whether reTHAWed spawns and draws the copied vehicles (the test is in DESIGN 8.42c); whether a proxy can follow a GTA car without NaN; memory for vehicles is not in the `rethawed.ini` numbers yet.

**Next:** the user loads the test map (one map at a time: back up the installed map first), and reports what appears; then `debug.txt` for errors.

**Result of the first load (2026-10-14, user):** reTHAWed **crashed** on load: `debug.txt` shows `Loading Ass::CSkinAsset models\veh\veh_pickup\veh_pickup.mdl`, then `Obj::CRailManagerComponent::InitFromStructure - Model: veh\veh_pickup\veh_pickup.mdl`, then `FATAL ERROR ... EXCEPTION_ACCESS_VIOLATION (Read of 0x00000004)` with EAX = EBX = 0. The vehicle model loads; the rail manager component of the copied record reads a rail that is not in this map.

**Fix (same day):** `_keep_vehicle_item`: the proxies keep only the class, type, model, rotation and flags; the unnamed checksums and integers (the rail manager) are dropped. 6 of the 10 items are kept. The test map is rebuilt in `extracted\thaw_maps\proxytest` (read-back: 3 records of 6 items). **Not run in game yet.**

## [2026-10-14] 8.42b skitch: pushing the left stick up behind a car also grabs on

**Changed:** `src/thaw/bmx.cpp`: the grab now also fires when the left stick is pushed up (GTA's stick is negative up, the same test as `spawner.cpp`: below -80). The D-pad Up still works, and Down still lets go.

**Why:** user 2026-10-14, after the first test ("good start"): the analog stick up behind a car should skitch too.

**Tested how:** built `thaw_plugin` cleanly. **Not run in game.**

**Still broken / not tested:** animation and the balance meter (see DESIGN 8.42, step 2): reTHAWed draws its balance meter and plays its skitch pose from its own scripts, and we have not yet run those for our skater.

## [2026-10-14] 8.42 skitch the way reTHAWed does it (step 1): press Up behind a car to grab on, Down to let go

**Changed:** `src/thaw/bmx.cpp`: the L1 tow is replaced by reTHAWed's grab. **Up** (a new press) directly behind a moving car (1 to 8 m back, within about 31 degrees of its nose, same level) grabs on. **Down** (a new press), the car stopping for 0.5 s, the car out of reach, or more than 12 m away lets go. The tow itself is as before (his velocity matched to the car's, a spring to 3.2 m behind the car's centre), now aimed along the car's nose. `src/gta/cars.cpp`: the car he holds is followed by its pool index out to 16 m, even when it is slow, so it is never swapped for the nearest car; its nose direction is published. `src/common/link.h`: link **v17** (`Local\SkateAndreas_v17`, 1108 bytes): `gtaSkitchId`, `gtaSkitchFwd`, `thawSkitchId`; a new block starts both car ids at -1, because zero would mean "holding car 0".

**Why:** user 2026-10-14: "check the issues we had with skitching in this project and try to make it work exactly like it does in rethawed". The problems and the sources are in DESIGN 8.42: it grabbed from any side with L1, nothing let go on purpose, there was no balance, and a tow could switch cars. reTHAWed's controls come from the official manual and the in-game text (`\b7`); the release from the walkthroughs.

**Tested how:** both plugins build cleanly (`cmake --build build --config Release --target gta_plugin thaw_plugin`); the shared block's size is checked by `static_assert` (1108 bytes). The new binaries are newer than their sources. **Not run in game.**

**Still broken / not tested:** not run in game at all. Not modelled: **balance** (Left and Right, the balance meter; step 2). The tow point (3.2 m), the 31-degree cone, the 1 m/s stop speed and the 12 m release are ours, not reTHAWed's, and need tuning by test. The radio wheel also uses Up (volume): close it before skitching. The release zip still holds the earlier build; rebuild it after the test.

**Next:** the user tests it (DESIGN 8.42, "How to test"), then step 2: balance.

## [2026-10-14] INSTALL.md rewritten as the end-user install-and-play guide; release rebuilt

**Changed:** `INSTALL.md` — rewritten as a **player** guide (it had drifted into a dev-ish checklist): what you need, where each file goes (with a folder diagram and the two rules that prevent most failures), the one-time map build (button, then the copy into reTHAWed that only the player can do), Play and the **five** checks, the controls, troubleshooting, and what works. `release\SkateAndreas.zip` rebuilt around it (11.21 MB).

**Why:** user 2026-10-14: *"write install and play instructions for the end user"*.

**Tested how:** after the rebuild, the three packaged binaries were re-hashed against `build\Release` - all identical, so the zip still ships the exact plugin the user tested; the code was **not** rebuilt. The guide's `INSTALL.md` inside the zip was read back and is the new file.

**Still broken / not tested:** the guide has not been followed by a fresh player on another machine. Two things it had wrong are fixed: it said **four** checks (there are five, the map one added in 8.39), and it said the mod **cannot** choose reTHAWed's game mode on Play - which road 3 made untrue.

**Next:** hand the zip to a player.

## [2026-10-14] 8.35 road 3: tested in game - free skate loads itself, sound and controller work

**Result:** one run (user 2026-10-14), **untouched** after Play. `logs\thaw.log`: the controller joined as player 1 at 10.7 s (`of 2 open`, held count 1 -> 0); `SetGameType freeskate` and `SetCurrentGameType` both returned 1; `change_level level = load_MPGTALSE` returned 487350264; then `IN FREE SKATE ON OUR MAP` and **`IT WORKED`** at 12.2 s. The audio watch fired **once, first try** (`SDL_PushEvent returned 1, try 1`) and 0.5 s later said `reTHAWed's sound is on (in our level)`. So both fixes work - joining the open controller, and giving reTHAWed a "focus gained" event after the auto start - and **Play no longer needs an alt-tab to reTHAWed**.

**Changed:** no code change in this entry; it records the test of `1fdf3d0` together with the `autostart.h` / `drawlog.cpp` / `main.cpp` that call it, which the DLL was built from and which are **committed with this entry** (they were the last uncommitted source behind the release).

**Tested how:** the log lines above, from the run the user played. Nothing new was built.

**Still broken / not tested:** seen in **one** run only - worth another before it is called solid. The "3 s after" and later state logs are ordinary. Nothing else regressed in that run (no NaN, no missed code checks).

**Next:** just Play, as a player would, and expect free skate ~8 s after reTHAWed opens.

## [2026-10-13] 8.35 road 3 fix 2: reTHAWed's sound stays on after the auto start

**Changed:** `src/thaw/autostart.cpp`: new audio watch, from reTHAWed's `Present` twice a second: while reTHAWed is in our level and its "muted because unfocused" state is on (three jumps in reTHAWed.exe that rthw.dll flips: 74 74 75), it sends reTHAWed the SDL "window focus gained" event (`SDL_PushEvent`), so reTHAWed's own code unmutes. At most every 5 s, 10 tries; logged as `autostart audio:`.
**Why:** user 2026-10-13: fix 1 worked (controller) "but I wasn't getting audio on the reTHAWed radio station". debug.txt: the level's song did start; our music code paused / resumed it fine. rthw.dll (read in the cloud): MuteOnUnfocus (default on) mutes on focus lost and unmutes on focus gained; road 3 never gives reTHAWed's window focus, so it stayed muted.
**Tested how:** rthw.dll's window-event handling read in the cloud (disassembly); clang syntax check. Not run yet.
**Result:** not tested yet.

## [2026-10-13] 8.35 road 3 fix 1: join the controller as player 1 (no input after the auto start)

**Changed:** `src/thaw/autostart.cpp`: before the level calls, road 3 now runs rthw.dll's own "add player" (`+0x54C40`) with the first controller reTHAWed has open, then sets that player's "buttons held" count to 0, so reTHAWed reads it. Skipped if player 1 already has a pad or none is open; code-checked against rthw.dll with its real load address.
**Why:** user 2026-10-13: road 3 got into skate mode "but got no controller input". thaw.log: the level loaded ("IT WORKED"), but reTHAWed never asked our input hook anything while skating. rthw.dll (read in the cloud): a pad is read only after it joins as a player, and it joins only on a button-press event, which SDL gives only to the front window. Menu presses did that before; road 3 skips the menu.
**Tested how:** rthw.dll's event loop, add-player and pad-reader code read in the cloud (disassembly); clang syntax check. User 2026-10-13: Play, untouched.
**Result:** works - the controller drives the skater. But no sound from reTHAWed (fix 2).

## [2026-10-13] 8.35 road 3 (test): free skate on our map the way the level menu does it

**Changed:** `src/thaw/autostart.cpp/.h`: new road 3. Once per reTHAWed start, 8 s after its front end is up with no level loaded, it runs what reTHAWed's own level menu runs: `SetGameType freeskate`, `SetCurrentGameType`, then `change_level level = load_MPGTALSE`, building the `level = ...` parameter with reTHAWed's own parameter-list code (`0x004141A0` set up, `0x00415E00` add a checksum, `0x00415420` clear; code-checked first). Called from reTHAWed's `Present` (`src/thaw/drawlog.cpp`), only on the thread that owns reTHAWed's window, with a re-entry guard; then logs the state every second for 60 s. `src/thaw/main.cpp`: runs its code checks at start.
**Why:** user 2026-10-13: "can we fix the launcher not putting the player in free skate?". Roads 1-2 (DESIGN 8.35) failed: the raw load script skipped `ChangeLevel` (so the level field kept saying the front end), and pressing buttons for the player can't start itself. Road 3 does what the menu itself ends with, and `Present` runs without anyone touching the pad.
**Tested how:** reTHAWed's scripts and code read in the cloud (decompiled scripts and disassembly, nothing in the repo); clang syntax check (Windows target, stand-in headers). User 2026-10-13: Play, untouched.
**Result:** the level loads by itself and skate mode works in GTA, but no controller input (fix 1).

## [2026-10-13] launcher: friendlier version wording, and a different reTHAWed build only WARNS

**Changed:** `src/launcher/main.cpp`: `SetupCheck` gained a **warning** state — shown **amber** with a `!!` mark, it does **not** block Play and does **not** fail `--selftest`. The reTHAWed check now reads `your copy is a different version - trying it anyway` (amber) where it used to be a refusal, and writes one log line with both byte counts and why. When Play genuinely *is* blocked, the status now names each failing check and its note (`Fix these first:` ...) instead of "some setup checks failed".

**Why:** user 2026-10-13 — reTHAWed **5.0.0.2** is out and the size gate was refusing to start it: *"let them try to launch at least maybe it will work who knows"*. The size is only a guess that we were written against that build; the real authority is the plugin's own code checks, which report exactly what they do not recognise. "Try it, and the log says why if it misbehaves" beats "refuse".

**Tested how:** faked a reTHAWed folder holding a 4,000,000-byte `reTHAWed.exe` (via `SkateAndreas.ini`): `--selftest` gives `[WARN] reTHAWed 5.0.0.1 - your copy is a different version - trying it anyway` with the other four `OK`, and **passes**, so Play is allowed. Normal layout: all five `OK`. **That test also caught a real bug** — adding the field shifted the brace initialisers so every check came out as a warning (MSVC flagged the narrowing); fixed before committing.

**Still broken / not tested:** not run against a real 5.0.0.2 yet — the user will. If it misbehaves, the `code check ... DIFFERENT` lines in `thaw.log` are the work list (DESIGN 11).

**Next:** try 5.0.0.2 and read `logs\thaw.log` for what the plugin did not recognise.

## [2026-10-13] 8.41 car rails: "parked" now actually means parked (the moving-rail NaN)

**Changed:** `src/gta/cars.cpp`: a car gets a roof rail only when it is **genuinely parked**, decided by the car's **position not changing** since the last pass (`kStillMoved` = 2 cm, remembered per vehicle-pool index), instead of by its speed. The old test was `move speed <= 0.05`, and that is a different thing: 0.05 m per frame is **2.5 m/s (9 km/h)**, so a car pulling away from a standstill sat inside that window for about a second with its rail rewritten **every frame** — a rail that **moves under the skater**, which is exactly what turns reTHAWed's grind maths to NaN. The once-a-second car scan now prints `NO RAIL (moving)` where it used to print a rail height it would not actually use.

**Why:** the user grinded moving cars on 2026-10-13 and got the glitch. `logs\thaw.log`: the skater's velocity becomes `-nan(ind)` at 294.9 while travelling at ~23 m/s, and then his position; `logs\gta.log` then shows every distance as `-nan(ind)` and `skate mode: OFF (reTHAWed not in a level)` at 302.6. That is the moving-rail chain.

**Tested how:** built clean; **not tested in game yet**. The scan line now states whether a rail would be published, so the next run shows the gate working directly.

**Still broken / not tested:** when a parked car drives off while the skater is on its rail, the rail stays where it was — a "ghost" rail he can keep grinding in mid-air. Cosmetic, and far better than NaN. Also unchanged: a moving car can still shove the skater about, and the player-folder path test (8.38 / 8.40) still has not been run — the games were launched from the normal setup on 2026-10-13, not from the test copy.

**Next:** grind a **parked** car (should work), then try a moving one (there should be no rail at all), and check the scan lines say `NO RAIL (moving)`.

## [2026-10-13] 8.40 the release carries its own Python, plus an MIT licence and a build script

**Changed:** `tools/make-release.ps1` (**new**) builds the whole package reproducibly: the three built binaries, the map tools, `INSTALL.md`/`README.md`/`LICENSE`, and the **Windows embeddable Python** (3.11.9 amd64, ~11 MB, with its own `LICENSE.txt`) under `tools\python\`, zipped to `release\SkateAndreas.zip` (~11 MB). `src/launcher/main.cpp`: `FindPython` now tries the bundled interpreter first (`<home>\tools\python\python.exe`), then `python` / `python3` / `py -3`, so a machine where the bundled one cannot run (a **32-bit Windows**) still works; `--where` prints the Python it chose. `LICENSE` (**new**): **MIT**, so the code can be reused freely (compatible with the zlib-licensed plugin-sdk). `INSTALL.md`: no Python install, a folder layout, and what to do if the interpreter is missing.

**Why:** user 2026-10-13 — "will players need Python installed?" and, if it makes life easier, bundle it. The map itself cannot be shipped (it is GTA's own collision, converted — AGENTS.md rule 1, DESIGN 8.39), so bundling a runtime is what gets to a single-download, one-button install.

**Tested how (2026-10-13):** the bundled Python runs the map tool alone — `release\SkateAndreas\tools\python\python.exe release\SkateAndreas\tools\gta_to_thaw_map.py --help` → **exit 0**, so its sibling imports resolve. The **packaged launcher, run from a folder outside the project**, picks its own bundled Python and resolves home/logs/extracted to that folder (`--where`); the dev launcher still picks the system `python` and the project folder. The launcher's home rule was tightened to need **both** `CMakeLists.txt` and `logs` two levels up, so a player's ordinary folder can never be mistaken for this project.

**Still broken / not tested:** the games have **not** been run from a player-style folder, so the last hop — the rail reader inside reTHAWed reading `SKATEANDREAS_DIR` on someone else's PC — is reasoned (the launcher and the plugins share `src/common/paths.h`), not observed. The user asked for a public release so players can report any launcher trouble.

**Next:** publish `release\SkateAndreas.zip`.

## [2026-10-13] Release package built (release\SkateAndreas.zip)

**Changed:** built **`release\SkateAndreas.zip`** (0.47 MB) holding `SkateAndreas.asi`, `SkateAndreas-thaw.dll`, `SkateAndreas-launcher.exe`, `tools\` (`gta_to_thaw_map.py` plus the two modules it imports, `gta_area_dump.py` and `gta_extras.py`), `INSTALL.md` and `README.md`. The `.gitignore` whitelist covers `release\`, so the package can never be committed by accident (`git check-ignore` confirms).

**Why:** the last item of DESIGN 11; the user asked for it 2026-10-13.

**Tested how:** the packaged map tool runs on its own — `python release\SkateAndreas\tools\gta_to_thaw_map.py --help` → exit 0 — which proves `tools\` is complete (the first `-1` was just the output pipe being closed early). `git check-ignore` confirms the zip is not committable.

**Still broken / not tested:** the package has **not been run on another machine**. The launcher's game folders are baked to this machine's paths, so a new player must press **Browse** the first time (by design, 8.36). Building the map still needs **Python 3** installed (8.39) unless a Python runtime is bundled with the package.

**Next:** publish the zip, and/or bundle Python so a player needs nothing at all.

## [2026-10-12] 8.39 "Build the map..." button in the launcher

**Changed:** `src/launcher/main.cpp`: a **Build the map...** button that finds Python 3 (`python`, `python3`, `py -3`), runs `tools\gta_to_thaw_map.py` in its **own console window** with the whole-map pool arguments (`--box -3000 -3000 3000 3000 --no-geometry-nodes --rail-pool 4500 --rail-drop-max 0`), waits for it, then opens `extracted\thaw_maps\` and says where to copy the result. A **fifth setup check** — "SkateAndreas map built" — looks for `<home>\extracted\thaw_maps\_railpool\rails_in_map.csv`, and says "press Build the map" when it is missing. The window grew to 560x480; Play and the button are disabled while a build runs; `INSTALL.md` step 3 now offers the button first.

**Why:** user 2026-10-12 — making the map from a command prompt is a poor first experience. The map itself **cannot be shipped**: it is GTA's own collision, converted, and redistributing that is what AGENTS.md rule 1 forbids (DESIGN 8.39, and the discussion behind it).

**Tested how:** `--selftest` → **all five checks pass** (the map check green, on a machine where the map is built); `python --version` → `Python 3.11.0`, exit 0, which is exactly what the probe tests; built clean.

**Still broken / not tested:** **the button's own run is not tested** — it would rebuild the ~80 MB map and rewrite the rail pool, so it is the user's to press. The launcher cannot copy the map into reTHAWed (rule 4), so that step stays manual, and **Python 3 stays a requirement**.

**Next:** press Build the map once (the existing map keeps working either way).

## [2026-10-12] 8.38 a downloaded copy now works wherever it is unzipped

**Changed:** new `src/common/paths.h` — `HomeFolderW()` (the mod's folder, from `SKATEANDREAS_DIR`), `LogFolderW()` and `ExtractedFolderA()`. The launcher (`src/launcher/main.cpp`) computes the home (`ModHome()`: its own folder, unless it is running from a build output folder inside the project — `..\..\logs` exists — in which case the project folder), sets `SKATEANDREAS_DIR` for both games to inherit, and uses the same log folder for its own log and its "Open logs folder" button. `src/gta/main.cpp` and `src/thaw/main.cpp` take their log folder from it; `src/thaw/railscan.cpp` finds the rail pool's two csv files through it (`PoolPath()`). New launcher switch **`--where`** writes the resolved folders to `launcher.log` and exits.

**Why:** the folder this project was compiled in was baked into the plugins, so a **downloaded** copy would look in `D:\Mods\my-passthrough\...` — logs in a folder that does not exist, and the rail pool unable to open its rail list, so **nothing would be grindable**. That was the last thing in the way of a release (found while writing `INSTALL.md`); see DESIGN 8.38.

**Tested how:** `--where` from `build\Release` → home = the project, logs = `<project>\logs`, extracted = `<project>\extracted` — **exactly as before**, so nothing moves on this machine. `--where` from a copy of the launcher in a temp folder → home, logs and extracted all under that copy, and it wrote its log there; the temp folder was then removed. `--selftest` still passes all four checks.

**Still broken / not tested:** **not run in the games from another machine** — that needs a second PC or a full simulated install, so the plugin side is reasoned from the shared header rather than observed.

**Next:** the release itself — a zip holding `SkateAndreas.asi`, `SkateAndreas-thaw.dll`, `SkateAndreas-launcher.exe`, `tools\gta_to_thaw_map.py` and `INSTALL.md` (DESIGN 11).

## [2026-10-12] 8.37 on-screen hints in GTA's own black message box

**Changed:** new `src/gta/hint.h` / `.cpp` (added to `CMakeLists.txt`, called from `src/gta/main.cpp` on every world update). With no mode on it shows GTA's **own help box** — **`Press L3+R3 to Skate`** on foot, **`Press L3+R3 to toggle BMX mode`** while riding a BMX. The "is CJ on a BMX" test is now exported from BMX mode (`BmxCjRidesBmx()` in `src/gta/bmxmode.*`) so there is only one copy of it. It never overwrites a message GTA itself is showing (`CHud::HelpMessageDisplayed()`), shows nothing while a car is being driven, and waits ~10 s after the plugin loads so the game is fully up first.

**Repeat behaviour (user's choice, 2026-10-12):** the **skate** hint is shown **once per run of the game** (`g_skateHintShown` — the player has to load free skate by hand anyway, so they only need telling once); the **BMX** hint repeats at most once a minute (`kHintRepeatMs`) while CJ is on a bike with no mode on.

**Why:** user 2026-10-12 — nothing told a player how to start skating (or BMX mode).

**Tested how:** played in GTA (user 2026-10-12): **both messages appear as intended** — the skate line once on foot, the BMX line on a bike. It logs `hint: showed "..."` to `logs\gta.log`.

**Still broken / not tested:** nothing known; the wording and the BMX repeat interval are the user's to change if they want.

**Next:** play in GTA: on foot, the skate line should appear once, shortly after you get control; getting on a BMX should show the BMX line.

## [2026-10-12] GUI launcher: the game folders can be pointed out by hand

**Changed:** `src/launcher/main.cpp`: the window now has a **Game folders** section showing the **GTA San Andreas** and **reTHAWed** folders, each with a **Browse...** button (the standard Windows folder picker). The choices are saved next to the launcher in **`SkateAndreas.ini`** (`[Paths] GtaDir=` / `RethawedDir=`) and are used both by the setup checks and by Play. With no ini, the paths the build was made with are used, so nothing changes on the current machine. The window is larger (560x460) to fit the new rows. `CMakeLists.txt`: the launcher links `ole32` for the picker.

**Why:** user 2026-10-12 — "is the player able to point to their gta and rethawed files manually in case they don't auto detect?" Until now both paths were fixed when the project was **built**, so any other PC needed a rebuild. It also gives section 11's planned settings file its home.

**Tested how:** `--selftest`, three ways: (1) no ini → all four checks OK; (2) an ini pointing GTA at `C:\Totally\Wrong` → both GTA checks red with the reason, exit 1; (3) ini removed → all four OK again. The window: launched and closed; the log shows `launcher: Ready. Click Play to start.`, so every control was created.

**Still broken / not tested:** the **Browse** buttons have not actually been clicked in a run (the picker window and the save path are unexercised), and the layout was checked by arithmetic rather than by eye.

**Next:** the user opens the launcher, tries Browse for each folder (and cancels once), and confirms the window looks right.

## [2026-10-12] 8.35 result: starting in free skate is a DEAD END — stopped, experiment switched off

**Changed:** `src/thaw/autostart.cpp`: the front-end **replay** and the **recorder** are switched **off** (`kReplayFrontEnd`, `kRecordFrontEnd`). `docs/DESIGN.md` 8.35: the full outcome written up.

**Why:** both routes to "Play lands you in free skate on our map" were tried and both failed, for reasons now understood rather than guessed:
1. **Running reTHAWed's own level script** loads the map (`debug.txt`: `LOADING CUSTOM LEVEL... MPGTALSE`) but leaves the game's "current level" field on the **front-end** level — its value is exactly `qbkey("load_z_mainmenu")` — so the plugin cannot recognise the level, CJ floats, and skate mode refuses. *(A comparison run proved the difference: the manual free-skate load gives `0x9F014B98` and `ABSOLUTE (level lined up with GTA)`.)*
2. **Replaying the player's own presses** (captured sequence, played back through the input hook) cannot work, because **reTHAWed's front end only polls the pad once the player touches it** — the input hook ran for **2 s** in a 69 s run, and only after a real press. Our synthetic presses go in through that same hook, so the replay can never start itself.

**Tested how:** four runs on 2026-10-12, each read back from `logs\thaw.log`, `logs\gta.log` and reTHAWed's `debug.txt`. The last one: the hook ran 2 s of 69 s.

**Still broken / not tested:** nothing further; this is stopped by decision (user 2026-10-12). The manual free-skate load works and stays.

**Next:** the user's actual complaint (a story character's voice lines over CJ) is better fixed directly by **muting reTHAWed's skater voice lines** — much smaller, and independent of any of this.

## [2026-10-12] 8.35: replay diagnostic (it never started)

**Changed:** `src/thaw/autostart.cpp`: the replay logs a line once a second — `frontend replay: inFrontend=.. settleSeen=.. userDrove=.. done=.. running=.. synth=0x..` — so the reason it does not start is visible.

**Why:** the first replay run did nothing and logged **no** `frontend replay:` line at all, even though reTHAWed was on its front end the whole time (the GTA log shows `[menu]` for **all 177** of its samples). The likely cause is that the state reads something other than "front end" often enough to keep resetting the 2.5 s settle timer, but a line a second says so for certain instead of guessing.

**Tested how:** built only — needs one short run.

**Still broken / not tested:** the replay has never run, so nothing about the idea is proven yet.

**Next:** one short run (Play, **do not touch the pad**, ~30 s), then read the diagnostic line.

## [2026-10-12] 8.35: the front-end replay (plays back the player's own presses)

**Changed:** `src/thaw/autostart.cpp` / `.h`: `kFrontEndMacro`, the captured sequence, and the code that plays it back — a timed list of the player's presses (`Down ×3, Cross ×3, Right ×3, Down ×2, Cross held`), holding the right buttons for reTHAWed's front end. It starts ~2.5 s after reTHAWed's front end appears, **only if the player has not pressed anything themselves** (`g_userDrove`), and stops when reTHAWed leaves the front end. `src/thaw/input.cpp`: the controller hook now answers "pressed" for the buttons the replay is holding.

**Why:** the one-script load leaves the game's level field on the menu (see the previous entry), but the player's own free-skate navigation sets everything correctly. Replaying their own presses therefore uses reTHAWed's real flow, so the level, the mode and the skater all come out right (DESIGN 8.35).

**Tested how:** built only — **not tested in game yet**. It logs `frontend replay: ...` lines to `logs\thaw.log`.

**Still broken / not tested:** the whole idea. Unknown: whether reTHAWed's front end accepts input while GTA is the front window (our focus patch makes it think it is focused, but the menus may check the window directly); whether the replay's timing survives menu animations; and whether the sequence loads the right level with the right skater.

**Next:** one run: Play, then **wait and do not touch the pad** (~45 s); check whether reTHAWed ends up in the level (alt-tab and look, or press L3+R3 in GTA and see whether CJ lands on the ground instead of floating).

## [2026-10-12] 8.35: front-end navigation recorder; the one-script load switched off

**Changed:** `src/thaw/autostart.cpp` / `.h`: a **recorder** that logs every front-end button press and release with its time (`frontend: Down DOWN (+1.23 s)`), called from the input hook while reTHAWed is the **front window** (`src/thaw/input.cpp`); and the one-script load is switched **off** (`kTryTheLoad = false`).

**Why:** the comparison run settled it — the manual **free skate** load gives level **`0x9F014B98`** and `skate mode: ON ... ABSOLUTE (level lined up with GTA)`, while our script load leaves the game on the menu level (`0x99CE4D72` = `qbkey("load_z_mainmenu")`), which is why the plugin saw "not in a level" and CJ floated. The user chose to reach free skate by **replaying their own pad presses** (DESIGN 8.35), so the sequence has to be captured first. The load is off so it cannot interfere with the recording.

**Tested how:** built; the recorder writes its lines to `logs\thaw.log`. The capture itself needs the user to do their normal manual load once (their run also showed they navigate with the **pad** — `input: source now REAL controller (reTHAWed in front)` while GTA's pad log showed `Down` / `Cross`, so our hook sees it).

**Still broken / not tested:** the replay does not exist yet; everything depends on the captured sequence.

**Next:** the user does the manual load once with the recorder on; then build the replay from the capture.

## [2026-10-12] 8.35: the script-loaded level's ID was the MENU's — my alignment entry reverted

**Changed:** `src/launcher/main.cpp`: the launcher log is now opened **shareable** (`_wfsopen(..., _SH_DENYNO)`), so it can be read while the launcher window is open. `src/common/convert.h`: the alignment entry I added earlier this evening for level ID **`0x99CE4D72`** is **reverted** — it was wrong (see below). `src/thaw/autostart.cpp`: the auto-load waits 90 s now, on purpose, for the comparison run.

**Why:** the run after that entry could not enter skate mode and the GTA log said `reTHAWed not in a level`. Re-reading the logs cracked it: the ID the plugin reads, **`0x99CE4D72`, is exactly `qbkey("load_z_mainmenu")`** — reTHAWed's **front-end menu level**. So a script call from the front end does load our map (reTHAWed's `debug.txt` still shows `LOADING CUSTOM LEVEL...` and `Loading career level MPGTALSE...`), but the game's "current level" field — what `ReadGameState` reads, `0x008A3B5C + 0x20` — **keeps pointing at the menu**. That is why the alignment was never applied (my matching entry would have labelled the *menu* as our map, which is worse), why skate mode refused, and why CJ floated.

**Tested how:** logs only, plus a build. The reverted entry was never a real fix — it should not have gone in as one.

**Still broken / not tested:** the whole auto-start. Running our map's load script is **not** equivalent to the front end's **free skate** flow: the game's level field is left pointing at the menu, so the plugin cannot recognise the level.

**Next:** the comparison run — load the map the **normal way (free skate)** in reTHAWed while GTA is running, then read the level ID in `gta.log` (`link skater: [level] level 0x...`). That is what the front end sets, and therefore what the auto-start has to reproduce.

## [2026-10-12] 8.35: align the script-loaded level (the floating fix) + launcher log sharing

**Changed:** `src/common/convert.h`: new alignment entry `kGtaPoolLoadScript` for level ID **`0x99CE4D72`** — the ID reTHAWed reports for our map when it is loaded by **our own script call** from the front end (a menu load reports `0x9F014B98`). Same map, same origin `(1975, -1525, 0)`, same scale and mirror, so the same alignment. `src/launcher/main.cpp`: the launcher log is now opened **shareable** (`_wfsopen(..., _SH_DENYNO)`), so it can be read while the launcher window is open.

**Why:** the 2026-10-12 auto-load run worked (reTHAWed loaded MPGTALSE, its `debug.txt` says so) but the plugin did **not** recognise the level ID, so it fell back to its "relative" placement and the user saw CJ **floating all around**. Separately, the launcher held `launcher.log` open, so it could only be read after closing the window.

**Tested how:** built clean; the log-sharing fix verified by reading `launcher.log` while the launcher window was open. The alignment entry itself is **not yet tested in game**.

**Still broken / not tested:** the alignment entry is a **hypothesis** — the next run has to show CJ on the ground. The level still loads as a **career** level, not **free skate** (the user's actual wish), which is the next piece of work.

**Next:** Play → wait for the auto-load → turn skate mode on: does CJ sit on the ground now instead of floating? Then continue to find the front end's free-skate path.

## [2026-10-12] 8.35 step 1 (test): can we start our map from reTHAWed's front end?

**Changed:** new `src/thaw/autostart.h` / `.cpp` (and `CMakeLists.txt`). On reTHAWed's **game thread**, once per start: 8 s after the plugin loads, if reTHAWed is sitting on its **front end**, run reTHAWed's own level script **`0x9F014B98`** — the checksum of the name **`load_MPGTALSE`** — up to 3 times, 2.5 s apart, then log reTHAWed's state (front end / level, and the level ID) once a second for 15 s. Wired in from `src/thaw/input.cpp` (our controller hook — the game thread, the same place BMX runs its script) and installed from `src/thaw/main.cpp`. The script runner (`0x004135E0`) is byte-checked before anything is called, as BMX's is.

**Why:** the user (DESIGN 8.35) wants Play to drop them into **free skate on our map** as a created skater (a created skater has no voice lines to play over CJ). reTHAWed has no setting and no command-line flag for that, so the plan is to run reTHAWed's own level script. This step answers, in a single run, whether that script on its own loads the level.

**Tested how:** one launch through the GUI launcher (2026-10-12 ~21:52), reTHAWed left on its front end. **It works.** `thaw.log`: `autostart: attempt 1: reTHAWed is on its front end; running script 0x9F014B98`, then `the call returned 487309304`; reTHAWed's own `debug.txt`: `LOADING CUSTOM LEVEL...`, `Changing Sky: MPGTALSE_sky`, `Loading career level MPGTALSE...`, `It's a custom zone!`, `Setting level name to MPGTALSE...`. So running that one script does load our map. Skate mode afterwards worked normally (the GTA log shows `skate mode: ON` and CJ following the skater). The same run also proved the launcher's **Play** path: it started reTHAWed, injected the plugin and started GTA.

**Still broken / not tested:** two things. (1) reTHAWed loaded it as a **career level** (`Loading career level MPGTALSE...`); the user wants **free skate**, so that is still to do. (2) The plugin then read the level as **`0x99CE4D72`**, **not** the `0x9F014B98` it recognises (the checksum of `load_MPGTALSE`), so it used its **"relative" placement** rather than the calibrated level alignment (see the note in the environment list about a wrong level id). Whether that lines up in game is not yet known. Also noted: the launcher keeps `launcher.log` open, so it cannot be read while the launcher window is open.

**Next:** find the front end's **free skate** path (what it does beyond loading the level) and whether that path gives the recognised level id; and ask the user whether the skating lined up during the auto-load run.

## [2026-10-12] Rename to "Tony Hawk's SkateAndreas"

**Changed:** the mod's name, and the files it ships as. The **display name** (what a player sees) is now **Tony Hawk's SkateAndreas**; file and folder names use **SkateAndreas** (no apostrophe, no space, so scripts and build tools stay happy):
- `my-passthrough.asi` → **`SkateAndreas.asi`**, `my-passthrough-thaw.dll` → **`SkateAndreas-thaw.dll`**, `my-passthrough-launcher.exe` → **`SkateAndreas-launcher.exe`** (`CMakeLists.txt`: project name, the three `OUTPUT_NAME`s, `GTA_DEPLOY_DIR`).
- GTA's Mod Loader folder: `modloader\my-passthrough\` → **`modloader\SkateAndreas\`**. The user approved this write; `radio-settings.ini` was copied across, the new plugin built into the new folder, and only then was the old folder deleted — so GTA never had two copies of the plugin at once (which would have double-loaded our hooks).
- The internal shared-memory names: `Local\MyPassthrough_v16` → **`Local\SkateAndreas_v16`**, `Local\MyPassthrough_Hud_v1` → **`Local\SkateAndreas_Hud_v1`** (`src/common/link.h`, `src/common/hudlink.h`).
- The level label `GTA East LS (my-passthrough)` → `GTA East LS (SkateAndreas)` (`src/common/convert.h`), the tools' map name/author (`tools/gta_to_thaw_map.py`), the plugin "loaded" log lines, and the header comment in every source file.

**Deliberately NOT changed:** the **repo folder** `D:\Mods\my-passthrough` and `build\` (so the CMake cache, the paths in `AGENTS.md` and the docs, and the working folder stay valid), the **Mod Loader folder in `AGENTS.md`** was updated to the new path, and the map's internal **prefix `MPGTALSE`** (the plugin matches the level by that prefix and its ID; changing it would break every map).

**Why:** user 2026-10-12, for release.

**Tested how:** `build\Release\SkateAndreas-launcher.exe --selftest` → exit code 0, all four checks OK (they look for the new names in the new folder). The build copies `SkateAndreas.asi` to `modloader\SkateAndreas\`. Verified the old `modloader\my-passthrough\` folder is gone and the new one holds `SkateAndreas.asi` + `radio-settings.ini`.

**Still broken / not tested:** the **Play path** (starting both games) still has not been run — not tested. Already-generated **maps** keep their old names until made again (`extracted\thaw_maps\...`); the one installed in reTHAWed is untouched and still works, because the level is matched by ID, not by name. Any **desktop shortcut** pointing at the old `my-passthrough-launcher.exe` is now broken (the launcher is `build\Release\SkateAndreas-launcher.exe`).

**Next:** the user plays once through the GUI launcher; then read `logs\launcher.log` and `logs\gta.log` / `logs\thaw.log`.

## [2026-10-12] 8.34 GUI launcher

**Changed:** `src/launcher/main.cpp` rewritten as a **Windows GUI app** (was a console app). The window has: a **Play** button, **four setup checks** shown green (OK) / red (with the reason), a **"Start reTHAWed minimised"** tick box, and an **"Open logs folder"** button. Play checks the setup, starts reTHAWed and loads the THAW plugin into it, starts **GTA San Andreas**, then closes reTHAWed (we started it) when GTA exits. The long work runs on a **background thread** so the window never freezes. The log now **appends** to `logs\launcher.log` with a run header. A hidden **`--selftest`** switch runs the checks and exits without starting anything. `CMakeLists.txt`: new `GTA_DIR` cache setting (default `D:/Games/GTA San Andreas`) + `MP_GTA_DIR`, `WIN32` subsystem, `shell32` link. `docs/DESIGN.md`: new section **8.34** (the plan, written before the code).

**Why:** user 2026-10-12: "build a launcher with a gui that is easy for end users to understand" — one window, one button, both games (their choice out of the options offered).

**Tested how:** ran `build\Release\SkateAndreas-launcher.exe --selftest` → exit code 0, `launcher.log` shows all four checks OK, no game started. Launched the window and closed it after 3 s → `launcher.log` shows `launcher: Ready. Click Play to start.`, so the window, controls and checks all built.

**Still broken / not tested:** the **Play path itself** (start reTHAWed, inject, start GTA, close reTHAWed) has **not been run yet** — not tested. **"Start reTHAWed minimised"** is a first guess (`SW_SHOWMINNOACTIVE`); if reTHAWed pauses when it is not on screen it could affect its rendering and the HUD copy — untested. The controller fix is still always on (section 11: make it a tick box).

**Next:** the user runs the GUI once and plays; then read `logs\launcher.log` (and `logs\gta.log` / `logs\thaw.log`) for that run.

## [2026-10-12] 8.33 skitch: hold L1 near a moving car and be towed (first cut)

**Changed:** `src/common/link.h`: link **version 15** (name `_v15`, size 1084) + `gtaSkitchValid`, `gtaSkitchPos[3]`, `gtaSkitchVel[3]`. `src/gta/cars.cpp` `CarTick`: while scanning cars it also remembers the nearest **MOVING** car (speed > 0.1, within the same range) and publishes its position and velocity in **THAW units** (no rail). `src/thaw/bmx.cpp`: `CarSkitchGameThread` — while **L1 is held** and that car is within 7 m, the skater's **velocity** (`+0xC8`) is set to the car's every game-thread call; his position is never touched. Called from `BmxGameThreadTick` before its 100 ms gate, so it runs every pad query. Logs `cars: skitching in N frames this second`.

**Why:** user 2026-10-12: "try to get skitching going at least". Moving-car **grinding** was ruled out (a moving rail NaN's the skater's physics); a **tow** (velocity only, no rail) is the safe way to ride a moving car, THAW-skitch style.

**Tested how:** not yet in game — needs a run: hold **L1** next to a moving car and see whether the skater is carried along; `thaw.log` should show `cars: skitching in N frames`.

**Result:** pending. The button (L1) and the 7 m range are the first guesses and can be changed.

## [2026-10-12] 8.33 C5: car-hit bail + solid cars (state as committed)

**Changed:** `src/gta/skatemode.cpp`: `FastCarOnCj` (a car within 2.2 m, faster than ~18 m/s, driving into CJ -> skate mode off; `SkateModeBailTick`, called once a second from `main.cpp`, asks for it again after 3.5 s). `PushOutOfCars` (the spot we are about to put CJ at is moved to the nearest face of a car's box, so he stands on / against it). `src/gta/cars.cpp`: cars give roof rails only when stopped / slow (`m_vecMoveSpeed < 0.05`). `src/thaw/railscan.cpp`: a reserved car rail the skater is near is never moved.

**Why:** user 2026-10-12: cars should be solid to CJ, and a fast car should knock him off (bail).

**Tested how:** many in-game runs; `gta.log` diagnostics `car solid check` (car box + CJ's local spot + inside/outside) and `car hit check` (distance, speed, head-on).

**Result / state:** the push-out works (log: "INSIDE -> push out") but against the car's **collision box**, which can be smaller than the visible car. A **moving** car still drives through CJ: his position is re-imposed every frame, so a re-posed ped cannot block a car. The bail fires but its window (on him + head-on + fast) is narrow: it is unreliable and can fire when CJ skates into traffic. The trail: moving-car grinds blew the skater's physics to NaN (a moving rail cannot be ground), so rails are for stopped / slow cars only; parked-car grinding works. **Next:** skitch — a grab / tow on a moving car with no rail.

## [2026-10-12] 8.33 C3 (first cut): the car tow (velocity match)

**Changed:** `src/thaw/bmx.cpp`: new `CarTowGameThread` (called from the game thread). While the skater is on a car roof rail (nearest published car segment, close sideways, part way along), it sets the skater's **velocity** (object +0xC8) to the car's, worked out from how far the rail moved since the last tick. Also validates the delta (finite, plausible) first. New `cars: towing ...` log line in `BmxSecond`.

**Why:** user 2026-10-12: grinding a **moving** car rails blew the skater's physics to NaN (reTHAWed's grind assumes a still rail). Matching his velocity to the car's carries him along, so the rail stays ~still relative to him — THAW's skitch.

**Tested how:** builds clean with MSVC. Not run in the game yet.

**Result:** not tested yet. Test: grind a moving NPC car; expect it to hold and carry him (no yellow-screen glitch); `thaw.log` shows `cars: towing in N frames`.

## [2026-10-12] 8.33 C2 (first cut): car roof rails into the rail pool

**Changed:** `src/common/link.h` (version 14, `PublishCarRails` / `ReadCarRails`): the GTA plugin publishes up to 8 cars' roof rails (each car: 2 rails along the roof's long edges, a 2-point segment each) in THAW units. `src/gta/cars.cpp`: `CarTick` (every frame, skate mode) finds cars within 22 m of CJ, sizes the roof from the model's bounding box (fallbacks), converts to THAW, validates every coordinate, and publishes. `src/thaw/railscan.cpp`: `CarRailTick` reserves the pool's last 16 slots for cars and rewrites them each pass (validating too); the pool fill skips reserved slots.

**Why:** user 2026-10-12: grind GTA cars (all cars, including moving). DESIGN 8.33.

**Tested how:** builds; user ran it.

**Result:** the user could grind the spawned car and NPC cars, but then **the skater's velocity went NaN**, reTHAWed stopped producing frames, and GTA's screen went yellow (CJ's position came from the NaN skater). Two likely causes: (a) a bad roof box producing bad/NaN rail coordinates (now blocked by validation on both sides); (b) grinding a **moving** rail — reTHAWed's grind physics assumes a static rail, so a rail sliding under the skater can blow up (this is why THAW uses skitch for moving cars). Next test decides which; move to C3 (skitch/tow) for moving cars if it is (b).

## [2026-10-12] 8.33 C1: list the GTA cars near CJ (log only)

**Changed:** new `src/gta/cars.{h,cpp}` (+ `main.cpp`, `CMakeLists.txt`): once a second while skate / BMX mode is on, walks GTA's vehicle pool and logs every car within 40 m of CJ — pool index, model id, position, speed, whether it has a driver, health. Read only.

**Why:** user 2026-10-12: make GTA cars grindable and skitchable. reTHAWed has skitching (`RT_ToggleSkitching` in rthw.dll) but our level has no vehicles; the cars are GTA's, so both features must be driven from the GTA plugin. Plan in DESIGN 8.33; this is step C1 (data).

**Tested how:** builds clean with MSVC. Not run in the game yet.

**Result:** not tested yet. Test: spawn a car near CJ (F6), turn skate mode on, check `gta.log` for `car N: model ...` lines.

## [2026-10-12] 8.32 research: Natas spins (ruled out; needs THAW objects) + rail terrain/flag options

**Changed:** `tools/gta_to_thaw_map.py`: new `--rail-terrain NAME` and `--rail-flag NAME` (write a TerrainType / an extra flag item on every rail node; defaults unchanged). `src/thaw/railscan.cpp`: F4 places a degenerate "point" rail (~10 cm, hydrant/pole height) beside the existing F5 25 m rail.

**Why:** user 2026-10-12: make Natas spins work (essential for the full THAW feel). A Natas spin = ollie onto a vertical pole/narrow object, hold Flip + press Grind.

**Tested how / result:** all three rail-side guesses failed in the game (user): a degenerate rail gives a normal "bonk grind"; `TERRAIN_METALPOLE` on every rail changes nothing; a `Spinnable` flag on every rail changes nothing. The user's park object (`pole` piece) IS spinnable, so Natas is a **THAW-object** mechanic — and our map (GTA collision) has no THAW objects. Park saves (`reTHAWed\save\*-Park`) are a fixed-size, mostly-zero binary with hashed fields and no per-object toggle, so the marker can't be read off cheaply. **Blocked:** needs THAW object/scene nodes in the map (a real `pole` model), which is project-sized. Documented in DESIGN 8.32 F.

## [2026-10-12] 8.32 source D: a rail along GTA's railroad tracks

**Changed:** `tools/gta_to_thaw_map.py`: new `read_tracks(gta, centre)`, called from `main` when not `--no-rails`, appended to the rail list (so it lands in the map's rails and in `all_rails.csv`). It reads `data\Paths\tracks*.dat` (text: a count, then `x y z flag` lines) and, per file, emits the whole node list as **one rail**, twice over: offset left and right by half the 1.435 m gauge so the rails sit on the **two steel rails** (not down the middle). Logs `tracks:  N rails, M nodes`.

**Why:** user 2026-10-12: make railroad tracks grindable. GTA's track node lists are the exact rail lines; no model/DFF work needed.

**Tested how / result:** first cut split each file at jumps over 50 m and put one rail down the centre — user: "major improvement" but short grinds and CJ ground "in the middle of the track". Analysis: no turn in any file exceeds 45 deg, and the big jumps are straight track sampled sparsely, so splitting was wrong. Now one continuous rail per file, doubled onto the two rails: `tracks: 8 rails, 3898 nodes`. Whole-map pool rebuilt; not run in the game yet.

**Result:** retest pending. (Source C, wires, was deferred — see DESIGN 8.32: only 8 wire models, compressed geometry; the pylon/pole-top connection is the alternative to revisit.)

## [2026-10-12] 8.32: learn the camera offset; rotate the left stick to match GTA's camera

**Changed:** `src/gta/padlink.cpp`: new `RotateStickForCamera`. While skate mode is on, it compares the direction the player pushes (rotated into GTA world by `TheCamera.m_fCamFrontXNorm/YNorm`) with the skater's actual movement direction (the link's `thawVel` mapped by `FindLevelAlignment`), learns the signed offset (low-passed), and rotates the left-stick value published to reTHAWed by it. GTA's camera is untouched. Logs `pad: camera offset learned: N deg` once a second.

**Why:** user 2026-10-12: reTHAWed moves the skater relative to its own camera, but the player sees GTA's camera, so "forward" could come out backwards/sideways on foot. Hunting the camera object failed (see the entry below), so the offset is learned from behaviour instead.

**Tested how:** builds clean with MSVC. Not run in the game yet.

**Result (user 2026-10-12): tried, then REMOVED (a dead end, kept here for the record).** First the correction never armed: `vlen` is metres/second (thawVel * scale), but the gate compared it against 120, so it never passed. With that fixed the offset did learn and settled at only ~0-10 deg during normal walking — meaning the two cameras largely agree — spiking to 30-147 deg only when the view was turned. Applying it made on-foot movement feel *stiff and randomly misdirected*: the skater's velocity lags the stick during turns, so the estimate is unreliable mid-turn even with a steady-stick gate and a deadband. **The stick is now sent to reTHAWed unchanged again** (`RotateStickForCamera` removed); the camera keeps the right-stick behaviour the user likes. The learned offset being ~0 also suggests reTHAWed's camera and GTA's camera mostly agree, so little was lost.

## [2026-10-12] 8.32 research: find reTHAWed's on-foot state (log only)

**Changed:** new `src/thaw/statefind.{h,cpp}` (+ `main.cpp`, `CMakeLists.txt`): F3 while **on foot** and F4 while **on the board** dump the skater object's first 0x400 bytes, and each of its components' first 0x40 bytes, to `thaw.log`. Read only.

**Why:** user 2026-10-12: GTA's camera should be moveable only while the skater is on foot in skate mode; nothing reads an on-foot/on-board state yet (the skater reader is position/velocity only), so we need to find the field.

**Tested how:** builds clean with MSVC. Not run yet.

**Result:** camera hunt so far (user 2026-10-12). The two dumps show the camera is not a field of the skater (identical skater position; only timers/pointers changed), so it is a separate object. Memory scans did not separate it either: restricted to near the skater only timers/pointers changed; a full scan found 52 "movers" that were all copies of the **skater's own position** (they move whenever he does) plus a scratch buffer. Rotating reTHAWed's camera apparently requires moving, which floods the scan with the skater's own data. Not solved. Next options: (a) a runtime-learned stick correction - infer the camera offset from how the skater's actual movement differs from the stick + GTA's camera - with no camera object needed; (b) leave it. Awaiting the user's choice.

## [2026-10-12] 8.32 controls: keep GTA's camera (right stick) in skate / BMX mode

**Changed:** `src/gta/padlink.cpp`: `BlankSafe(touched, camera)` now keeps the right stick in GTA's pad while the rest is blanked; `OurUpdatePads` passes the camera through in skate / BMX mode, except while the radio wheel is open. `src/gta/radio.cpp/.h`: new `RadioWheelIsOpen()`. `src/gta/skatemode.cpp`: the DisablePlayerControls bit (kOurControlBit) is no longer kept set during normal skate mode (only while waiting for reTHAWed to place its skater), because GTA's camera ignores the right stick while any such bit is set; the pad is blanked regardless, so CJ still gets no input.

**Why:** user 2026-10-12: "not really able to move the camera around while on foot in skate mode"; asked to keep the skating side the same. reTHAWed still gets the right stick through the link, so its side is unchanged; only GTA's camera additionally receives it.

**Tested how:** builds clean with MSVC. First attempt (right stick only) did not work: `gta.log` showed the right stick reaching the games (RX 125 etc.) but the camera still would not move, which points at the control bit. Second change (clear the bit) built. Not run in the game yet.

**Result:** retest pending.

## [2026-10-12] 8.32 R3c: the rail pool (map tool + plugin)

**Changed:**
- `tools/gta_to_thaw_map.py`: new `--rail-pool N` (DESIGN 8.32 R3c). With it the finder runs with no budget, every real rail is written to `all_rails.csv` (node,rail,point,x,y,z), and the level instead gets N two-node rails parked far below the map (`make_pool_rails`). Both csv files are also copied to the fixed `extracted/thaw_maps/_railpool/`, which the plugin reads whatever the map is called. The inline rail-csv writer is factored out as `write_rail_csv`. `_railpool/rails_in_map.csv` is now written for every map build.
- `src/thaw/railscan.cpp/.h`: the scan now reads its targets from `_railpool/rails_in_map.csv` (was a hard-coded map folder). New `RailPoolTick`: on a pool map it builds the pool's 2-node rails from the block's own links, loads `_railpool/all_rails.csv` (2-node rails only; longer ones counted and skipped for now), and whenever the skater has moved 40 m copies the GTA rails within 150 m into the nearest pool slots (positions + boxes only, R3a-2), parking the rest far below and aside the map. It refuses to run if the level's 2-node rails don't match `_railpool/rails_in_map.csv`, so a normal map is left alone.
- `src/thaw/main.cpp`: calls `RailPoolTick` each pass.

**Why:** R3b (user 2026-10-12) proved a moved rail is grindable, so build the pool: the whole map can then be ground with only about 9,000 nodes loaded.

**Tested how:** tool `py_compile` plus a pool build of East LS (`--rail-pool 4500`, prefix MPGTALSP): level script 9,037 nodes (1 + 36 geometry + 9,000 RailNode), `_railpool/all_rails.csv` 21,309 rail nodes. Plugin builds clean with MSVC. Not run in the game yet.

**Result (test 1, user 2026-10-12 11:10): failed to start — my mistake, fixed.** The pool map was built with a new prefix (MPGTALSP, level ID 0xF2DCAF73), but the plugin only knows `load_MPGTALSE` = 0x9F014B98 (every one of our maps uses that prefix, including the whole-map build). So `inOurLevelNow` was false: no rail scan, and skate mode had no level alignment, which is what sent CJ "under the map" and made a flat road feel like a bowl. Fix: build the pool map with the normal prefix (MPGTALSE). Also parked the pool rails less extremely (about 150 m below the map instead of 1,500 m). Retest pending.

**Result (test 2, whole-map pool, user 2026-10-12): the pool works.** `_railpool/all_rails.csv` has the whole map (101,425 rails / 246,032 nodes); the plugin loaded 92,467 two-node rails, found all 4,500 pool rails, and placed 172-430 rails of 4,500 slots within 150 m as the skater moved around (never near the limit). The user could grind rails that were not grindable before. Still missing, and both are rail *supply*, not the pool:
- **(a) long rails (3+ nodes):** 8,958 of them (25% of all rail nodes, up to 116 nodes each) are skipped, because the pool only places two-node rails. So long kerbs / walls get no rail at all here — a visible gap versus the old map, whose rails were the *longest* ones.
- **(b) ledges taller than about 1.6 m:** the map tool's rail finder refuses to make a rail where the drop is over `RAIL_DROP` (1.6 m), so the top of a high ledge has no rail to grind even with a wallride (user 2026-10-12).
Next: fix (a) (long rails in the pool), then (b) (taller ledges in the finder).

**Fix 3 (2026-10-12, builds; not run yet):** (a) the plugin now splits each long GTA rail (3+ nodes) into consecutive two-node pieces that **share their endpoints**, so the pool places the whole chain and a grind should carry along it the way a Tony Hawk rail does (if reTHAWed connects rails whose ends touch). (b) the map tool's `RAIL_DROP` upper bound is raised from 1.6 m to 5.0 m, so a wallride-top ledge gets a rail (roof edges stay above 5 m).

**Fix 4 (2026-10-12, built; not run yet):** new map-tool option `--rail-drop-max M` (0 = no limit); the whole map now generates 193,945 rails / 443,029 nodes (was 147,329 / 343,976), so **building and structure tops** get rails (Tony Hawk style). A capacity check (grid sample of the map) found at most 3,460 rail pieces within 150 m anywhere — under the pool's 4,500 slots, so headroom remains. Note: no limit also adds some odd high rails (the highest is at 512 m — likely a mountain ridge); tune later if it feels wrong.

**Fix 5 (2026-10-12, built; not run yet):** the pool now logs when the skater is on a placed rail (`rail pool: ON a placed rail (speed, pos)` / `left the placed rail`), so grinds that stop can be seen in `thaw.log` (user: a grind through bushes stopped at a palm). Data check: palms and round trees have no rails on them (0 of 298 `sm_veg_tree4`, 0 of 60 `veg_palmkb1`), while solid bushes/hedges/planters do (`sm_bushytree` 101/101, hedges, planters 26/41) — so a palm in a hedge line is a real gap in the rail.

## [2026-10-10] 8.32 R3b test 2: show the test rail in GTA, detect grinding it

**Changed:** `src/common/link.h` (version 13: test rail ends + on flag), `src/thaw/railscan.cpp/.h`, `src/thaw/main.cpp`: F5 rail now 6 m ahead, 25 m long, 50 cm up; its ends go to GTA; the log marks samples with the skater on it and sums them up. New `src/gta/testrail.h/.cpp` (+ `main.cpp`, `CMakeLists.txt`): a row of 13 orange lights along the test rail while skate mode is on.
**Why:** test 1 (user 21:07 "I was able to grind with f5"): the log shows the skater rolled under the invisible test rail and ground a real map rail ~4 m beside it, so the test didn't show yet whether a moved rail can be ground.
**Tested how:** THAW and GTA files clang syntax-checked (Windows target, stand-in headers). Built and run in the game 2026-10-12 (user).
**Result (2026-10-12, user): PASSED — a moved rail is grindable.** The orange lights showed in GTA ("test rail: showing it in GTA ..."), F5 twice (rail moved, checksum 668276D2, no crash), and the log marks "ON THE TEST RAIL" 10 of 182 samples in test 1 = "GROUND IT" (0.0 from the rail, 0.0 above it, 4% -> 86% along it, speed 842-934) and 10 more samples in test 2 (27% -> 93%, speed 857; its 20 s summary line was not written: the game closed first). So reTHAWed keeps no hidden note of where rails are that would stop a relocated rail being ground: R3c (the rail pool) can go ahead. One user note: the test rail is placed from the skater's heading and does not snap to GTA ledges, as designed — that is the pool's job.

## [2026-10-10] 8.32 R3b test: F5 moves one rail in front of the skater

**Changed:** `src/thaw/railscan.cpp/.h`, `src/thaw/main.cpp`: the rail scan now only finds reTHAWed's block of rail node records (no more saving / pointer search), again after every level load, and forgets it when the level goes. New `RailTestTick`: F5 in our level moves one far-away 2-node rail (positions + boxes in its 2 records) to just in front of the skater and logs the skater's distance to it for 20 s.
**Why:** R3a-2 (user 20:55) decoded every rail node record: sorted by name, boxes = node to next node, links all correct, the "packed bits" are left-over bytes. So moving a rail should only need new positions and boxes; this tests that before building the pool.
**Tested how:** decoding: cloud script, all 8,999 records matched to rails_in_map.csv (links, boxes, sort order). Code: clang syntax check (Windows target, stand-in headers). User 21:07: F5 twice in the game.
**Result:** F5 works (rail moved, no crash), but the log shows the grinds were on a real map rail ~4 m beside the test rail; the skater passed under the test rail. Grinding a moved rail not shown yet: test 2.

## [2026-10-10] 8.32 R3a-2: save reTHAWed's rail node block (research, read only)

**Changed:** `src/thaw/railscan.cpp`: after finding the rail positions, walks to the start and end of reTHAWed's block of 0x60-byte rail node records, saves it to `logs\rails_array.bin`, and logs pointers to its start / end. Pointer search near single hits removed.
**Why:** R3a (user 20:49) found the record layout (positions, boxes, next / previous links, name checksum) but also packed bits that look like spatial sort keys; all records are needed to decode them before rails can be moved.
**Tested how:** clang syntax check (Windows target, stand-in headers). R3a itself ran fine in the game (no problems, 11 s on its own thread). User 20:55: ran it in the game.
**Result:** worked: 8,999 records saved (= every rail node), the holding object found; decoded (see the R3b test entry and DESIGN 8.32).

## [2026-10-10] 8.32 R3a: find reTHAWed's rail list in memory (research, read only)

**Changed:** `tools/gta_to_thaw_map.py`: writes `rails_in_map.csv` (node number, rail, point, exact THAW position as stored). New `src/thaw/railscan.h/.cpp` (+ `main.cpp`, `CMakeLists.txt`): 15 s after our GTA map level is up, on its own thread, finds 6 of those rail node positions in reTHAWed's memory and logs each place with the bytes around it, plus pointers into the first two. Reads only.
**Why:** user 20:40: go with the rail pool (R3) so every rail can be ground; 30,000 rail nodes froze reTHAWed after loading (not a pool limit), so the pool must be filled at run time, which needs reTHAWed's rail node layout.
**Tested how:** rail scanner syntax-checked with clang (Windows target, stand-in headers). Not run yet.
**Result:** not tested yet.

## [2026-10-10] 8.32 R1: rail totals and reTHAWed pool sizes from the converter

**Changed:** `tools/gta_to_thaw_map.py`: prints all rails found before the budget (rails, nodes, km); `--rail-budget 0` = no limit; prints the `rethawed.ini` `[Memory]` values the built map needs (RailPoint, RailString, HashItem, CStruct, CComponent, CVector).
**Why:** user 20:10: get around the 9,000-node rail cap; the cap is most likely reTHAWed's memory pools, which its own ini can raise (DESIGN 8.21 / 8.32).
**Tested how:** compiles; needs a run on the PC (gta3.img).
**Result:** not tested yet.

## [2026-10-10] 8.31 fix 3: holes in the map (placements with flag bits were dropped)

**Changed:** `tools/gta_area_dump.py`: placements are kept when the low 8 bits of their interior number are 0 or 13 (flags above them no longer drop them); every placement left out is recorded with its reason. `tools/gta_to_thaw_map.py`: writes `skipped.csv`.
**Why:** user 19:41, 11 wall marks: every "invisible wall" was the skater bouncing back at the edge of the map's collision, with nothing beyond (thaw.log path + map collision); the whole-map picture shows big holes inside the land.
**Tested how:** cloud: wall marks matched to the 9-per-second skater path and the map's collision (all 11 at a collision edge or a real step); text map files show flagged interior numbers. User 20:07: map rebuilt, skated LS -> LV and back.
**Result:** works: no invisible walls. Map now 855 tiles, 2.58M triangles (+335k). skipped.csv: only far-away stand-ins and interiors left out.

## [2026-10-10] 8.31 fix 2: wall marks, placements list, GPS start fix

**Changed:** `src/gta/skatemode.cpp`: touchpad click alone in skate mode logs "WALL MARK #n" (CJ's and the skater's position, speed). `tools/gta_to_thaw_map.py`: writes `placements.csv` (model, map file, box, triangles for every GTA placement in the map). `src/gta/gps.cpp`: no re-routing while CJ hasn't reached the route's first road point yet (he may start far from a road).
**Why:** user 19:13: still some invisible walls (no crash, F8 fine); logs: every stop / bounce found had real GTA collision there, so the walls need marking and naming. gta.log: GPS re-routed every 0.5 s while CJ stood 45 m from a road.
**Tested how:** cloud: GPS run on the real road files from that same spot (no re-route; full trip finishes); converter compiles. Earlier fix 1 confirmed in the game: no crash, placement and barriers work (see DESIGN 8.31 test 2).
**Result:** not tested yet.

## [2026-10-10] 8.31 fix 1: crash on CJ speaking, wobbly GPS line, skater stopped by walls on its way to CJ, barriers by name

**Changed:**
- `src/gta/skatemode.cpp`: skate mode no longer removes CJ's talking-face task (only look-at); placing the skater at CJ is asked again up to 3 times when reTHAWed's skater ends up elsewhere.
- `src/thaw/bmx.cpp`: placement also sets the skater's previous position (+0x78, guarded) and logs both before the move.
- `src/gta/gps.cpp`: progress search starts at the piece CJ is on.
- `tools/gta_to_thaw_map.py`: barriers left out by model (texture sets roadblkx / barrierblk: 24 models), instead of "big box with a road through it".
**Why:** user 18:37: GTA crashed twice (once with skate mode off), GPS line wiggly on the radar, skate mode once went to the skatepark and F8 + skate mode jumped CJ back to the skater, walls on a bridge to the countryside. Crash lines (D4's first catch): gta_sa.exe 0x691238 writing 0x10, args 7 / 0xAF0 = speech asking the missing talking-face task to talk.
**Tested how:** cloud only: GPS code run on the real road files (5.8 km trip, no re-route); barrier list from the user's IDE files (24 models); disassembly of the crash address and its three callers (registers match the 0x4E5E7C call). Not tested in the game.
**Result:** not tested yet.

## [2026-10-10] 8.31 D1-D5: no region barriers in the map, skate mode anywhere, F8 fixed, crash log, GPS

**Changed:**
- D1 `tools/gta_to_thaw_map.py`: reads GTA's car roads (`data\Paths\NODES*.DAT`, read only) and leaves out collision boxes at least 4 m tall and 5 x 12 m across that a car road runs straight through (the bridge barriers); `removed_barriers.txt`, count in `map_info.txt`.
- D2 `src/gta/skatemode.cpp`: in our whole-map level, turning skate mode on moves reTHAWed's skater to CJ (link `gtaPlace*`, as BMX mode) instead of CJ to the skater; starts once the skater is there (max 1.5 s, else the old way); not when within 3 m already; refused inside interiors.
- D3 `src/gta/teleport.cpp`: F8 loads the target's collision too and moves CJ only once real ground was found (tries up to 3 s).
- D4 `src/gta/main.cpp`: `gta.prev.log` kept; crash handler logs error, address, module, registers, CJ's position, skate mode; normal exit logged. `src/thaw/main.cpp`: `thaw.prev.log` kept.
- D5 new `src/gta/gps.h/.cpp` (+ `CMakeLists.txt`, `main.cpp`): GTA's car roads read at start, A* route to the waypoint, purple line on the radar (clipped to its circle) and the pause map, rerouted when 40 m off, waypoint removed on arrival (15 m).
**Why:** user's whole-map test (15:34, 17:55): invisible walls on the bridges to SF and LV (thrown into the water near SF), skate mode always jumped CJ to the skater, F8 to SF fell through the map and crashed, one more crash; asked for a GTA V style GPS and skate mode on anywhere.
**Tested how:** in the cloud only. Map file (copied, read only): the two walls the skater hit are 12-triangle boxes across the road (SF end of the Flint bridge 29 x 20 x 16.7 m; LV bridge 40 x 20 x 10 m). The barrier rule run on all 25,974 tall boxes of the current map with the real road files picks 15: every bridge out of Los Santos (Flint, both SF ends, LV, Red County - Bone County) plus two 100+ m boxes on the Gant Bridge road; posts and pillars stay. GPS code built on Linux with stand-in GTA headers and run on the real road files: 64 files, 30,587 points, 59,620 links; LS skatepark -> SF route 6.4 km, LS -> LV 3.8 km; radar clip and arrival work. Converter, skate mode, F8, crash log: not tested (need the PC).
**Result:** not tested in the game yet.

## [2026-10-10] C1: reTHAWed "lean drawing" (its unseen 3D world not drawn behind GTA)

**Changed:** `src/thaw/drawlog.h/.cpp`: while reTHAWed is in a level and behind GTA, its world draws before the frame's HUD part are dropped (the HUD is drawn as before); switch on by default, touchpad + D-pad Right toggles; 10 s log of dropped / drawn draws. `src/thaw/touchpad.cpp`, `src/common/link.h`: D-pad Right bit. `src/thaw/main.cpp`: passes the toggle.
**Why:** user 14:47: fix the occasional stutter without lowering graphics; GTA ran at 44-58 fps while both games draw.
**Tested how:** user 15:02: BMX and skating; logs read.
**Result:** smooth, HUD still there. About 98% of reTHAWed's draws dropped while behind GTA; GTA at 59-60 fps (585-600 frames per 10 s, was 440-580).

## [2026-10-10] B12: the GTA bike lies where reTHAWed's dropped bike lies

**Changed:** `src/common/link.h`: version 12 (`_v12`, 636 bytes), `thawDroppedBike` + `kDroppedBikeValid`. `src/thaw/bikeparts.h/.cpp`: `DroppedBikeTick` (bike frame in THAW's world while the rider is off the bike; sent only once it's known to stay put). `src/thaw/main.cpp`: sends it. `src/gta/bmxmode.cpp`: in skate mode the GTA bike CJ left mirrors it (collision off meanwhile); Square mount keeps it there.
**Why:** user 14:37 (clip): getting on with Square looked off: CJ bent down for a bike that wasn't there, then it popped in.
**Tested how:** user 14:47.
**Result:** "a lot better, I'm fine with how that works".

## [2026-10-10] B11: BMX <-> skate the quick way (hop-on copied; Square gets back on the bike)

**Changed:** `src/gta/bmxmode.cpp`: L1 + R1 + rider off = no 1 s wait; CJ taken off the GTA bike at once (GTA's set-ped-out task, warp flag), skate mode starts at once; the bike's GTA handle remembered. In skate mode, reTHAWed's rider getting on his bike (Square) puts CJ on a GTA BMX at once (the one he left within 30 m, the nearest free one within 15 m, or a new one via GTA's vehicle cheat), placed where CJ is; BMX mode starts without the 3 s wait. `src/gta/skatemode.h/.cpp`: `SkateModeTurnOffNow`.
**Why:** user 14:25 (clip): in reTHAWed the board rolls to the rider and he hops on; GTA missed it. Also: get on the bike from skate mode with reTHAWed's Square prompt.
**Tested how:** user 14:37 (clip + gta.log).
**Result:** hop-on "a lot better"; Square mount works but looks off (bike not there while CJ picks it up): B12.

## [2026-10-10] B10 fix 1: the GTA bike stays put while the rider gets off

**Changed:** `src/gta/bmxmode.cpp`: while reTHAWed's rider is off the bike (thawBikeState not "on"), BMX mode no longer follows: the GTA bike is held at its last placement, upright, speed zero; no turned picture, no bike parts, CJ's body copy dropped. Logged.
**Why:** test 14:17 (clip): the bike rolled up like a skateboard while getting off (it was copying the rider's board bone).
**Tested how:** user 14:25 (clip).
**Result:** bike stays put. Still: reTHAWed's hop-on missed (B11).

## [2026-10-10] B10: L1 + R1 goes from BMX mode to skate mode; hands / feet ease in

**Changed:** `src/gta/skatemode.h/.cpp`: `SkateModeRequestOn(why, ms)`: skate mode switches on by itself as soon as GTA allows (CJ out of the vehicle, no script, not paused), for up to that long; F7 / L3 + R3 cancel it. `src/gta/bmxmode.cpp`: when the rider gets off the bike in reTHAWed (L1 + R1), after CJ is told to get off, skate mode is requested (6 s); the hands / feet "on" amounts ease with a 0.1 s time constant.
**Why:** user 14:11: L1 + R1 should go from BMX to skate (L3 + R3 already = GTA's normal BMX); awkward moment when CJ starts pedalling again.
**Tested how:** user 14:17 (clip).
**Result:** BMX -> skate works; the bike rolled up like a board while getting off (fix 1).

## [2026-10-10] BMX B9 fix 1: riding pose safe from tricks; feet by distance to the crank axle

**Changed:** `src/thaw/pose.h/.cpp`: `ReadSkeletonBind` (a skeleton's default pose). `src/thaw/bikeparts.cpp`: straight bars from the bike's default pose; riding pose only taken while the bars are straight; feet on the pedals within 22 units of the crank axle (off beyond 32); log of the feet's distances.
**Why:** test 13:57: tricks show, but the riding pose was taken mid-trick and feet were counted off.
**Tested how:** cloud maths tests; user 14:11.
**Result:** tricks look good, feet look good most of the time; awkward moment when pedalling again (B10 eases it); slight stutter left: GTA runs at 44-60 fps while riding (gta.log pacer lines).

## [2026-10-10] BMX B9: the GTA bike from reTHAWed's own bike skeleton

**Changed:** `src/thaw/bikefind.h/.cpp`: quick finder for the bike skeleton (rider's components, then 2 MB around the rider's skeleton; bone checksums named). `src/thaw/bikeparts.h/.cpp`: rewritten on the bike's bones: frame (sent as the board, lined up with Bone_Board_Root once), fork (Bone_Bike_Front), crank (crank + pedal bones), hands / feet on grips / pedals. `src/thaw/pose.h/.cpp`: `ReadSkeletonBones` for any skeleton. `src/thaw/main.cpp`: uses them; the 15 s scan removed.
**Why:** B8 found the bike's skeleton; its bones carry the bar spins (Front 178 deg), tailwhips (Frame 180), crank flips.
**Tested how:** cloud maths tests; user 13:57 (video + logs).
**Result:** "the tricks are looking a lot better": tailwhips, bar spins, tabletops, crank flips show. Feet mostly counted off (riding pose taken mid-trick): fix 1.

## [2026-10-10] BMX B8 (research): find reTHAWed's bike skeleton

**Changed:** new `src/thaw/bikefind.h/.cpp` (log only): once per mount, 6 s after getting on, scans reTHAWed's memory for skeletons, logs them, follows the small ones that aren't the rider's and logs which bones turn each second. `src/thaw/main.cpp` calls it; `CMakeLists.txt` adds it. STATUS.md written (two attempts at the tricks).
**Why:** test 13:28: bars don't spin, tailwhips don't show; reTHAWed's rider bones don't carry those tricks; the bike is its own model (veh_bmx).
**Tested how:** user 13:45, thaw.log read.
**Result:** found it: an 8-bone bike skeleton next to the rider's; its Front bone turns in bar spins, Frame in tailwhips (DESIGN 8.29 B8 result).

## [2026-10-10] BMX B7 fix 1: feet on the pedals; checks that the parts reach the screen

**Changed:** `src/thaw/bikeparts.cpp`: foot "on the pedal" = keeps its distance from the crank axle and its place along it (as in the riding pose); the crank's angle from the rider's feet while on, from the crank bone while off; per-second log of feet vs crank bone turning. `near` / `far` renamed (Windows macros; build error 13:11). `src/gta/bmxmode.cpp`: per-second log of the bars' turn as GTA will draw it.
**Why:** test 13:15 (video + logs): feet counted off the pedals nearly all the time; legs looked unnatural; tricks not visible.
**Tested how:** user 13:28: video + logs.
**Result:** bars don't spin, tailwhips don't show (data source wrong: STATUS.md); feet still mostly counted off the pedals; "bars as drawn" matched what was sent (GTA side works).

## [2026-10-10] BMX B7: bars and cranks like reTHAWed's; CJ's hands on the grips, feet on the pedals

**Changed:**
- `src/common/link.h`: version 11 (`Local\MyPassthrough_v11`, 588 bytes): `thawBike` (16 numbers: the fork's turn, the crank's direction, hand / foot "on" amounts for the bike's right and left), flag `kBikeValid`.
- `src/thaw/bikeparts.h/.cpp`: besides the log, works those numbers out every pass (riding pose 4 s after getting on: where the hands sit on the bars and the feet on the pedals; then how far they are from there now). `src/thaw/main.cpp`: sends them.
- `src/gta/blend.cpp`: blends them like the rest.
- `src/gta/bmxmode.cpp`: at BMX start finds the GTA bike's parts and measures its grips from the bars' mesh (fixed guess if it can't); each frame works out the grips and pedals in the world on the drawn bike and hands them to body.cpp; while the bike is drawn: chassis unturned (no extra GTA lean), fork + handlebars turned like reTHAWed's fork, chainset pointed like reTHAWed's crank, pedals level; all put back after drawing. Per-second log line for it.
- `src/gta/body.h/.cpp`: after copying the body, bends each arm / leg (two-joint reach, elbow / knee keeps its side) so the hand / foot lands on its grip / pedal, by the "on" amount.
**Why:** user 12:54: riding smoother, but CJ's hands don't line up with the bars and look unnatural (screenshots: arms hanging beside the bars); trick moves (bar spins, crank flips). Research log (B6): Board_Nose + Trucks_Nose = fork, Board_Tail + Trucks_Tail = cranks. DESIGN 8.29 B7.
**Tested how:** cloud maths tests; build error 13:11 (`near` / `far` are Windows macros: renamed); user test 13:15 with a video, logs read.
**Result:** hands better, slightly off at moments; feet / legs unnatural (feet counted off the pedals nearly all the time: fix 1); tricks not visible yet; fork / crank numbers do reach GTA.

## [2026-10-10] BMX B5 + B6: smooth riding; research log of the bike's moving parts

**Changed:**
- New `src/gta/blend.h/.cpp`: skate mode's smoothing (last 8 reTHAWed frames, shown 40 ms late, blended, on an even frame clock, orientation smoothed), moved out of `skatemode.cpp` unchanged as a class. `skatemode.cpp` uses it (same behaviour).
- `src/gta/bmxmode.cpp`: BMX mode uses the same smoothing instead of the newest frame plus a guess ahead. Once per game session, when BMX mode starts, logs the GTA BMX's moving parts (`bmx parts:` lines). Per-second line now says how many frames were blended.
- New `src/thaw/bikeparts.h/.cpp` (log only): while reTHAWed's rider is on the bike, each of the 4 other "board" bones' turn relative to the bike frame (Board_Root), compared with the riding pose; `bike parts:` lines when one turns more than 20 degrees, plus a per-second summary. `src/thaw/main.cpp`: calls it; also logs the bone list again if the skeleton changes.
- `CMakeLists.txt`: the two new .cpp files.
**Why:** user 12:41: bike stays with CJ now (fix 3 works), but riding stutters like the old skate stutter, and tricks (tabletops, crank flips, bar spins) don't show. DESIGN 8.29 B5, B6.
**Tested how:** new files syntax-checked in the cloud (stub Windows headers); angle maths checked. User 12:54: rode, tricks, screenshots; logs read.
**Result:** riding "a lot smoother", slight stutter left in flips. Research log: fork = Board_Nose + Trucks_Nose, cranks = Board_Tail + Trucks_Tail (see DESIGN 8.29). CJ's hands don't reach the bars (screenshots). Next: B7.

## [2026-10-10] BMX B4b fix 3: the bike's picture really updated (GTA's UpdateRW)

**Changed:** `src/gta/bmxmode.cpp`: the drawing swap now calls GTA's own CEntity::UpdateRW (0x446F90, plugin-sdk `UpdateRwMatrix`) to copy the matrix into the RenderWare frame the bike is drawn from; we had called `m_matrix->UpdateRW()`, which does nothing because GTA doesn't attach that matrix to the frame (GTA's physics calls CEntity::UpdateRW itself, which is why the real bike moved fine). Same call now when following and when putting the bike on the ground at the end. F5 test removed (F5 belongs to another mod).
**Why:** F5 test 12:30: the swap never showed on screen.
**Tested how:** user 12:41: rode with tricks.
**Result:** works: the bike stays with CJ and flips with him. Left: stutter; trick moves (B5, B6 above).

## [2026-10-10] BMX: F5 draw test

**Changed:** `src/gta/bmxmode.cpp`: in BMX mode, F5 switches a test on / off: the GTA bike is drawn upside down and 1 m higher than the real bike (logged). Nothing else changed.
**Why:** STATUS.md 02:10: find out whether changing the bike right before GTA draws it shows on screen at all.
**Tested how:** user 12:30: F5 in BMX mode (log: test on / off 8 times).
**Result:** no upside-down bike seen; F5 also opens another mod's white debug text. So changing the bike right before drawing never reached the screen. Also: "when CJ does a back flip on the bike he flips back to the bike, the bike does not flip with him".

## [2026-10-10] BMX B4b fix 2: the GTA bike drawn exactly where reTHAWed's bike is

**Changed:** `src/gta/bmxmode.cpp`: the GTA bike's picture (drawn turned, real bike untouched) = reTHAWed's bike (board bone): its right, its forward (minus the bone's "up"), its up (the bone's "at"); centre = the bone's position lifted by (GTA bike's wheel-to-centre height - 17.8 THAW units), so at rest the wheels sit as before. Fixed numbers from the research log; no lining up at run time (that was what spoiled both earlier attempts). The once-a-second log keeps the board bone values and how many frames were drawn there.
**Why:** research log 01:59 (above); STATUS.md.
**Tested how:** user 02:04: rode, tricks, L3 + R3.
**Result:** gun no longer stuck (release guard works); bike "still not staying with CJ". gta.log: drawn at reTHAWed's bike ~55 frames/s, values sensible. Third attempt: stopped (STATUS.md).

## [2026-10-10] BMX: research log for the bike, no false starts, no buttons leaking into GTA

**Changed:** `src/gta/bmxmode.cpp`: turning the GTA bike's picture switched off (bike upright as in B4a) until we know how reTHAWed's bike sits; once a second while BMX mode is on the log gives the board bone's axes and position in the rider's own space, the speed, and how far hips / board turned (research for B4b). BMX mode starts only when CJ is fully seated (GTA ped state "driving") and 3 s after we last asked reTHAWed to switch the rider. `src/gta/padlink.cpp`: when skate / BMX mode ends, GTA gets the controller back only once all buttons and triggers are let go.
**Why:** STATUS.md (01:55); test 01:48: false start threw CJ off; "gun stuck shooting" after L3 + R3 on the bike (buttons held for reTHAWed reached GTA at once; on a GTA bike one of them fires).
**Tested how:** user 01:59: rode straight, flips, barspins / tailwhips, wheelies (order not remembered).
**Result:** "the bike was still not fully staying with CJ" (expected: picture turning was off). Research log (91 lines): riding normally the board bone in the rider's space is right (1, 0, 0), "up" (0, 0, -1) = towards the back, "at" (0, 1, 0) = up, 17.8 units above the feet: exactly the skateboard's set-up, so it IS the bike. In hops it rises to ~33 units and pitches; in spins it turns 90 degrees and more; in bails it flies far off (-280 units).

## [2026-10-10] BMX B4b fix 1: the bike's picture follows the rider's hips

**Changed:** `src/gta/bmxmode.cpp`: the GTA bike's picture now follows the rider's hips and torso (origin the hips; up hips to neck; right left thigh to right thigh) instead of the "board" bone, lined up the same way at the 10th frame. Per second the log says the most the rider's hips and the board bone turned (to see whether the board bone moves on the bike at all).
**Why:** test 01:40: no change on screen though the swap ran every frame. CJ's body flips correctly, so the rider's bones do carry the tricks.
**Tested how:** user 01:48: tricks, flips, L3 + R3.
**Result:** "still janky"; also "when pressing L3 + R3 my gun got stuck shooting". gta.log: lined up 1 s after start, while reTHAWed's get-on animation (~2 s) was still playing; the board bone does turn (56-163 deg). BMX mode once started while CJ was still getting on / reTHAWed still switching (151-160 s) and CJ was thrown off. STATUS.md written (two attempts).

## [2026-10-10] BMX step B4b: the GTA bike drawn where reTHAWed's bike is

**Changed:** `src/gta/bmxmode.cpp/.h`, `main.cpp`: new `BmxAddHook` (GTA's vehicle-draw event, before and after). In BMX mode, the 10th followed frame notes how reTHAWed's bike (the rider's "board" bone, from the link, flips included) sits relative to the GTA bike (both as they are then: rider standing on the bike); after that, each frame works out the GTA bike's picture: the same turn and shift reTHAWed's bike made since then. Right before GTA draws our bike, its matrix is set to that picture; right after drawing, it's put back, so GTA's camera and physics keep the upright bike. Logged: the lining up, and per second how many frames were drawn turned.
**Why:** test 01:33 (B4a above): CJ flips but the GTA bike doesn't, and it drifts away from CJ in jumps.
**Tested how:** user 01:40: tricks and flips.
**Result:** no visible change ("behaved the same way as before"). gta.log: lined up at 47 s, the bike was drawn turned in ~50 frames a second, so the drawing swap ran; most likely reTHAWed's "board" bone doesn't move with its bike in BMX mode (fix 1 logs it).

## [2026-10-10] BMX step B4a: CJ's body does the rider's tricks

**Changed:** `src/gta/bmxmode.cpp`: each BMX-mode frame, the rider's bones (with reTHAWed's flips already applied by the THAW side) go to the same body code skate mode uses (`BodySetTarget`, body.cpp), with the rider's origin at the GTA spot the bike follows; cleared when BMX mode ends (CJ's own riding animation again). The once-a-second BMX log also says how many drawn frames had CJ's body copied. The GTA bike still stays upright (B4b).
**Why:** DESIGN 8.29 B4; user 01:27: start B4 (first-mission check left to players after release).
**Tested how:** user 01:33: tricks and flips in BMX mode.
**Result:** CJ's body does the flips ("he flips but the bike doesn't"); "the bike looked like it was getting away from CJ as he jumped": the GTA bike stays at the rider's feet spot, upright, while the rider (and reTHAWed's bike) move and turn in tricks (B4b).

## [2026-10-10] BMX B3 fix 3: getting off in reTHAWed gets CJ off too

**Changed:** `src/gta/bmxmode.cpp`: while BMX mode is on, GTA no longer asks for the rider to be on the bike (wish "leave it"), so getting off in reTHAWed (L1 + R1) stands. The rider off for 1 s (was 2.5 s) ends BMX mode, and CJ then gets off the GTA bike with GTA's own get-off task (CTaskComplexLeaveCar); logged.
**Why:** user 01:14: L1 + R1 left CJ sitting on the bike; we had put the rider straight back on.
**Tested how:** user 01:25: bails and L1 + R1 in BMX mode.
**Result:** works: "bailing did not end BMX mode and L1 + R1 got off the bike". B3 done (riding, jumps, radio wheel, no replay / floating, getting off).

## [2026-10-10] BMX B3 fix 2: no GTA idle camera, bails don't end BMX mode, bike onto GTA's ground

**Changed:** `src/gta/padlink.cpp`: while skate / BMX mode blanks GTA's pad, GTA is told the pad was just touched (CPad LastTimeTouched), so it doesn't start its "idle" cinematic camera. `src/gta/bmxmode.cpp`: in BMX mode GTA's cinematic car camera is switched off if it starts (logged once); GTA's replay mode logged if it ever runs; reTHAWed's rider "off the bike" ends BMX mode only after 2.5 s (a bail drops it briefly); when BMX mode ends the bike is put on GTA's ground under it (GTA's ground finder), speed 0, physics woken, logged; when CJ leaves the bike during BMX mode the log says why (in vehicle, which vehicle, ped state, knocked-off flag, bike health, replay mode, control flags). STATUS.md: the analysis.
Also `src/gta/padlink.cpp`: GTA's replay is blocked while skate / BMX mode is on (its "replay allowed" byte 0x8A6160 cleared, put back afterwards).
**Why:** test 01:01 (see above and STATUS.md); user 01:06: it's GTA's real replay ("REPLAY" in the corner, a playback of what CJ did) when changing radio station. GTA starts it on the keyboard's F1 (CReplay::Update 0x460500) unless a control flag is set; skate mode sets one, BMX mode doesn't. Something sends F1 then (probably DualSenseX mapping a button). The replay may also explain the floating and CJ leaving the bike (GTA puts things back after a replay).
**Tested how:** user 01:14: rode, changed stations, F7 / L3 + R3 off and on, L1 + R1 in reTHAWed.
**Result:** replay and floating gone; stations change; bike put on GTA's ground at each switch-off (0.02-0.05 m). Not working: L1 + R1 (get off in reTHAWed): "CJ just stayed on the bike and didn't move"; thaw.log: rider off, then our toggle put him back on 2 s later (GTA still wished "on the bike"), so BMX mode never saw a lasting "off". No "CJ off the bike" events this time.

## [2026-10-10] BMX B3 fix: bike upright, GTA stunt jumps off, physics woken at the end

**Changed:** `src/gta/bmxmode.cpp`: the GTA bike now stays upright and only turns to the rider's heading (from its facing, or its travel direction when the facing points up a vert wall, or the bike's own when standing still); no more flips / tilt on the GTA bike. GTA's unique stunt jumps (CStuntJumpManager::m_bActive) are switched off while BMX mode is on (not in the middle of one) and put back as they were afterwards. When BMX mode ends, the bike's physics is woken (not static, a small downward speed).
**Why:** test 00:51 / 00:52: tilting the bike with the rider's flips likely swung GTA's bike camera and made GTA throw CJ off a "tipped over" bike; flying through East LS's unique stunt jumps started GTA's slow-motion stunt replay; after BMX mode GTA kept the bike frozen in the air. Tricks will be shown on CJ's body in B4.
**Tested how:** user 01:01: rode with the radio wheel, L3 + R3 off.
**Result:** not fixed: "when using the radio it still tried to do a replay and CJ ended up floating in the air on the bike again". gta.log: CJ "got off the bike" twice mid-ride (cause not logged); a 2 s "rider off" blip (bail) switched BMX mode off. STATUS.md written (two attempts).

## [2026-10-10] BMX step B3: BMX mode (controller to reTHAWed, GTA bike follows the rider)

**Changed:** `src/gta/bmxmode.cpp` rewritten (B1 and B2 kept): when CJ rides a BMX, reTHAWed's rider is on its bike, reTHAWed's level is lined up with GTA's world, and GTA isn't in charge (cutscene, black bars, a script switched player controls off, CJ dead / arrested), the rider is first put at the GTA bike (B2), then BMX mode starts: link `gtaSkateMode` = 2 (the THAW side takes GTA's controller as in skate mode), CJ can't be knocked off the bike, the GTA bike's collision off; each world update the GTA bike is put at the rider (newest sample, up to 50 ms ahead by its speed; facing and tilt from the rider; speed passed on so the wheels turn). Off (all restored) when CJ gets off, GTA takes charge (it starts again by itself afterwards, rider placed again), the level isn't lined up, reTHAWed disconnects, F7 / L3 + R3, or the rider gets off in reTHAWed (the last two: GTA's normal BMX for the rest of this ride; F7 / L3 + R3 switches it back on). GTA menu open: held. Logs every change and once a second while on. `src/gta/skatemode.h`: `BmxModeIsOn`, `RideModeIsOn` (skate or BMX). `src/gta/padlink.cpp`, `radio.cpp`, `hudshow.cpp`: pad blanking, radio wheel, reTHAWed's HUD in BMX mode too. `src/thaw/input.cpp`: GTA's controller goes to reTHAWed when `gtaSkateMode` is 1 or 2.
**Why:** DESIGN 8.29 B3; user 00:07: radio wheel in BMX mode as in skate mode; missions must stay completable (CJ stays on GTA's bike; GTA in charge = GTA's bike).
**Tested how:** user 00:51 / 00:52: rode, jumped, radio wheel, L3 + R3 off.
**Result:** partly: riding and jumping "seemed to work fine"; radio wheel worked (station changed). Problems: "weird camera angles", "kept doing a replay"; CJ came off the GTA bike twice while reTHAWed's rider stayed on (gta.log "CJ got off the bike" at 117 s and 131 s); after L3 + R3 off, CJ "floated in the sky". See fix below.

## [2026-10-10] BMX step B2: reTHAWed's rider moved to the GTA bike

**Changed:** link version 10 (`Local\MyPassthrough_v10`, `src/common/link.h`): `gtaPlacePos` / `gtaPlaceAt` / `gtaPlaceSeq`, `thawPlaceDone`. `src/gta/bmxmode.cpp`: once reTHAWed reports the rider on its bike after CJ got on, and reTHAWed's level is lined up with GTA's world (Los Santos East or our GTA map), sends the GTA bike's spot (ground under it) and facing, turned into reTHAWed's units (inverse of the level mapping); 1 s later logs where the rider ended up in GTA terms and how far from the bike. `src/thaw/bmx.cpp`: does it on reTHAWed's game thread the way reTHAWed's own Obj_SetPosition (0x0042E9E0) does (position at +0x68, then each component's "teleported" function, vtable +0x24), plus facing (+0x88, level, the matrix's own handedness kept) and speed 0; code checks for that function. Logged. The GTA bike doesn't follow the rider yet (B3).
**Why:** DESIGN 8.29: where GTA (or a mission) puts CJ must count, so the rider goes to the bike, not the other way round.
**Tested how:** user 00:38: got on a BMX in East LS away from the rider.
**Result:** works ("worked as intended"); gta.log: rider 0.00 m from the GTA bike, height difference 0.00 m, 1.3 s after getting on.

## [2026-10-10] BMX B1 fix: one toggle per change

**Changed:** `src/thaw/bmx.cpp`: the bike toggle runs once when GTA's wish changes; again only if reTHAWed still hasn't switched 8 s later (was every 1.5 s).
**Why:** test 00:25: getting off looped (see B1 below).
**Tested how:** user 00:29: got on and off the BMX in GTA.
**Result:** works: "the skater got on and off the bike when CJ did". B1 done.

## [2026-10-10] BMX step B1: reTHAWed's rider gets on / off the bike with CJ

**Changed:** link version 9 (`Local\MyPassthrough_v9`, `src/common/link.h`): `gtaBikeWish` (kBike*), `thawBikeState` (kBikeState*). New `src/gta/bmxmode.h/.cpp`: each world update, CJ riding a BMX (model 481, as its rider, skate mode off) -> wish "on the bike"; after getting off -> "off" until reTHAWed reports off; logs changes both ways. New `src/thaw/bmx.h/.cpp`: code checks; on reTHAWed's game thread (our controller hook, every 100 ms) reads the rider's "on bike" byte (component 0x5BD63B29 +0x524, via GetComponent 0x0042E8C0) and, when it differs from GTA's wish (in a level, not paused, at most every 1.5 s), runs reTHAWed's own bike toggle script 0x27065DA5 through 0x004135E0. `main.cpp` (both), `src/thaw/input.cpp`, `CMakeLists.txt`: wiring. Plan: DESIGN 8.29 (mission rules, steps B1-B4).
**Why:** user 00:07: BMX mode when CJ gets on a BMX; first missions must stay completable; radio wheel as in skate mode (B3).
**Tested how:** user 00:25: F6 BMX, got on and off.
**Result:** getting on works (rider on the bike 2.8 s after CJ). Getting off looped: "stuck in a loop getting off the bike" until GTA was closed. thaw.log: a toggle every 1.5 s while the byte stayed "on"; a switch takes about 3 s, so each new toggle restarted the dismount.

## [2026-10-09] Tags T1 fix 3: reTHAWed's exact tag spot, tag shape from gta3.img, kept fully sprayed

**Changed:** link version 8 (`Local\MyPassthrough_v8`, `src/common/link.h`): new `thawTagPos` / `thawTagNormal` and bit `kTagSpotOk`. `src/thaw/tags.cpp`: the tag-placing call's two vectors are logged; the one of length 1 is the facing direction, the other the spot; both go into the link before the count. `src/gta/skatemode.cpp/.h`: new `SkateModeThawToGta` (a reTHAWed point / direction mapped like CJ). `src/gta/tags.cpp`: the tag goes exactly where reTHAWed put it (wall or ground), else as before; the tag model's shape (facing side, centre) is read once from GTA's own `models\gta3.img` (tag_01.dff, read only, nothing written or copied); its picture layers and the sprayed value right after GTA builds it are logged; our tags are set fully sprayed (255) every frame.
**Why:** user 23:47: "a couple of wonky versions of the purple ballas tag; it didn't appear in the bowl but sort of showed up in the street standing up; we want the green grove street tag". gta.log: 4 tags placed, all on the ground (no GTA wall where reTHAWed's bowl is), "tag model has no readable vertices" (so the facing was a guess: wrong). Purple Ballas = the Grove layer wasn't faded in.
**Tested how:** user 00:03 (10-10): rebuilt, tagged about 9 times in skate mode.
**Result:** works: "seems to be working perfectly now". Green Grove tags, lying flat. gta.log: the model read from gta3.img (8 vertices, 4 triangles, 2 x 2 m, facing -X, from triangles); all 9 tags went on GTA's ground under CJ. thaw.log: reTHAWed's two vectors are two points 24 units apart straight up and down (a downward line, not spot + direction), so "spot not told apart" every time and GTA used its own ground; reTHAWed's flag said "wall" each time although they were evidently ground tags, so that flag's meaning is probably the other way round. Still open: tags on walls not tried; the picture-layer log line found no geometry (harmless).

## [2026-10-09] Tags T1 fix 2: count the call that places the tag

**Changed:** `src/thaw/tags.cpp`: instead of the score-script call (0x005F933A) we now go through the call at 0x005F92E0, which puts the finished tag in reTHAWed's world (to 0x005F8680, 12 arguments; the 2nd says wall or ground). Code checks for it. New link bit `kTagOnWall` (`src/common/link.h`). `src/gta/tags.cpp`: looks for a GTA wall only when reTHAWed's tag went on a wall; otherwise straight to the ground. DESIGN 8.28 (a) corrected.
**Why:** test 23:36 (user 23:38: "The tag still did not show up"): thaw.log "6 tags started, 0 finished"; gta.log "started / stopped spraying" 6 times, no "finished". The score script only runs with a combo going (0x005F92F6), so a standing tag never reached our hook.
**Tested how:** user 23:47: rebuilt, tagged 4 times (3 on reTHAWed walls, 1 ground).
**Result:** partly: 4 tags placed in GTA, no crash. But they showed the purple Ballas layer, were placed on the street (no GTA wall at reTHAWed's bowl) and stood up instead of lying flat (see fix 3).

## [2026-10-09] Tags T1 fix: reTHAWed changes one checked call

**Changed:** `src/thaw/tags.cpp`: the code check of the graffiti update no longer checks where its call at 0x005F9B5A goes (only that it is a call); the log now says where it goes.
**Why:** test 23:32 (user 23:35: "cj did the tag animation but there was no tag"): thaw.log "code check 0x005F9B50 ... DIFFERENT at byte 11": reTHAWed itself points that call somewhere else at start, so our check failed and the tag watch stayed off; GTA never heard of a tag. Our hook wraps the whole update, so that call's target doesn't matter to us.
**Tested how:** user 23:38: rebuilt, tagged 6 times in skate mode.
**Result:** the tag watch now runs (starts / stops seen in both logs), but no tag finished was counted and no tag appeared (see fix 2 above).

## [2026-10-09] Tags step T1: Grove Street tag in GTA when the skater finishes a tag

**Changed:** new `src/thaw/tags.h/.cpp`: after code checks, our function sits in the graffiti component's update slot (vtable 0x006EE3C8, calls the original 0x005F9B50 first) and notes "spraying" (component byte +0x1C); the call at 0x005F933A that runs the "Graffiti Tag" score script goes through us (counts, then calls the original 0x004135E0). Published in link `reserved[5]` (`kTag*` bits in `src/common/link.h`). Logs starts / finishes per second. New `src/gta/tags.h/.cpp`: GTA's calls at 0x533DCE and 0x565C5E (tag lookups that would crash on a tag that isn't one of GTA's 100) go through us: GTA's own tags unchanged, ours shown fully sprayed. In skate mode, each finished tag puts GTA's tag model 1490 (Grove tag) on the GTA wall 1.5 m in front of CJ's chest, else on the ground under him; its facing side worked out from the model's vertices (logged once). Max 50, oldest removed; normal objects, not saved. `main.cpp` (both), `CMakeLists.txt`: the new files. Research and plan: DESIGN 8.28 (a). reTHAWed's scripts were decompiled into a scratch folder outside the repo; nothing from them is in the repo.
**Why:** user 22:08: when the skater tags, CJ should spray the Grove Street tag (the collectible one) in GTA.
**Tested how:** user 23:35: built, tagged in skate mode.
**Result:** CJ did the spraying animation, no tag appeared: the THAW side switched itself off (see fix above).

## [2026-10-09] Freak-out on bails: checked, already works

**Changed:** no code. `docs/DESIGN.md` 8.28 (b): result written down.
**Why:** user 22:08: wanted reTHAWed's freak-out meter and animation in GTA; plan was to test first and fix only what's missing.
**Tested how:** user 23:16: skate mode in GTA, bailed with reTHAWed's freak-out option switched on.
**Result:** works: meter and animations all showed in GTA. The freak-out is off by default in reTHAWed's options; players need to switch it on (to go in the README).

## [2026-10-09] Vehicle spawner (F6)

**Changed:** new `src/gta/spawner.h/.cpp`: F6 (GTA in front, skate mode off) opens a list of all GTA vehicles (models 400-611 that exist; trains 449, 537, 538, 569, 570, 590 left out because they need rails), BMX (481) at the top, the rest A-Z by GTA's own names (model info game name -> TheText). D-pad / left stick up-down (hold to scroll), Cross or Enter spawns in front of CJ through GTA's vehicle-cheat code (CCheat::VehicleCheat), Circle or F6 closes. While open GTA doesn't get the controller; after closing, not until the buttons are let go (no sprint / punch from the same press). Drawn after GTA's HUD (GTA's font, CSprite2d boxes). Logs opens and spawns. `src/gta/padlink.cpp`: calls the list's pad filter each controller frame. `src/gta/main.cpp`: frame and drawing hooks. `CMakeLists.txt`: spawner.cpp. Plan: DESIGN 8.28 (d).
**Why:** user 22:08 / 22:11: a vehicle spawner with all vehicles, BMX at the top, F6, controller once open; first of the four new features (needed to test BMX).
**Tested how:** user 23:07: built, opened with F6, picked and spawned vehicles with the controller.
**Result:** works: "it worked flawlessly".

## [2026-10-09] Radio step R3: station and volumes remembered

**Changed:** `src/gta/radio.cpp`: reads `radio-settings.ini` (next to our plugin, `GTA San Andreas\modloader\my-passthrough\`, approved write path) at the first world update: `[Radio] Station` (rethawed / 1-12 / off), `GtaVolume` (0-100, default 50), `RethawedVolume` (0-100 or -1 = leave reTHAWed's own). Writes it (Windows' ini functions) when the wheel closes after a station or volume change. Logs what was loaded / saved. No other file written.
**Why:** user 21:56: step 1 of the list, remember the radio settings between sessions.
**Tested how:** user 22:02: changed station and both volumes, restarted both games.
**Result:** works: "Worked perfectly first try".

## [2026-10-09] Radio: User Track Player on the wheel; reTHAWed music volume on the wheel

**Changed:** `src/gta/radio.cpp`: the wheel has 14 slots: User Track Player (GTA station 12, GTA's own logo `radio_TPLAYER`) added after WCTR. D-pad Up / Down on the open wheel now change the volume of the station pointed at: a GTA station = GTA radio while skating (default 50%, as before); reTHAWed = reTHAWed's own music volume (0-100 in steps of 10; shows reTHAWed's current value until changed). Sent to reTHAWed through the link (reserved[3]). `src/thaw/music.cpp`: on reTHAWed's game thread, sets its music volume with its own SetMusicVolume (0x004B4F10; 3 more code checks, verified on the cloud copy) when GTA asks; reports the current value back (reserved[4]); puts reTHAWed's own volume back when GTA closes. Music only; sound effects untouched. `src/common/link.h`: reserved[3], [4] described.
**Why:** user 21:49: keep GTA radio at 50% with the option; make the reTHAWed station's volume adjustable; add the User Track Player for people's own music.
**Tested how:** user 21:55: wheel volumes for reTHAWed and GTA stations; User Track Player with no songs added.
**Result:** works ("everything else seemed good"). User Track Player with no songs plays GTA's commercials; to look at only if players report problems with their own songs.

## [2026-10-09] Sound fix: reTHAWed's sound effects keep playing behind GTA

**Changed:** `src/launcher/main.cpp`: reTHAWed is now started paused (CREATE_SUSPENDED); before it runs, the launcher adds DirectSound's "global focus" flag (DSBCAPS_GLOBALFOCUS, 0x8000) to the two flag values reTHAWed uses when it makes its sound-effect buffers (0x00443F48: 0x400B0 for 3D sounds, 0x00443F7B: 0x400E0 for the others; each instruction checked byte for byte first, then one byte 0x00 -> 0x80 written with WriteProcessMemory, in memory only), then lets it run. Logged in launcher.log. reTHAWed.exe has no address randomisation (image base 0x400000, DllCharacteristics 0).
**Why:** user 21:40: with GTA's radio at 0% the skate sounds still weren't heard. Found in reTHAWed.exe: its sound-effect buffers (IDirectSound CreateSoundBuffer at 0x00443FBD) are made without global focus, so Windows mutes them whenever another program (GTA) is in front; its music goes another way (0x006B3xxx streams) and kept playing. So the skate sounds were probably never heard behind GTA, radio or not.
**Side effect:** reTHAWed's sound effects also keep playing when reTHAWed is used alone and another window is in front.
**Tested how:** user 21:49: launched with the new launcher, skated with GTA in front.
**Result:** works: "It worked that time!" (skate sounds heard).

## [2026-10-09] Radio volume while skating (GTA stations drowned out the skating sounds)

**Changed:** `src/gta/radio.cpp`: GTA sets its music fader every frame from the screen fade (call at 0x50B815 to SetMusicFaderScalingFactor 0x506E40; checked first: E8 -> 0x506E40, also on the cloud copy). That call now goes through ours, which multiplies the value by a skate-radio level (default 50%) while our skate-mode GTA station plays; otherwise unchanged. Music only: sound effects and reTHAWed untouched. While the wheel is open, D-pad Up / Down change the level in 10% steps (kept from reTHAWed meanwhile); the wheel shows "GTA radio volume N%". Logged when changed. Not remembered between sessions yet (R3).
**Why:** user 21:28: "gta radio stations are a lot louder and you can't really hear the skate sound effects".
**Tested how:** user 21:35: volume set from 50% down to 0% and up to 40% (gta.log shows each change, redirect installed).
**Result:** failed: the volume stayed the same. Cause (gta_sa.exe): GTA calls the fader only during a screen fade (CCamera music fade 0x50B6D0 returns at once when no fade is running), so our scaling was never applied. The hardware does use the fader for music channels (0x4D8CE2: fader +0x418 x music volume +0x410).
**Fix (same day):** `ApplyFader()` sets the fader to the skate-radio level every world update while our GTA station plays, and back to 1 once afterwards (skate mode off, other station); during a fade the redirected call still multiplies. **Tested 21:40: works** ("The volume adjustment worked"). But at 0% the skate sounds still weren't heard: separate cause (sound fix above).

## [2026-10-09] Radio step R2: GTA V-style station wheel with logos

**Changed:** `src/gta/radio.cpp` (rewritten; the diagnostics are gone, fixes 1 and 2 kept): holding Back 0.4 s opens a wheel (13 slots around a dark see-through circle: reTHAWed, GTA stations 1-11, Off); the right stick points at a slot (dead zone half tilt), the pointed station is bigger and bright with its name in the middle, the current one has a gold frame; letting go of Back chooses it (name at the top for 2.5 s). While the wheel is open the right stick is zeroed for reTHAWed (no camera turn). Logos: GTA's own from `models\fronten1.txd` (loaded at run time into our own texture slot "mp_radio", `radio_playback` ... `radio_WCTR`), reTHAWed's from `<reTHAWed>\ReTHAWed_NotifIcon.ico` (Windows' image reader, WIC); nothing copied. A slot without a logo shows its name; Off shows "OFF". `src/gta/radio.h`, `src/gta/padlink.cpp`: the filter now gets the sticks too (called after the axes are filled). `CMakeLists.txt`: MP_RETHAWED_DIR for the GTA plugin. Plan: DESIGN 8.27 R2.
**Why:** user 20:45 / 21:19: the wheel with icons; 21:17-21:20: GTA's own logos found in its files.
**Tested how:** user 21:28: built and used.
**Result:** works: "The wheel looks great". GTA stations were much louder than reTHAWed's skating sounds (volume entry above).

## [2026-10-09] Radio fix 2: GTA treats our station like an interior radio

**Changed:** `src/gta/radio.cpp`: GTA's radio code (0x4EA590) asks, when CJ has no vehicle radio, whether an interior radio is playing (call at 0x4EA5C3 to 0x507280 -> AEAmbienceTrackManager 0x4D6D40) and stops the radio if not. That one call now goes to our function: "yes" while our skate-mode GTA station plays, GTA's own answer otherwise (call checked first: E8 -> 0x507280, verified on the cloud copy too). Diagnostics stay for this test (now also count the answers).
**Why:** diagnostics test (user 21:01): every start was followed within a second by exactly one stop from 0x4EA5D2 (GTA's radio code, right after that question); no interior-code stops, no cutscene (state 8), flags 0xB7CB48/49 = 0, ambience track 0.
**Tested how:** user 21:15: GTA stations while skating, switched through them and back to the reTHAWed station.
**Result:** works: "Radio stations worked great", reTHAWed playlist back fine. gta.log: radio stays on after each start (manager status 3 = playing), no stop calls. First start of a session cost one frame-rate dip (478 frames in that 10 s), as when GTA loads a station in a car.
**Still to do:** remove the diagnostics (next radio build); R2 wheel with icons.

## [2026-10-09] Radio diagnostics (log only): why GTA stations don't play on foot

**Changed:** `src/gta/radio.cpp` (temporary diagnostics, STATUS.md): every other call of GTA's two radio-stop functions (8 calls of 0x4E9820, 7 of 0x506F70; each checked to be GTA's original call first) goes through a counter that notes where it came from, then calls GTA's own function as before. gta.log: GTA's radio state (manager status +0x68, flags +0x00..+0x07, retunes, station byte +0xE9, IsRadioOn, IsAmbienceTrackActive, cutscene-track state at 0x8AE55C, bytes 0xB7CB48/49) just before and right after our StartRadio, and once a second while a GTA station is chosen, with the stop calls counted since the last line.
**Why:** user 20:57: fix the GTA stations first. Two failed attempts, so measure before changing anything (AGENTS.md).
**Tested how:** user 21:01: Playback FM ~10 s, K-Rose ~10 s.
**Result:** found it: each StartRadio turned the radio on (IsRadioOn 1), then one stop call from 0x4EA5D2 turned it off within a second. Everything else quiet. Fix 2 above.

## [2026-10-09] Radio fix 1: GTA stations stopped on foot

**Changed:** `src/gta/radio.cpp`: GTA's interior-radio code (0x4D6E60) stops the radio every frame while CJ is outside a club or house; its three StopRadio calls (0x4D71D2, 0x4D71FD, 0x4D7221, checked to be E8 -> 0x506F70 first) now go to our function, which skips the stop only while our skate-mode GTA station plays (otherwise GTA's own stop as before). The station is restarted at most once (each start loads it = a hitch).
**Why:** test 20:45: Back hold changed stations and the reTHAWed station played, but GTA stations didn't play and GTA stuttered. gta.log: "GTA's radio had stopped ... starting it again" every 3 s; pacer 480-550 frames per 10 s.
**Tested how:** user 20:55: rebuilt, chose GTA stations while skating.
**Result:** failed: still no music on GTA stations; no stutter now; the reTHAWed station plays. gta.log: redirect installed, 0 interior stops skipped, radio off again 3 s later (restarted once). Second failed attempt: STATUS.md written, next step is a log-only build.

## [2026-10-09] Radio while skating, step R1: station switch on Back

**Changed:** new `src/gta/radio.h/.cpp`: while skate mode is on, Back (Create/Share on PlayStation, View on Xbox) is kept from reTHAWed; a tap is passed on at release (4 frames), a hold of 0.4 s steps to the next station: reTHAWed soundtrack -> GTA stations 1-11 -> Off. The name shows at the top of the screen (GTA's font) for 2.5 s. GTA station = AudioEngine.StartRadio on foot (restarted if GTA stops it); skate mode off = GTA radio stopped and reTHAWed's music paused (user 20:35: its music only in skate mode while GTA runs; GTA silent for 5 s = back to normal). Logs all GTA station names once. `src/gta/padlink.cpp`: calls the Back filter before the pad goes to reTHAWed. `src/gta/main.cpp`: radio per world update, name drawing hook. New `src/thaw/music.h/.cpp`: on reTHAWed's game thread (inside our controller hook, every 30 ms) pauses / resumes only reTHAWed's music with its own PauseMusic (0x004B4DE0, 3 code checks, verified on the cloud copy); only resumes what it paused; GTA's wish older than 5 s = leave alone. `src/thaw/input.cpp`, `src/thaw/main.cpp`: hooks it in, logs once per second. `src/common/link.h`: reserved[1] (wish), reserved[2] (GTA tick), kMusic*. `CMakeLists.txt`: both new files. Plan: DESIGN 8.27.
**Why:** user 20:23: a GTA V-style radio with reTHAWed's playlist as its own station; must work on PlayStation and Xbox controllers (20:24); Back opens a menu in reTHAWed, so tap vs hold (20:28, OK 20:29). Wheel comes next (R2).
**Tested how:** user 20:45: built and played.
**Result:** partly. Back hold steps through the stations (names from GTA correct: 1 Playback FM ... 11 WCTR), the name shows, the reTHAWed station plays, reTHAWed's music pauses and resumes (thaw.log), no wheel yet (as planned). GTA stations did not play and GTA stuttered (fix 1 above).

## [2026-10-09] Body flips: copy reTHAWed's display rotation

**Changed:** `src/thaw/pose.h/.cpp`: new `DisplayCodeChecksPass` (16 code checks, all verified against the cloud copy of reTHAWed.exe) and `ApplyDisplayRotation`; `ReadPose` now also rebuilds reTHAWed's display-only rotation of the skater model (3 angle queues in the model component, the game's two clocks, its easing, L = Rx * Ry * Rz around the pivot at model +0x28), read only. `src/thaw/main.cpp`: before publishing, turns the body bones and the board by it; logs once per second when a flip is on (largest angles) and when the model's 'flipped' byte is set. GTA side unchanged. Plan and addresses: DESIGN 8.26.
**Why:** user 19:49/20:00: no backflip, front flip or roll on CJ; asked for every trick's animation from reTHAWed. The 8 recordings (20:00) show the skater's facing and bones completely steady in the air: reTHAWed does body flips by turning the drawn model (script command RotateDisplay, model component), not the skeleton.
**Tested how:** user 20:15: backflips, front flips and rolls with GTA in front; 6 GTA recordings.
**Result:** works: "it looked pretty good on my end". thaw.log: all 16 code checks passed; flips seen as x-axis rotations up to 353-374 degrees (back/front flips) and z-axis up to 353 (rolls); 'flipped' byte never set. The GTA recordings show CJ's hips going below the board during flips (body really turns). Body copy: 0-1 half-updated samples skipped per second (as before).

## [2026-10-09] Phase 6 H3 + H4: reTHAWed's HUD shown in GTA

**Changed:** new `src/common/hudlink.h`: second shared memory block (`Local\MyPassthrough_Hud_v1`, header + 2 picture slots up to 1920x1200). `src/thaw/hud.cpp`: after each caught HUD, shrinks the picture on the graphics card (3840x2160 -> 1920x1080), starts copying it to memory, and writes last frame's copy into the slot GTA isn't reading; logs time per frame every 10 s. New `src/gta/hudshow.h/.cpp`: right after GTA draws its own HUD (`drawHudEvent`), while skate mode is on and the picture is fresh (< 250 ms), copies a new picture into a RenderWare texture and draws it over the whole screen (pre-multiplied blend); logs every 10 s. `src/gta/main.cpp`: adds the hook. `CMakeLists.txt`: hudshow.cpp. Plan: DESIGN 8.25.
**Why:** user chose copying reTHAWed's own HUD (14:52); H2 works (19:33); user OK 19:35.
**Tested how:** user 19:49: built, skated in GTA with skate mode on, 5 HUD pictures taken.
**Result:** works: "the hud looks pretty great overall". thaw.log: ~600 pictures shared per 10 s, 1.0-1.3 ms per frame (max 3.4 ms), 0 failures. gta.log: shown in ~550-600 frames per 10 s, 1.1-1.2 ms per new picture, 0 half-written, 0 errors.
**Still broken:** user: "the numbers on the score and combo meter look slightly off" (the caught picture's numbers look clean at 1920x1080; cause not known yet: maybe softness from 1920x1080 stretched to GTA's screen). Body flips (backflip, front flip, rolls) still don't show on CJ; no GTA recording of one yet. gta.log: body copied ~60 frames/s, 0 samples skipped, so the flip is either not in the data or undone on the GTA side.

## [2026-10-09] H2 fix 1: HUD picture came out empty; force alpha writes

**Changed:** `src/thaw/hud.cpp`: while the HUD draws into our picture, also sets the colour write mask to all 4 channels (game's value put back afterwards). The test picture keeps the colour where alpha is 0, and the log line now says how many pixels had alpha / colour and the game's write mask.
**Why:** test 19:18 (GTA in front): HUD caught in every frame (44-57 draws, start/end marks in the right place in thaw_frame_1), 6 pictures saved, but all fully see-through (alpha 0 everywhere). The save also blanked colour where alpha was 0, so whether colour arrived is unknown. Most likely cause: the game draws its HUD with alpha writes off (it never needed alpha on its own screen).
**Tested how:** user 19:33: GTA in front, 7 pictures with touchpad + D-pad Up.
**Result:** works. Log: the game's write mask is 0x7 (colour only, no alpha), which was the cause. All 7 pictures have the HUD with proper see-through: score + special bar, trick name and combo line, multiplier x base points, manual balance meter, "Hidden Combo!", stance (FAKIE). Picture kept outside the repo (game content).

## [2026-10-09] Phase 6 H2: catch reTHAWed's HUD in a see-through picture

**Changed:** new `src/thaw/hud.h/.cpp`: when reTHAWed starts drawing its HUD (first `DrawIndexedPrimitiveUP` of a frame, see DESIGN 8.23), its drawing goes into our own full-size see-through picture instead of its screen, until its next render target change / EndScene / Present; then everything is put back (target, depth buffer, viewport, 4 alpha states). Only in a level with reTHAWed behind GTA. Touchpad + D-pad Left switches it off/on. The frame logged with touchpad + D-pad Up / F11 also saves the HUD picture as `logs/thaw_hud_N.tga` (half size). Device reset: our picture is released first. `src/thaw/drawlog.*`: calls it from the wrapped functions (+ Reset wrapped), marks "HUD picture start/end" in the frame log. `src/thaw/main.cpp`: passes in-level / not-in-front / toggle. `src/thaw/touchpad.cpp`, `src/common/link.h`: D-pad Left bit. `CMakeLists.txt`: hud.cpp. Plan: DESIGN 8.23.
**Why:** user chose copying reTHAWed's own HUD (14:52); H1 found where it is drawn; user OK 18:03.
**Tested how:** user 19:12: built, skated with GTA in front, special mode, 8 frame logs with touchpad + D-pad Up.
**Result:** catching works: with reTHAWed behind GTA the HUD was caught in every frame (about 590 of 590 per 10 s, 44-55 draws each), 0 failures, no crash. GTA's pacer numbers look the same with catching on and off. **No HUD picture saved yet:** reTHAWed was the front window (thaw.log 118-205 s and from 278 s) every time the button was pressed, and catching is off then by design. Special mode: the frame logs show the same targets and passes as normal frames (only more 3D), so its screen effect is not an extra pass; probably the same glow passes with other shader values, which the log doesn't record yet (DESIGN 8.24).
**Still to do:** take the HUD picture with GTA in front. Nothing goes to GTA yet (H3, H4).

## [2026-10-09] Shortcuts use the PS5 touchpad instead of Select

**Changed:** new `src/thaw/touchpad.h/.cpp`: the reTHAWed plugin reads the PS5 controller (DualSense, USB or Bluetooth) directly, read only, on its own thread, for the touchpad click and the D-pad. It puts them in the link (`reserved[0]`, `kPadRaw*` bits in `src/common/link.h`). Touchpad + D-pad Up = F11 draw log (`src/thaw/main.cpp`); touchpad + D-pad Down = F9 recording (`src/gta/skatemode.cpp`). Select is no longer used. `CMakeLists.txt`: builds touchpad.cpp, links hid + setupapi. Temporary: remove before release.
**Why:** user 17:51: Select + D-pad was awkward; touchpad + D-pad is easier. The touchpad isn't in GTA's pad (DualSenseX shows GTA an Xbox pad), so it has to be read from the controller itself.
**Tested how:** user 18:01, PS5 controller over Bluetooth with DualSenseX running. (Earlier Select test: reTHAWed's draw log worked, GTA's recording never started; cause not found, the new route doesn't use GTA's pad.)
**Result:** works. thaw.log: "reading the PS5 controller", first report id 0x31 (Bluetooth), 78 bytes; 8 presses logged. GTA: 5 recordings (gta_frames_1-5.csv); reTHAWed: 4 frame logs (thaw_frame_1-4.txt). Fixed afterwards: drawlog.cpp still printed "Select + D-pad Up" (text only; not rebuilt yet).
**H1 finding (same test):** in all 4 frames the end looks the same: small 960x540 / 240x135 glow passes, then the full-size 3840x2160 picture gets one full-screen quad (glow added), then about 120 small 2D draws (DrawIndexedPrimitiveUP, screen-space FVF 0x144, mostly 2-triangle quads: text and icons; some larger shapes: meters) = the HUD; then one copy to another full-size target and EndScene. So the HUD is the stretch between the glow quad and that last target change.

## [2026-10-09] Temporary controller shortcuts for the two recorders

**Changed:** `src/gta/recorder.*`, `src/gta/skatemode.cpp`: Select + D-pad Down (controller) = F9 frame recording. `src/thaw/drawlog.*`, `src/thaw/main.cpp`: Select + D-pad Up = F11 draw log. Both read the controller from the link (GTA's pad as published for reTHAWed). Temporary: remove before release.
**Why:** user 17:32 plays on a TV with a small keyboard; hard to hit F9 / F11 at the right moment.
**Tested how:** not tested. Note: the buttons still reach reTHAWed too (Select may do something there).
**Result:** none yet.

## [2026-10-09] Phase 6 H1: log how reTHAWed draws one frame (F11)

**Changed:** new `src/thaw/drawlog.h/.cpp`: after a code check, wraps 17 functions of reTHAWed's Direct3D 9 device (found at 0x00852044); F11 logs every call of the next frame to `logs/thaw_frame_N.txt`. `src/thaw/main.cpp` calls it each pass. `CMakeLists.txt`: builds drawlog.cpp. Plan: DESIGN 8.22 (user chose copying reTHAWed's HUD picture).
**Why:** to find where reTHAWed's HUD drawing starts, before copying it.
**Tested how:** not tested (not compiled here).
**Result:** none yet.
**Next:** user: build, skate, F11 a few times (riding normally; during a manual; during a grind), plus F9 in GTA during a body flip.

## [2026-10-09] Phase 5-3k: even frame clock for showing reTHAWed

**Changed:** `src/gta/skatemode.cpp`: new `FrameClock`: the moment at which reTHAWed is shown (blended, 40 ms back) and the orientation smoothing's time now advance by the average frame time each frame (measured, about 16.7 ms), only nudged 5 % towards the real time each frame (restarts after a hitch over 3 frames). Reset when skate mode goes off.
**Why:** recording 14:35: GTA's updates still alternate 13.5 / 19.5 ms apart even with the pacer; sampling reTHAWed at those moments gives uneven steps on screen.
**Tested how:** in the cloud with updates alternating 13.5 / 19.8 ms: the clock moves 16.55 to 16.78 ms per frame; after a 0.5 s hitch it restarts at the real time. Not in the game.
**Result (user 14:43):** skating smooth now.
**Next:** phase 6 plan (DESIGN 8.22): HUD and body flips.

## [2026-10-09] Research: reTHAWed's frame rate and pool limits (no code)

**Changed:** `docs/DESIGN.md` 8.21 only.
**Why:** user away (vet), asked for research on the animation / fps issue and the map.
**Found (read only, rthw.dll):** reTHAWed's game step is fixed at 1/60 s (the `UnlockFPSValue` debug option turns it into 1/120); the animation fps option only thins the skeleton updates. reTHAWed reads pool sizes from `rethawed.ini` `[Memory]` by pool name before its own table: `HashItem` (30000 by default; T4's limit) and `Out set RailPoint` (20000; very likely the rail hang) can be raised there by the user.
**Tested how:** not tested (reading only).
**Next:** user: pacer test (8.20); later T4 with `[Memory] HashItem=100000`.

## [2026-10-09] Phase 5-3j: even 60 fps pacer for GTA

**Changed:** new `src/gta/pacer.h/.cpp`: once per drawn frame (main.cpp's drawing hook) waits until exactly 1/60 s after the previous frame (QueryPerformanceCounter; Sleep(1) with timeBeginPeriod(1), last 1.5 ms spinning). Only while GTA's own Frame Limiter (Display Setup) is OFF (read from the menu settings byte plugin-sdk calls `m_bPrefsVsync`, which is the frame limiter in GTA SA 1.0); with it on the pacer stands aside. Logs when it starts / stands aside and every 10 s how many frames came late. `CMakeLists.txt`: builds pacer.cpp.
**Why:** user 11:39: both games at 30 = smooth; wants 60. The recordings showed GTA's frames 14 / 19.5 ms apart in turn at 60 (its limiter, here FramerateVigilante's 60): an uneven picture even when our positions are right.
**Tested how:** user 14:35 (GTA Frame Limiter off; reTHAWed animations default), one recording (ramp jump).
**Result:** "almost there": still a little stutter at the peak of jumps. Log: pacer on, 600 frames per 10 s, late frames 0 to 284 per 10 s. Recording: GTA's game updates STILL come 13.5 / 19.5 ms apart in turn; CJ's step per update follows that exactly (positions are right for the moment of each update), so the picture judders when frames reach the screen evenly. Most visible when he moves slowly (peak of a jump).
**Next:** 5-3k (entry above).

## [2026-10-09] Phase 5-3i: aim at CJ's real neck; neck matches the skater's neck-to-head

**Changed:** `src/gta/body.cpp`: GTA bone ids corrected: neck = 4, head = 5 (from CJ's skeleton log; plugin-sdk's names are one off). The hips and chest are now aimed at CJ's neck (they were aimed at his head while matching the skater's neck), and the neck is aimed so neck-to-head matches the skater's neck-to-head.
**Why:** user 11:14: CJ leans back slightly and looks up. Pairing CJ's head with the skater's neck tips the upper body back.
**Tested how:** body maths in the cloud on a made-up skeleton with the real neck/head layout (neck-to-head within 0.01 degrees, hips-to-neck 1.5 degrees). Not in the game.
**Also suggested to the user:** try reTHAWed's own frame-rate setting at 60 instead of unlimited (reTHAWed's option, nothing of ours); the logs show reTHAWed already makes 60 new frames a second while skating, so the gain may be small; GTA's own frames alternate 14 / 19.5 ms apart.
**Result (user 11:39):** CJ looks "a lot more natural on the board". reTHAWed only offers a frame-rate setting for its animations (no 60 choice); with GTA at 30 and reTHAWed's animations at 30 the stutter is gone. User wants 60 without stutter.
**Next:** user test.

## [2026-10-09] Phase 5-3h: smooth the skater's orientation; whole calm pose under the copy

**Changed:** `src/gta/skatemode.cpp`: the skater's orientation (after blending) is smoothed with a 40 ms time constant (`SmoothOrient`; a 45 degree step spreads over about 5 frames; reset when skate mode goes off). `src/gta/body.cpp`: the calm pose now covers every bone except the root: each frame every bone starts from the pose of the 20th frame of skate mode, then gets turned to match the skater; GTA's animation underneath no longer moves anything (it used to roll the arms).
**Why:** recordings 11:00 (entry below).
**Tested how:** smoothing and body maths in the cloud; user 11:14.
**Result:** smoother; shoulder shimmy gone. Still slight stutter on ramps. CJ looks stiff: "looking up and leaning back slightly".
**Note:** F10 OFF shows GTA's own (roller-skating) animation on purpose; it's only for comparing.
**Next:** 5-3i (entry above).

## [2026-10-09] Phase 5-3g: calm collar bones; log reTHAWed's frame rate

**Changed:** `src/gta/body.cpp`: collar bones (21, 31) join the calm pose (neck, head, face). `src/thaw/main.cpp`: the per-second link line adds "new reTHAWed frames in the last second".
**Why:** user's F10 test (10:51): shimmy only with the copy on. Nothing copies the collar bones from the skater, so GTA's skating animation kept rolling the shoulders under the copied arms. The frame count: blending often fell back to "newest frame as is", which happens when reTHAWed's frames come more than 40 ms apart; need to know how fast reTHAWed really runs behind GTA.
**Tested how:** user 11:00, three recordings (bowl run, standing still with F10).
**Result:** shimmy still there; stutter a bit better, still there. reTHAWed runs at 60 (60 new frames a second while skating). Bowl recording: blending in every frame, position smooth, BUT the skater's orientation (up direction) changes in steps every few frames (0.58 -> 0.64 -> 0.85 ...): on curved ramps it jumps from one flat face to the next; every step jerked CJ's tilt, board and copied body (arm direction up to 90 degrees in one frame). Standing still: the copied targets move smoothly (slow idle sway only), so the shimmy there comes from GTA's side: its skating animation still rolled the arms around their own length (only their direction is copied). GTA's frames alternate 14 / 19.5 ms apart (its own frame limiter).
**Next:** 5-3h (entry above).

## [2026-10-09] Phase 5-3f: blend between reTHAWed frames; neck too; stop GTA's look-at and talking; F10 compare

**Changed:**
- `src/thaw/main.cpp`: the loop wakes about every 1 ms (timeBeginPeriod(1)) and only checks the skater's position; a change = a new reTHAWed frame: its exact time is noted (QueryPerformanceCounter), the full read follows 4 ms later (lets reTHAWed finish the frame); without a change a full read still runs every 10 ms. `src/thaw/skater.h/.cpp`: `ReadPosQuick`.
- `src/common/link.h`: version 7 (`_v7`, 460 bytes): the frame's time.
- `src/gta/skatemode.cpp`: keeps the last 8 reTHAWed frames and shows the skater 40 ms ago, blended between the two frames around that moment (position, speed, facing squared up again, board, body); every frame stops GTA's look-at (secondary IK task) and talking face (secondary facial task) on CJ; F10 switches the body copy on / off; per-second log: frames blended, tasks stopped.
- `src/gta/body.cpp`: calm pose now neck + head + jaw + brows (bones 4, 5, 6, 7, 8), taken after 20 drawn frames (once the look-at / talking have stopped); `BodyToggle`. `src/gta/recorder.*`: column `blend_pct` instead of `ahead_ms`.
**Why:** user 10:40 + recordings (entry below).
**Tested how:** blending tested in the cloud; user 10:51 (after one more build fix: `HistClear` used before declared; and the first copy to the PC again landed stale).
**Result:** F10 works: copy on = skating pose, copy off = roller skating; so the copy does reach the screen. With the copy on the shoulders still shimmy. Stutter not checked yet. Log: blending used in 22 to 28 of 60 frames in most seconds, all 60 in some; GTA's look-at / talking tasks: none found to stop.
**Next:** 5-3g (entry above).

## [2026-10-09] Phase 5-3d fix 3: no look-ahead (the stutter); calm head and face

**Changed:** `src/gta/skatemode.cpp`: look-ahead off (`kMaxExtrapolateMs` 50 -> 0). `src/gta/body.cpp`: the neck is no longer aimed at the skater's head; neck, head and the two face bones keep the pose of the first frame of skate mode (relative to the chest) every frame, so GTA's head turning and talking mouth don't show.
**Why:** frame recorder (entry below): the look-ahead made CJ (and with him the hips, body and board) jump back and forth up to 0.6 m per frame. The shoulder "shimmy" is most likely the same shaking (the skater's own shoulders don't shimmy, per the user). User 10:31: CJ looks up and moves his mouth: the neck aim tipped his face up; GTA's own look-at and talking ran underneath.
**Tested how:** body maths retested in the cloud; user 10:40 with two new recordings.
**Result:** improved, but still noticeable stutter; CJ still looks up, moves his mouth, shimmies; roller-skating look when standing still. Recordings: look-ahead off as planned; a third of GTA's frames now got NO new reTHAWed frame (56 and 63 of 179) and the next got two: the remaining stutter. CJ's skeleton log shows bone 4 is the neck (collar bones and head hang from it), 5 the head, 6/7/8 hang from the head: fix 3 locked the head but not the neck, so GTA's look-at still turned the neck.
**Still open:** a few single-frame jumps remain in the skater's own data (a reTHAWed frame seen twice, then one missed); smoothing those would need the blend buffer (STATUS idea A). The calm head is taken from the first frame; if CJ was mid-glance then, the head keeps that glance until skate mode is switched off and on.
**Next:** user test.

## [2026-10-09] Phase 5-3e: frame recorder (F9 records 3 seconds)

**Changed:** new `src/gta/recorder.h/.cpp`; `src/gta/skatemode.cpp`, `board.cpp`, `body.cpp` fill in one row per frame while recording; `CMakeLists.txt` builds recorder.cpp. Plan: DESIGN 8.18.
**Why:** STATUS.md: measure the shimmy / stutter before a third fix.
**Tested how:** user 10:31: two recordings (pushing; ramp jump).
**Result:** works, and found the stutter: CJ moved 0.07 m, then 0.68 m, then 0.07 m ... per frame while the skater moved an even 0.39 m. The step size followed the sample age, which only ever reads 0 or 16 ms (GetTickCount64 steps). GTA got one new reTHAWed frame per frame almost every time; the drawing happens 2 ms after the update in the same frame. Chest direction from the skater changes smoothly. User also checked: reTHAWed's skater does NOT shimmy its shoulders.
**Next:** fix 3 (entry above).

## [2026-10-09] Phase 5-3d fix 2 (second attempt at shimmy / stutter)

**Changed:** `src/thaw/main.cpp`: a sample keeps the time we FIRST saw that reTHAWed frame (same position and speed as last time = same frame; refreshed after 200 ms so standing still isn't "old"); log counts the repeats per second. `src/thaw/pose.h/.cpp`: 20 body points: adds the chest and a point 10 units along the chest's right axis. `src/common/link.h`: version 6 (`_v6`, 452 bytes). `src/gta/body.cpp`: CJ's chest twist follows the skater's chest axis instead of its shoulder joints.
**Why (two causes found by reasoning, not yet measured):** (1) stutter: reTHAWed runs about 60 frames a second, we read about 65 times, so the same frame is read twice; the second read was stamped "new" and GTA's "move ahead by speed x age" then put CJ up to one frame's travel behind (at 8 m/s about 13 cm) and back: a jitter along the travel direction, worst when fast (take-offs). The board had the same stutter earlier. (2) shimmy: the chest twist copied the line between the skater's shoulder joints, which move with every arm swing in the push animation; reTHAWed's chest itself doesn't twist that much.
**Tested how:** bone maths in the cloud. Not in the game. Second real attempt at this problem: if it still happens, STATUS.md next (AGENTS.md).
**Result (user 02:50): did NOT help either.** Log: about 34 repeat reads a second while skating; GTA sees samples 0, 16 or 31 ms old. Two real attempts failed: stopped, wrote STATUS.md.
**Next:** see STATUS.md (measure first).

## [2026-10-09] Phase 5-3d fix 1: steady shoulders, smoother take-offs

**Changed:** `src/gta/skatemode.cpp`: while the body is copied, GTA's animation underneath stays on the standing pose (no switching to pushing / fast / jump). `src/gta/body.cpp`: the chest's twist uses the collar bones (fixed on the chest) instead of the shoulder joints; samples where the skater's bones changed length (read while reTHAWed was half-way through updating them) are skipped, keeping the last good pose (lengths learned while riding; reset when skate mode goes off; 30 odd ones in a row = accept). Log line adds the number skipped.
**Why:** user test 02:39: shoulder shimmy (GTA's pushing / sprint animations shrug the shoulders, which also moved our twist reference); stutter at take-off (the base animation switched to the jump pose there: 172 switches in one session).
**Tested how:** bone maths retested in the cloud; user test 02:44.
**Result:** did NOT help: shimmy and stutter still there. Log: base animation now steady (1 change), 0 half-updated samples skipped, body copied every frame. So neither the base animation nor torn reads were the cause.
**Next:** fix 2 (entry above).

## [2026-10-09] Phase 5-3d: CJ's body copies the reTHAWed skater's body

**Changed:** `src/thaw/pose.h/.cpp`: also reads 18 body bone positions; `src/thaw/main.cpp` sends them and logs whether all were found. `src/common/link.h`: version 5 (`_v5`, 428 bytes), `thawBody`, flag `kBodyValid`. New `src/gta/body.h/.cpp`: turns CJ's bones right before GTA draws him (hips, spine, neck, arms, legs). `src/gta/skatemode.cpp`: hands the targets over each frame, clears them when skate mode goes off, logs once CJ's skeleton layout and every second how many frames were copied. `src/gta/main.cpp`: installs the hook. `CMakeLists.txt`: builds body.cpp. Plan: DESIGN 8.17.
**Why:** user wanted skateboarding animations and tricks instead of roller-skating moves.
**Tested how:** in the cloud on a made-up skeleton; user test 02:39 (after one build fix: `BodyChecksums` used before it was declared, and the first copy to the PC didn't land). gta.log: CJ's skeleton 32 bones, world space; body copied in 57 to 60 frames of 60.
**Result:** "seemed to work pretty good". Problems: shoulders "shimmy"; body stutters when jumping off a ramp.
**Next:** fix those (entry above).

## [2026-10-09] Phase 5-3c: CJ turns and tilts with the skater

**Changed:** `src/gta/skatemode.cpp`: new `TiltCj`: CJ's matrix takes the skater's up and facing each frame, turned around his feet (switch `kTiltCj`); set upright again when skate mode goes off; the drift check now compares with where CJ was really put; per-second log adds "CJ turned N (lean up to X deg)". Plan: DESIGN 8.16.
**Why:** user saw odd movement in bowl airs: the board turned with the skater, CJ stayed upright.
**Tested how:** user test 02:20; gta.log: CJ turned in every frame, lean up to 90 degrees, no errors.
**Result:** works: board stays with CJ as he turns, no turning around at rail ends.
**Still broken:** poses still roller-skating.
**Next:** CJ's body copies the skater's (5-3d).

## [2026-10-09] Phase R4-2 + 5-3b: GTA's board copies reTHAWed's board; CJ faces where the skater faces

**Changed:** `src/common/link.h`: version 4 (name `_v4`, 212 bytes), adds the skater's orientation and the board bone, flag `kPoseValid`. `src/thaw/main.cpp`: reads them every pass and sends them. `src/gta/skatemode.cpp`: CJ's heading from the skater's facing (velocity as fallback); places the board from the bone (`PlaceBoardFromThaw`); the per-second report counts frames that used them. `src/gta/board.h/.cpp`: `BoardUpdatePose`. Also fixed `docs/DESIGN.md`: the 8.14 edit had dropped the "## 11. Before release" heading and so the 8.15 plan was never written; both are in now. Plan: DESIGN 8.15.
**Why:** user asked for tricks; the board stuttered; CJ turned around at rail ends.
**Tested how:** user test 02:08 (both games, East LS); gta.log: board followed reTHAWed's bone in every frame of every second (56 to 60 of 60), facing from reTHAWed used in all of them, no errors.
**Result:** works: tricks show on the board, board stays with CJ in bowl airs. User saw "weird movements in the air": most likely the board turning with the skater (on vert the skater rotates up to 90 degrees and spins in airs) while CJ stays upright in the roller-skating jump pose. Not yet confirmed (no F9 mark).
**Still broken:** CJ doesn't turn or tilt with the skater (ramps, vert airs); poses still roller-skating.
**Next:** CJ's whole body follows the skater's orientation, then the board stance (5-3a).

## [2026-10-09] Phase R4-1: read the skater's facing and skeleton from reTHAWed (log only)

**Changed:** new `src/thaw/pose.h/.cpp`; `src/thaw/main.cpp` logs, 10 times per second while there is a skater: the skater's orientation matrix (object `+0x88`), the board bone (`Bone_Board_Root`) and the pelvis in the skater's own space; the bone list once per skater; every bone's full matrix on F9. 9 code checks first. `CMakeLists.txt`: builds pose.cpp. Plan and the addresses: DESIGN 8.15.
**Why:** tricks on CJ's board and the board stutter both need reTHAWed's own board angle; also fixes CJ turning around at rail ends later (skater's facing).
**Tested how:** in the cloud: the 9 code checks compared against the copy of reTHAWed.exe (all match); the checksum code gives the game's numbers ("skeleton" 0x222756D5); compiles with stand-in headers. Not run in the game.
**Result (user test 01:52): works.** All code checks match; 52 bones, all named; facing follows the velocity; the board bone flips over in kickflips and turns 180 in shove-its. Details in DESIGN 8.15.
**Still broken:** nothing in this step.
**Next:** R4-2 + 5-3b.

## [2026-10-09] Phase 5-1: stand-in skateboard under CJ

**Changed:** new `src/gta/board.h/.cpp`: builds a simple board (deck with kicked nose and tail, trucks, wheels; 236 triangles, plain colours) and draws it each frame right after GTA draws the world. `skatemode.cpp`: lifts CJ by the deck height (11.2 cm), places the board under his feet each frame (tilted with the slope on the ground, level in the air), hides it when skate mode goes off. `main.cpp`: installs the drawing hook. `CMakeLists.txt`: builds board.cpp. Plan: DESIGN 8.13.
**Why:** user asked for a board on CJ; chose "stand-in board first, reTHAWed's board later" (01:25).
**Tested how:** in the cloud only: the board code compiles with stand-in headers (not the real plugin-sdk, no Windows compiler here) and its shape was drawn to a picture (board_preview.png, not in the repo) on a test slope: right size, tilts nose-up. Not tested in the game. Phase 5-0 (poses) also still not tested.
**Result (user, 01:42):** works. Board shows under CJ and stays with him, pushing looks faster, jumps fine; the board stutters while following; tricks don't show on the board (expected). Log: board shown, "skate" file loaded (block 122), 112 pose changes in one session, no errors.
**Still broken:** stutter (probably the slope tilt measured from GTA's ground; to be replaced by reTHAWed's own board angle, DESIGN 8.15); board ignores GTA's lighting (same brightness day and night); wheels don't spin; CJ's roller-skating poses don't match a board stance.
**Next:** user builds and tests poses and board together.

## [2026-10-09] Phase 5-0: CJ gets skating poses (GTA's roller-skating animations)

**Changed:** new `src/gta/skateanim.h/.cpp`; `src/gta/skatemode.cpp` calls it every frame while skate mode moves CJ (speed along the ground, height above GTA's ground) and stops it when skate mode goes off (also after an error); `CMakeLists.txt` builds it. Plan: DESIGN 8.13.
**Why:** user asked for skate animations and a board on CJ before re-running T4. GTA has no skateboard model (cut content), so this step only does poses: `skate_idle` / `skate_run` / `skate_sprint` from GTA's `skate` animation file by speed, `JUMP_glide` in the air.
**Tested how:** user, 01:42, together with phase 5-1.
**Result:** works: poses switch standing / pushing / fast / in the air as planned.
**Still broken / not tested:** everything; no board; CJ still turns around at rail ends (8.11).
**Next:** user builds and tests; then T4 re-run.

## [2026-10-09] Phase 4-4 R2: T3 works, T4 out of hash items; collision merged into 200 m tiles

**Changed:** `tools/gta_to_thaw_map.py`: after vert, wallride and rails are worked out (they need GTA's separate objects), the collision objects are merged into squares of 200 m (one collision object per tile, split if a tile passes 65,535 corners or faces). `--no-merge` keeps the old way. East LS: 2,493 objects -> 35 tiles.
**Why:** T3 (all of LS, `--box 0 -3000 3000 0`): fine (user, 01:09). T4 (whole map, `--box -3000 -3000 3000 3000`): reTHAWed stopped with "CRITICAL: Out of HashItems (30000 max) in their Compact Pool". `debug.txt`: everything loaded first, including the 61 MB collision (28,220 objects, 2,249,641 triangles, super sectors 236 x 234: "Loading collision... Loaded!"); the pool ran out afterwards. reTHAWed keeps one hash entry per collision object / scene sector (and per node) in a pool of 30,000; 28,220 objects + the game's own entries + 9,001 nodes is too many. T2 (7,800 objects) and T3 fit. The earlier hang with 21,963 nodes may have been the same pool, without the message.
**Tested how:** in the cloud: East LS merged into 35 tiles: same 164,562 triangles, same flags (65,343 wallride, 250 vert); every BSP tree checked: all 164,562 faces reachable, none in the wrong place. Not in the game.
**Result:** none yet.
**Still broken / not tested:** in-game; the whole map should now be about 700 tiles + 9,001 nodes.
**Next:** user re-runs T4.

## [2026-10-09] Phase 4-4 R2 prep: --box and --no-geometry-nodes

**Changed:** `tools/gta_to_thaw_map.py`: `--box XMIN YMIN XMAX YMAX` converts any GTA area (the centre stays East LS's (1975, -1525), so the GTA alignment in `convert.h` stays right); `--no-geometry-nodes` leaves out the one-LevelGeometry-node-per-object list.
**Why:** R2 size ladder (DESIGN 8.12): T1 East LS without geometry nodes (does collision still work?), T2 2 x 2 km (482k triangles), T3 all of LS (728k), T4 whole map (2.25M).
**Tested how:** in the cloud: East LS without geometry nodes: 9,001 nodes (start + 9,000 rail nodes), PAK 6.8 MB. Not in the game.
**Result (00:50): T2 works.** User built T2 (`--box 1000 -2500 3000 -500 --no-geometry-nodes`): **7,800 collision objects, 541,288 triangles** (more than LSE's 495k), 84,946 BSP nodes, super sectors 89 x 97, 9,001 nodes; it loaded and skated well, and in skate mode the user took CJ all the way to the airport (`thaw.log`: skater from GTA x 1976 to 1015, y down to -2466). GTA ran at 58 to 60 frames a second. This also answers T1: **collision works without the LevelGeometry nodes**.
**Found on the way:** with the same 9,000 rail node budget over 4 x the area, the longest rails take it all (655 rails kept), so short but good rails (e.g. the bowl coping) can be dropped. Rails need a per-area budget, or zones.
**Next:** T3 (all of LS, ~728k triangles) and T4 (whole map, 2.25M).

## [2026-10-09] Phase 4-4 R1: whole-map statistics tool (map size research)

**Changed:** `docs/DESIGN.md` new section 8.12 (plan: R1 how much is there, R2 where reTHAWed stops, R3 THAW's zones, then decide). New read-only `tools/gta_world_stats.py`: reads all of San Andreas from the GTA install (same reader as `gta_area_dump.py`; interiors and LOD stand-ins skipped) and writes `extracted\gta_world\world_stats.csv` (per 500 m square: placements, collision objects, triangles, vertical / flat faces, height range) and `summary.txt` (totals, busiest squares, East LS for comparison). No 3D file.
**Why:** user chose the map size research as the next step (2026-10-09 00:32).
**Tested how:** in the cloud only with a made-up install (3 placements: counts, squares and the East LS comparison come out right). Not run on the real install yet (`gta3.img` is on the PC only).
**Result (user ran it, 00:35):** all of San Andreas: 30,842 placements, **28,213 collision objects, 2,249,161 triangles** (831k vertical, 821k flat), in 143 squares of 500 m. East LS test map: 2,486 objects, 164k triangles = **7% of the map**. Busiest squares: East LS 59k triangles (x 2000, y -1500), San Fierro 55k, central LS 52k; median square 9.8k; none over 60k. Picture `world_collision_map.png` (sent, not in the repo). Estimates by box: 2 x 2 km around East LS 482k triangles / 6,955 objects (about LSE's 495k); all of Los Santos (3 x 3 km) 728k / 10,382; south half of the map 1.1M / 14,418.
**What it means:** the whole map is about 14 x our test map in collision and objects. Two known pressure points: (1) one LevelGeometry node per object would be 28,213 nodes, above where reTHAWed hung (21,963 nodes); LSE has these nodes for only a tenth of its objects, so we test without them; (2) at our current density the whole map would have about 120,000 rail nodes vs. the 9,000 that load: rails will need streaming (zones) or a much stricter choice.
**Next:** R2 size ladder (below).

## [2026-10-09] Skatepark wall: fence moved to the middle; rails on both edges and on the fence top - WORKS

**Changed:** `tools/gta_extras.py`: the fence now stands in the middle of the low wall (GTA has it near the outer edge), so the wall top is a ledge on both sides; rails along both top edges and along the fence top: 21 rails for the 7 runs (was 7). Faces still all point outwards (480 of 480).
**Why:** test (user, 00:14): could grind the inner ledge and wallride parts of the fence, but not grind the fence top, and not grind or wallride from outside the park. `thaw.log`: inside grinds ran exactly on the inner rail (0.33 m from the wall centre, at wall-top height, up to 23 m/s); outside, there was no ledge at all (the fence stood flush on the outer edge) and the fence top had no rail. One outside wallride did happen (west wall, north part, at 124 s: sideways along the fence 0.38 m from the centre, 14.4 to 15.6 m high). The skater also hung for half a second in the north-west corner where two runs meet (141 s), then got free.
**Tested how:** in the cloud: rails per run, face directions. Not in the game.
**Result (00:26): works.** User: grinding the fence top and the outer ledges works, and wallrides. Some fence-top grinds turned CJ the opposite way.
**Still broken / not tested:** CJ turning around: `thaw.log` (00:22 run) has 9 moments out of 870 moving samples where the skater's reported speed points against his movement, all at the ends of the hand-made rails (e.g. the fence-top rail ends at GTA x 1862.9 and 1876.5 on the north side, y -1391.3 at the west entrance): the skater's speed flips for a moment there (he pops off or bounces at the rail end). CJ's facing is taken from the skater's speed, so CJ turns around; when the skater then moves slowly CJ keeps the wrong facing. Fix later on the GTA side: take CJ's facing from the skater's own facing (not his speed), which skate animations will need anyway. Corners where two runs meet may still snag.

**First wall test result (00:14):** inner ledge grinds and some fence wallrides work; see Why above.

## [2026-10-09] Hand-made extras: the skatepark's low wall and fence

**Changed:**
- New `tools/gta_extras.py`: a hand-made list of things GTA draws but has no collision for, and builders for their collision boxes and grind rails. First entry: the East LS skatepark's surrounding low wall (0.7 m wide, about 0.6 to 0.7 m high) with a fence on its outer edge (1.7 m high on the south-west, about 3 to 3.7 m elsewhere), in 7 runs with gaps at the three entrances, following the street's slope on the north and east sides. Each run gets a grind rail along its inner top edge.
- `tools/gta_to_thaw_map.py`: adds those walls as extra collision objects (`extra_...`); wallride comes from the normal vertical-wall rule; their rails are added first and count against the 9,000 rail node budget (the automatic finder skips the extras). `--no-extras` leaves them out. Prints an `extras:` line.
**Why:** user (2026-10-09 00:01): grinds and wallrides work except on the skatepark's surrounding fence; GTA's collision has nothing there. User chose option 1 (hand-placed extras; reading GTA's visual models stays a later option for the whole map).
**Where the numbers come from:** read off the Los Santos East map (its author built this wall and fence from GTA's visual model), in GTA metres, rounded to about 10 cm; only the coordinates are in our file, nothing from LSE's files.
**Tested how:** in the cloud: 7 runs, 480 faces, all pointing outwards once written; top view (`skatepark_wall.png`, sent, not in the repo): wall outline with the three entrance gaps, rails on the inside. Map: 2,493 objects, 9,000 rail nodes, 11,494 nodes. Not in the game.
**Result:** none yet.
**Still broken / not tested:** in-game. Where the park inside is lower than the street (north-east), there could be a gap under the wall; joints between segments may snag a wallride.
**Next:** user re-runs the tool, replaces the map folder, grinds and wallrides the wall and fence.

## [2026-10-08] Rails: at most 9,000 rail nodes (map with 19,476 hung while loading) - WORKS

**Changed:** `tools/gta_to_thaw_map.py`: after finding the rails, the tool keeps the longest ones up to 9,000 rail nodes in all (`--rail-budget N` to change; it prints how many it found and kept).
**Why:** test (user, 23:52): "the map crashed" / "reTHAWed was not responding when trying to load in". The map installed this time was the new one (9.2 MB): with the box fix, box tops became ledges too, so the tool found 8,015 rails / **19,476 rail nodes** (21,963 nodes in all, level script 4.2 MB). `debug.txt` stops right after "zone_init: mpgtalse ... Ped avoid hack disabled, avoidance ON" with no error message, i.e. the game hung while setting up the level's nodes (the working maps got past this point to "Done! / mission_init"). Claude checked the node list in that exact file: no duplicate names, no bad or self links, no broken positions. The only thing out of line is the size: LSE has 11,574 rail nodes (12,524 nodes, 2.9 MB) and loads fine. So we stay below LSE's size for now.
**Tested how:** in the cloud: 2,774 of 3,500 rails kept, 8,999 rail nodes, 85.7 of 87 km (the dropped ones are the short bits); level script 11,486 nodes. Not in the game.
**Result (2026-10-09 00:01): loads, and grinds and wallrides work** (user: "the grinds and wall rides seemed to work well except on the surrounding fence of the skate park"). So 9,000 rail nodes load fine; the box fix works too (no report of getting stuck).
**Still broken / not tested:** the skatepark's surrounding low wall and fence: **GTA's collision has nothing there** (checked along x 1862, y -1451 and y -1351.5: only the ground), so in our map there is nothing to grind or ride. LSE has a 0.7 m wall plus a 1.7 to 3 m fence there (wallride flagged), built by its author from GTA's visual model. The real node limit (if it is a count limit at all) is unknown. If 9,000 still hangs, the next test is `--rail-budget 2000`, then `--no-rails`, to tell a size problem from something about the rails themselves.
**Next:** user re-runs the tool, replaces the map folder, tries the map.

## [2026-10-08] Fix: GTA boxes and spheres faced inwards (skater stuck inside a tree)

**Changed:** `tools/gta_area_dump.py`: the boxes and spheres we build from GTA's collision (GTA stores many objects, e.g. trees, as boxes / spheres, not meshes) now have their corners in the same order as GTA's own mesh faces. Before, after `gta_to_thaw_map.py` reversed every face for THAW (attempt 6), all box and sphere faces pointed **inwards** (checked: box 12 of 12, sphere 8 of 8 inwards; now 12 / 8 outwards).
**Why:** test (user, 19:50): got stuck, F9 at GTA (1966.2, -1359.8). `thaw.log`: the skater was shut in a pocket about 1.3 x 1.5 m, at x 1966.2..1967.4, y -1360.8..-1359.3, for over 30 s. That is exactly the collision box of a tree (`sm_veg_tree4_vbig`, x 1965.8..1967.8, y -1361.1..-1358.9, 34 m tall): with its faces pointing inwards, the skater could get into the box but not out. Probably also the "stuck in a corner" from the first map test (12:52).
**Tested how:** in the cloud: face directions of a test box and sphere. Not in the game.
**Also found:** the user's grind / kerb test used the **previous** map: the tool had run (19:43, 7.4 MB PAK with rails in `extracted`), but the map folder in reTHAWed was still the 19:14 one (5.3 MB, no rails, no wallride). So rails and wallride are still untested.
**Result:** none yet.
**Next:** user re-runs the tool (rail and wallride counts will change a little: box tops can now be ledges), replaces the whole map folder, tests grinds, wallrides and the tree.

## [2026-10-08] Grind rails from GTA's shapes (East LS map)

**Changed:** `tools/gta_to_thaw_map.py`: a rail finder and RailNode chains in the level script.
- Rails go along the outer edge of a flat top (normal-up >= 0.9) that drops away by 8 cm to 1.6 m: (a) where the top meets a steep side in the same object (ledges, benches, the bowl's coping: deck meets the 65-degree lip), (b) where a flat top's edge is shared by no other face of its object and the ground 30 cm beyond it is lower (kerbs: GTA's roads have **no kerb faces**, the pavement is a separate plane 17 cm above the road). Ground heights come from a 2 m grid of all flat-ish faces. Roof edges and high wall tops (more than 1.6 m drop) are skipped; edges steeper than 27 degrees are skipped; bends sharper than 35 degrees start a new rail; chains under 1 m are dropped; points on straight runs are merged.
- Each chain becomes RailNode nodes laid out as in LSE: `Pos`, `Angles`, `Name` (`MPGTALSE_Rail<n>_<i>`), `Class RailNode`, `Type Concrete`, `CollisionMode Geometry`, `TerrainType TERRAIN_GRINDCONC`, `CreatedAtStart`, `Links` = next node. One-entry `Links` arrays are written inline (header, count 1, value), as LSE does. The lookup lists include the rail nodes. `--no-rails` leaves them out.
- Result for our area: 3,500 rails, 10,464 rail nodes, about 87 km (LSE: 11,574 rail nodes). Level script: 12,951 nodes.
**Why:** user (19:36): grinds and wallrides for the East LS map, useful later for the whole map.
**Tested how:** in the cloud: our rail nodes read back with the same item layout as LSE's (all 10,464 links valid, inline form). Top-view pictures of our rails over LSE's hand-made rails (`rails_compare.png`, `rails_compare_zoom.png`, sent to the user, not in the repo): our bowl coping lies exactly on LSE's; our kerbs match LSE's kerb rails along the streets. LSE has extras we don't: overhead wires / power lines, small details inside plazas, half pipe deck edges (drop over 1.6 m). Not in the game.
**Result:** none yet.
**Still broken / not tested:** in-game. All rails are "concrete" (sound only); GTA's surface types could pick metal / wood later. No wires, railings or handrails (thin poles) yet.
**Next:** user tests grinds (kerbs, bowl coping) and wallrides.

## [2026-10-08] Wallride on every vertical wall (East LS map)

**Changed:** `tools/gta_to_thaw_map.py`: every collision face that is a vertical wall (|normal-up| <= 0.01, the limit SceneConverter uses) and not vert gets THAW's wallride flag 0x0004. 65,023 of the 164,082 faces (GTA's collision is mostly building walls and boxes; LSE: 31k of 495k). The tool prints the count.
**Why:** user (19:36): add grinds and wallrides to the East LS map before the ramp finder / map size research. Wallride first (small); grind rails planned in DESIGN 8.10 (format learned from LSE's rail nodes).
**Tested how:** in the cloud: counts. Not in the game.
**Result:** none yet.
**Still broken / not tested:** in-game. Short vertical faces (kerb sides, box edges) are marked too; the game decides whether a wall is tall enough to ride.
**Next:** user tests wallrides; Claude writes the rail finder.

## [2026-10-08] Phase 4-3 step 1: vert on the skatepark's two half pipes - WORKS

**Changed:** `tools/gta_to_thaw_map.py`: two more `VERT_AREAS` entries for GTA model `laeskatetube1`: the half pipe on the ground (GTA x 1872..1883, y -1368..-1358, ramps 13.8 to 18.1 m) and the raised half pipe (x 1950..1959, y -1378..-1367, ramps 18.8 to 23.1 m). Same rule as the bowl: vert on faces with normal-up 0.02 to 0.7 (the 0.60, 0.34 and near-vertical 0.03 bands: 12 faces per half pipe), the gentler bottom bands (0.93, 0.80) plain, vertical walls wallride. The thin sloped side braces (normal-up 0.42, starting at the floor) are left out by the boxes / heights. Totals: 250 vert faces, 168 wallride faces.
**Why:** user OK'd step 1 of the vert plan (DESIGN 8.10).
**Tested how:** in the cloud: listed every flagged face of the half pipe model by steepness and height (each ramp: 4 faces per band, both sides). Compared with LSE first: LSE's version of this model flags only 8 faces (part of the ground half pipe; nothing on the raised one), so for the half pipes LSE is not a reference; ours goes by the rule learned from the bowl.
**Result (19:33): works.** The user: "both half pipes seemed to skate well" (the raised one is reachable).
**Still broken / not tested:** nothing known in the skatepark. Rest of the map has no vert yet.
**Next:** phase 4-3 step 2, the candidate finder (DESIGN 8.10).

## [2026-10-08] Phase 4-2d step 2, fix 2: vert on the channel's upper walls (+ wallride in the skatepark) - WORKS

**Changed:** `tools/gta_to_thaw_map.py`: the skatepark area now reaches 15.5 m and takes faces from normal-up 0.03 (was 13.1 m and 0.2), so the nearly vertical walls above the 13.0 m lip get vert too (226 vert faces, was 220). The area's truly vertical walls (|normal-up| <= 0.01) get the wallride flag 0x0004 (18 faces), as in LSE.
**Why:** user (18:51): the channel feels better in LSE; what looked wrong in ours was the skater not going up and down the channel properly. Claude compared the user's LSE run (logged through the launcher) with ours at the same walls. **LSE:** at the lip the skater's sideways speed turns into upward speed (e.g. 13.5 up + 9.7 sideways -> 22.6 m/s straight up) and he flies to 23 to 26 m, 10+ m above the deck, then drops back in. **Ours:** at the lip his upward speed (14.3 m/s) is gone in one step (-1.7 m/s) at 13.5 to 13.7 m, as if he hit something. He did: the channel's walls go on nearly vertical from 13.0 to 15.4 m (normal-up 0.05 / 0.09), in GTA's collision and in LSE's alike. LSE marks those faces vert; ours didn't (outside the height and steepness range), so the skater ran into a plain wall right above the lip. Our skatepark object now matches LSE's flag pattern: vert on the 54-degree walls, the 65-degree lip and the near-vertical upper walls; wallride on the vertical walls; the gentle bottom curve and floors plain.
**Tested how:** in the cloud: flag table of our skatepark object compared line by line with LSE's skatepark object (same heights and steepness bands; LSE splits the same surfaces into more triangles). Not in the game.
**Result (19:09): works.** The user: "able to skate it pretty spot on compared to the east los santos map". The skatepark bowl now skates like LSE's.
**Still broken / not tested:** wallride in the skatepark not specifically tried. Rest of the map has no vert / wallride yet.
**Next:** phase 4-3 step 1 (the half pipes, `laeskatetube1`), see DESIGN 8.10.

## [2026-10-08] Phase 4-2d step 2, fix: vert on the whole bowl, not 58% of it

**Changed:** `tools/gta_to_thaw_map.py`: the bowl's area box now covers the whole GTA skatepark model (x 1840..1980, y -1490..-1345). Vert faces: 220 (was 128).
**Why:** bowl test (user, 18:30, both games running): CJ still launched out of the bowl; acid drops worked only sometimes, not like in Los Santos East. Claude checked: (1) the installed map has the 128 vert faces; (2) LSE's bowl has the **same shape** as GTA's (walls at normal-up 0.6, lip 0.4, deck at 13.0 m) with vert on the same kinds of faces, so flat GTA walls are not the problem; (3) `thaw.log` shows vert air **does** work on our faces: twice the skater went straight up and down with 0 sideways speed (at 133.1 s and 146.7 s); (4) but the bowl reaches x 1965 and y -1375, and the first box stopped at x 1945 / y -1390 and only took faces lying completely inside it: 92 of the 220 steep bowl faces had no vert, e.g. right next to the logged launch at GTA (1938, -1436).
**Tested how:** in the cloud: 220 faces flagged; the faces at the logged launch spot now have vert.
**Result:** none yet in the game.
**Still broken / not tested:** not tested in the game. Part of the outside launches in the log were off gentle slopes north of the bowl (normal-up 0.93 to 0.99, not bowl walls); those stay ordinary ground.
**Next:** user re-runs the tool, replaces the map folder, skates the bowl again.

**Bowl test 2 result (18:46):** better: acid drops into the bowl work; the user found no spot in this bowl meant for spine transfers (fine). One section felt off ("not going up on the board and coming back down properly"); F9 there: GTA (1911.8, -1430.3, 9.8), the bottom of the narrow channel between the bowl's south wall (y -1433) and north wall (y -1420), 13 m apart, walls 3.6 m high. Claude compared that section with LSE: **same shape and same vert faces** (floor 9.36 m, 31-degree curve not vert, 54-degree wall and 65-degree lip vert, deck at 13.0 m); only differences: LSE's faces have terrain 16 (ours 0) and LSE's piece has a LevelGeometry node with TrickObject + Cluster. The log there shows vert air working (sideways speed drops to near 0 at the top and the skater comes back in), but the airs are small (0.4 m above the deck at the speeds used). Not yet known whether the same section feels the same in LSE; asked the user to compare.

**Bowl test 1 result (18:30):** vert worked in places (2 straight-up airs in the log), but 92 bowl faces were left without vert (above).

## [2026-10-08] Plan: vert everywhere (phase 4-3, draft)

**Changed:** `docs/DESIGN.md` new section 8.10 (plan only, no code).
**Why:** user's goal: half pipes, ramps and empty pools across San Andreas should skate like reTHAWed. While planning, found that GTA's skatepark has a second model, `laeskatetube1` (half pipes) with real curved transitions (faces from nearly flat to nearly vertical, about 14 to 23 m high).
**Tested how:** read-only look at the skatepark models in `collision_gta.obj`.
**Result:** plan written; starts after the bowl vert test.
**Next:** user tests the bowl (4-2d step 2).

## [2026-10-08] Phase 4-2d step 2: vert for the skatepark bowl

**Changed:** `tools/gta_to_thaw_map.py`: collision faces can now carry THAW's "vert" flag (0x0008). A small table, `VERT_AREAS`, says where: for now one area, the East LS skatepark bowl (GTA model `laeskateparkla`, box x 1860..1945, y -1460..-1390, height 9.0..13.1 m, faces with normal-up between 0.2 and 0.7, i.e. steeper than about 45 degrees). 128 faces get the flag: the bowl walls (54 degrees, 10.8 to 12.6 m) and the lip (66 degrees, 12.6 to 13.0 m). The gentle bottom curve stays ordinary ground. The tool prints how many vert faces it marked.
**Why:** GTA skate test (user, 14:20): with skate mode on, CJ followed the skater well on our map and bounced off walls at the right places, but in the bowl the skater flew out forwards instead of going up and coming back in; the user wants to spine transfer and skate the bowl like in reTHAWed. In THPS games that needs "vert" faces. Checked in LSE first: 1,544 faces carry 0x0008 (alone), all sloped (normal-up 0.0 to 0.9), mostly in and around the skatepark; LSE's flag 0x0004 (wallride) is on vertical walls (as SceneConverter's source also names it). GTA's collision has no such information, so we mark it by area.
**Tested how:** in the cloud: 128 faces flagged in the output file, all within GTA x 1867.5..1940.7, y -1441.7..-1390.2, height 10.8..13.0 m (the bowl). Not in the game.
**Result:** none yet.
**Still broken / not tested:** whether THAW's vert works well on GTA's flat 54-degree bowl walls (THPS bowls are curved and end in a vertical part); the skater may launch at an angle. Whether the bowl edges where two walls meet allow a spine transfer. Other ramps in the map are not marked yet. Wallride (0x0004) not added yet.
**Next:** user re-runs the tool, replaces the map folder, skates the bowl (reTHAWed alone is enough).

**GTA skate test on our map (14:20):** works: CJ follows the skater well and bounces off GTA's walls where the skater hits ours. Problem: no vert in the bowl (above).

## [2026-10-08] Phase 4-2d step 1, attempt 6: the ground faced downwards (triangle corner order) - WORKS

**Changed:** `tools/gta_to_thaw_map.py`: every collision triangle's corners are written in the reverse order (a, c, b).
**Why:** logged run (12:38, launcher, `thaw.log`): on our map the skater sat at THAW (43, 502, -2964) the whole time = GTA (1976.1, -1449.7, 12.75), exactly our start point; he did not fall and did not move when pushing forward (position changed by at most 0.4 units in 22 s), while his speed kept flipping between two values. The user saw him in a T-pose, still able to do tricks. Our collision has ground right under him (object 911, the big ground piece, triangles at height 491 to 495, i.e. 7 to 11 inches below). Then Claude compared the facing of flat triangles: in LSE, (B-A) x (C-A) points **up** for 94,219 flat triangles and down for 34,646 (undersides); in ours it was the other way round (14,420 up, 40,549 down). So our ground faced downwards: to the game the skater was under a ceiling, not on a floor.
**Tested how:** in the cloud: after the change our flat triangles are 40,549 up / 14,420 down, the same split as LSE's. BSP trees are unaffected (they don't depend on corner order).
**Result:** none yet in the game.
**Still broken / not tested:** not loaded. The spawn is only about 7 to 11 inches above the ground; if he starts inside the ground, raise it.
**Next:** user re-runs the tool, replaces the map folder, tries the map.

**Attempt 6 result (12:52): it works.** The user skated our map twice and could skate the bowl in the skatepark. The first time he got stuck in a corner (not logged: the second run overwrote `thaw.log`). Logged second run: he started at the start point and skated about 4,000 units (100 m) west along the street at height ~494 (GTA z 12.5 m), then down into the bowl at height ~368 to 373 (GTA ~9.4 m, i.e. about 3 m deep) and around it, at speeds up to ~730 units/s. **First map made by us from GTA's collision that skates.**

**Attempt 5 logged-run result:** see Why above.

## [2026-10-08] Phase 4-2d step 1, attempt 5: a real (but empty) sky

**Changed:** `tools/gta_to_thaw_map.py`: `MPGTALSE_sky.pak.wpc` is no longer empty. Like LSE's sky PAK it now has a texture file (no textures), a scene with two empty sectors, and a small sky script (`.qb`): a node list with the two sky pieces, the two lookup lists, and a settings struct; all entries carry PAK flag 1, names `skies\mpgtalse_sky\mpgtalse_sky.*`. Two checksums from LSE's sky script whose names we don't know are copied as numbers.
**Why:** attempt 4 got much further: the whole level loaded ("Loading collision... Loaded!", "zone_init: mpgtalse", setup script done). Then, at "starting standard gameflow", the game loaded our sky ("NEW SKY PATH: ...mpgtalse_sky.pak.wpc") and crashed right after (`FATAL ERROR AT 0x00439ba0`, read of address 0). Our sky PAK had nothing in it.
**Tested how:** in the cloud: our sky PAK lists like LSE's (tex -> scene -> script, same types, flags 1 / 0x21, same parent links); the sky scene's layout numbers match LSE's sky scene exactly (2 sectors; same offsets). Not loaded.
**Result:** none yet in the game.
**Still broken / not tested:** a sky with no meshes may still upset the game (LSE's two sky pieces have meshes). Also not ruled out: the crash could be the skater being placed at our start point, which happens around the same moment.
**Next:** user re-runs the tool, replaces the map folder, tries the map.

**Attempt 5 result (11:31): the map loads!** No crash; the run starts ("GameFlow_StartRun", music plays). But the user reports the skater "just floating, no solid ground". `debug.txt` shows "Loading collision... Loaded!" and no collision warnings. Not yet known whether the skater was falling or hanging still, or where he was: next is a logged run through our launcher (`thaw.log` records the skater's position and speed every 100 ms).

**Attempt 4 result:** the level script fix worked; everything in our main PAK loaded, including the collision. Crash moved to the sky (above).

## [2026-10-08] Phase 4-2d step 1, attempt 4: fix a byte-order mistake in the level script

**Changed:** `tools/gta_to_thaw_map.py`: the first word of every array in the level script is now written as bytes `00 01 <kind> 00`, as in LSE. The node list's was written as `00 0A 01 00` (two bytes swapped) since attempt 1; the new lookup lists from attempt 3 had the same mistake. The in-node `restart_types` list was already right.
**Why:** attempt 3 crashed exactly like attempt 2 (same address 0x00624fd0, while loading the level script, the new 368,828-byte file). Instead of adding more guesses, Claude compared our level script with LSE's byte by byte: the start point and map-piece entries match LSE's exactly, but the node list's header did not. A wrong array type means the game can't read the node list at all.
**Tested how:** in the cloud: the node list header and both lookup list headers now read `00040c00 <name> <pak> <ptr> 00000000 00010a00 / 00010d00 / 00010100 <count> <ptr>`, the same pattern as LSE's three arrays.
**Result:** none yet in the game.
**Still broken / not tested:** not loaded. The empty scripts from attempt 3 stay in (harmless, and LSE has them).
**Next:** user re-runs the tool, replaces the map folder, tries the map.

**Attempt 3 result:** crashed, same place and address as attempt 2 (`debug.txt`, 11:19).

## [2026-10-08] Phase 4-2d step 1, attempt 3: the level script gets LSE's empty scripts and lookup lists

**Changed:** `tools/gta_to_thaw_map.py`: the level script (`.nqb`) now has, besides the node list: empty scripts named `MPGTALSE_Startup`, `MPGTALSE_Setup`, `MPGTALSE_Goals`, `LoadTerrain`, `LoadCameras`, `LoadAllParticleTextures`, `load_level_anims`, `LoadObjectAnims`, plus 8 more empty scripts with the same (unknown) names as the 8 at the start of LSE's; and two lookup lists, `MPGTALSE_NodeArray_SortedNames` (every node's name, in node order) and `MPGTALSE_NodeArray_SortedIndices` (0, 1, 2, ...).
**Why:** attempt 2 crashed on loading the level script (see `STATUS.md`). The user OK'd going on with STATUS.md's suggested step 1 ("let's pick up the map").
**Tested how:** in the cloud. Checked against LSE first: an empty script in LSE is item kind 0x07 + 0xBABEFACE, size 2, packed size 2, bytes `01 24` (16 of LSE's 77 scripts are exactly this); the two other arrays in LSE are `salseast_NodeArray_SortedNames` / `_SortedIndices` (names confirmed by checksum; reMAP's source names them too), 12,524 entries each = the node count; SortedNames is the node names in node order (checked for all 12,524; not sorted, despite the name), SortedIndices is 0..12,523. Our output read back: 19 top-level items, each laid out like LSE's; the node list starts at the same offset as in LSE.
**Result:** none yet in the game.
**Still broken / not tested:** not loaded. LSE's `LoadTerrain` is a real script (terrain sounds); ours is empty, so there may be no skate sounds. If this crashes in the same place, try STATUS.md step 2 (a script with only the start point).
**Next:** user re-runs the tool, replaces the map folder, tries the map.

## [2026-10-08] Phase 4-2d step 1, attempt 2: an (invisible) scene for the map (crashed: stopped, STATUS.md)

**Changed:** `tools/gta_to_thaw_map.py`: the level PAK now also has an empty texture file (`.tex`, 0 textures) and a scene (`.geom`) with one empty sector per collision object (no materials, no meshes, so nothing is drawn). The PAK entries now come in LSE's order with LSE's "parent" links: textures -> scene (parent: textures) -> level script and collision (parent: scene).
**Why:** attempt 1 crashed. reTHAWed's `debug.txt` (copied to the cloud, read only): the map was found, the sky loaded, then "Load level pak... Loading asset 0x4543d4e7 (Type: 5)" (our collision) and "FATAL ERROR AT 0x0041bfbb, EXCEPTION_ACCESS_VIOLATION (Read of 0x00000000)". The collision was the first asset and had no parent scene, so the game read through an empty pointer. In LSE the collision's parent is the scene, and the scene's parent is the texture file.
**Tested how:** in the cloud from `collision_gta.obj`. Formats checked against LSE first: texture file header (magic 0xABADD00D, 1, 4, u16 count; LSE's count 3,096 matches its 3,096 textures), material list header (LSE: 6,340 materials, size field = bytes up to 0xBABEFACE), scene header byte for byte against LSE's layout, 48-byte sectors, 104-byte geoms; LSE has 126 sectors with 0 meshes and 23 collision objects with no sector, so empty sectors are allowed. Our output read back: 2,486 sectors with the same names, in the same order, as the 2,486 collision objects; PAK 5.3 MB.
**Result:** **crashed again, one file later.** User tried it (01:41). `debug.txt`: the texture file (Type 4) and the scene (Type 3) loaded; the crash came on the level script (Type 13, `.nqb`): `FATAL ERROR AT 0x00624fd0`, read of address 0. So the scene fix was right and the level script is the new problem. LSE's level script has 80 top-level items (77 compiled scripts such as `salseast_Startup`, `LoadTerrain`, `LoadCameras`; 3 arrays); ours has only the node array. Second failed attempt for this step: **stopped, see `STATUS.md`.**
**Still broken / not tested (at the time of writing):** not loaded in reTHAWed. The empty material list (0 materials) is the one field layout guessed from a single example (LSE). If this also fails: stop and write STATUS.md (second failed attempt for this step).
**Next:** user re-runs the tool, replaces the map folder in `UserMods\Maps`, tries the map.

**Attempt 1 result (entry below):** crash on loading (see Why above).

## [2026-10-08] Phase 4-2d step 1: our first reTHAWed map from GTA's collision (written)

**Changed:**
- New `tools/gta_to_thaw_map.py` (Python 3, standard library): GTA's collision for an area (read from the install via `gta_area_dump.py`, or from its `collision_gta.obj` with `--from-obj`) -> a reTHAWed user map in `extracted\thaw_maps\my-passthrough GTA East LS\` (gitignored): `level.ini`, `MPGTALSE.pak.wpc` (THAW collision version 10 with our own BSP trees + compiled level script with the node list: 1 Restart "Player1" at the skatepark corner + 1 LevelGeometry node per collision object, as in LSE), empty `_sky` and `_sfx` PAKs, `map_info.txt`. No visible scene, no textures, no rails yet. THAW units = inches (39.3701 per metre) around the area centre (1975, -1525).
- `tools/gta_area_dump.py`: the area collection and the per-placement transform moved into functions (`collect`, `world_triangles`) so the map tool can reuse them; output unchanged.
- `src/common/convert.h`: alignment for the new map (`kGtaEastLs`: level ID 0x9F014B98 = checksum of "load_MPGTALSE", scale 0.0254, offset (1975, -1525, 0)); `convert_test.cpp`: 4 new checks (33, all pass).
**Why:** step 4-2d: find out whether reTHAWed loads a map made by us, and whether a map without a visible scene works.
**Tested how:** in the cloud from the user's `collision_gta.obj`: 2,486 objects, 118,647 corners, 164,082 triangles, 25,880 BSP nodes, 2,487 nodes; 4.9 MB PAK; built in 2 s. Read back with `thaw_level_dump.py` (PAK, collision and node list parse); BSP checked on 300 random objects: 0 misplaced faces, 0 faces missing. Format details confirmed from LSE first: level ID = checksum of "load_" + prefix (`load_SALSEAST` = 0x0274F7DE, LSE's ID); PAK entry = type, offset, size, checksum of the path without `.wpc`, 0, checksum of the stored file name, parent, flags (0x20 = named); BSP: left child = below the split point, faces crossing go to both; vertex light 255, optimization byte 0, terrain 0, object flags 0 in LSE.
**Result:** none yet in the game.
**Still broken / not tested:** not loaded in reTHAWed. In LSE the collision and script are "children" of the scene file in the PAK; ours have no parent (no scene). Without a scene the level may not load, or load with no collision. Not run on the user's PC yet.
**Next:** user runs the tool, copies the map folder into reTHAWed's `UserMods\Maps`, rebuilds, and tries the map.

## [2026-10-08] Phase 4-2c: read SceneConverter / reMAP / NodeQBC

**Changed:** `docs/DESIGN.md` section 8.9: 4-2c findings and the 4-2d plan. No code. (Read-only access to `D:\Mods\thps-tools`, where the user downloaded the three tools, granted for this.)
**Why:** step 4-2c: learn how a working converter writes reTHAWed maps.
**Tested how:** read their READMEs, licences and the THAW-related source (collision writer, BSP nodes, PAK assembly, `level.ini`) in the cloud; counted LSE's node kinds with our own reader.
**Result:** no licence on SceneConverter, reMAP or NodeQBC (only the bundled math3d is MIT): **nothing copied**, facts only. THAW collision is now fully understood, including the header's "super sector" rows/columns, the per-face "optimization" byte and the BSP tree layout. A map = level PAK (scene, textures, collision, level script) + sky PAK + `level.ini`. LSE's 12,524 nodes: 11,574 rail, 715 LevelGeometry, 111 LevelObject, 46 Waypoint, 18 Restart, ... Decision: write our own collision / script / PAK writer.
**Still broken / not tested:** unknown whether reTHAWed loads a map without a visible scene (first test in 4-2d).
**Next:** 4-2d step 1: the writer, tested in the cloud by reading its output back.

## [2026-10-08] Phase 4-2b tested: GTA's collision matches Los Santos East

**Changed:** nothing (test only; docs updated).
**Why:** first run of `tools/gta_area_dump.py` on the real install.
**Tested how:** user ran it on the PC (00:55): 54 IDE / 52 IPL files listed in `gta.dat`, 14,258 model names, 9,268 text placements, 16,316 files in `gta3.img`, 36,569 stream placements, 6,050 LOD stand-ins skipped; in the area: 2,580 placements of 499 models, **collision found for all 499** (216 `.col` archives), 164,082 triangles. Claude copied the output to the cloud and drew the walls (steep triangles) of both from above, in GTA coordinates.
**Result:** **they line up.** Buildings, the skatepark, the hospital, the club, the rotated buildings at the bottom middle and the road edges sit on top of each other: the rotation convention (conjugated quaternion) is right. Measured: of 9,837 GTA wall pieces in the shared part, 9,719 have a Los Santos East wall nearby, at a median distance of 0.12 m (90% within 0.23 m, 95% within 0.38 m). Differences: LSE has many more small things as solid (lamp posts, trees, bins, kerb edges, street furniture: 495k triangles vs GTA's 164k for a larger area); GTA's collision is the simplified version the game uses for physics. Surface materials seen (GTA ids): 0 (98k triangles), 53 (33k), 61, 43, 4, 55, ... (to be named from `surfinfo.dat` later; e.g. for "grindable" metal).
**Still broken / not tested:** nothing converted yet. Whether GTA's simpler collision skates well (kerb heights, steps, rails) is unknown until we build a level from it. GTA has no rails: they must be made from the shapes.
**Next:** 4-2c (read SceneConverter / NodeQBC: needs the user to download them) or straight to 4-2d (our own writer, using what 4-2a taught about THAW's format).

## [2026-10-08] Phase 4-2b: tool for GTA's own collision of an area

**Changed:** new `tools/gta_area_dump.py` (Python 3, standard library only, read only). From the GTA install it reads `data\gta.dat`, the IDE files (model numbers -> names), the text IPL files and, from `models\gta3.img`, the binary "stream" IPL files (placements) and the `.col` archives (collision), and writes for one area (default: Los Santos East's playable part, x 1700..2250, y -1900..-1150, +150 m margin) into `extracted\gta_<area>\` (gitignored): `collision_gta.obj` (boxes, meshes, spheres as 8-sided shapes, in GTA world metres), `placements.csv`, `summary.txt` (counts, surface materials, models without collision). Skips interiors and far-away (LOD) stand-ins: stream placements point at their LOD placement in the matching text IPL.
**Why:** step 4-2b: GTA's collision next to LSE's, to see how close a straight conversion from GTA's data would be.
**Tested how:** in the cloud only: the IMG reader on a copy of `models\cutscene.img` (634 files listed correctly), the COL1 reader on `models\coll\vehicles.col` / `weapons.col` (right counts), and the whole tool on a made-up install (real LAe/LAe2 map files + a fake `gta3.img` with one binary IPL and one COL3 model: the box and triangle come out where expected, rotated 90 degrees). `gta3.img` itself (940 MB) is too big to copy to the cloud, so the tool must run on the user's PC.
**Result:** none yet on real data.
**Still broken / not tested:** not run on the real `gta3.img`. COL2/COL3 reading only tested on made-up data. Rotation sign of IPL quaternions believed conjugated (`--quat-as-is` to try the other). `models\coll\peds.col` doesn't follow the COL1 layout the tool expects (skipped; not needed for the world).
**Next:** user checks Python is installed and runs the tool; Claude overlays the result with LSE's collision.

## [2026-10-08] Phase 4-2a: tool to look inside a reTHAWed map (collision + rails)

**Changed:** new `tools/thaw_level_dump.py` (Python 3, standard library only, read only). Given a reTHAWed map folder (or its `.pak.wpc`), it lists the files in the PAK and writes, into `extracted/<prefix>/` (gitignored): `pak_index.txt`, `collision.obj` (the collision as a 3D file), `collision_flags.txt`, `rails.obj` (lines between linked RailNodes), `rails.csv`. With `--gta lse` it also writes `collision_gta.obj` / `rails_gta.obj` in GTA SA world coordinates using the 4-0 formula. The formats it reads are written up at the top of the file.
**Why:** step 4-2a: learn THAW's level format from a working example before writing our own.
**Tested how:** run in the cloud on a copy of Los Santos East (2.7 s), and a top-down picture of the result in GTA coordinates drawn with the 3 landmarks from 4-0 on it (not in the repo).
**Result:** works on LSE. PAK: 9 files (`.stex`, `.mdl`, 2 unknown types, `.tex` 58.7 MB, `.geom` 28.2 MB, `.nqb` 2.9 MB = the node list, `.col` 16.7 MB, `.qb` 5.7 KB). The end marker's type is "last" without the dot. Collision version 10: 7,532 objects, 686,760 corners, 495,075 triangles; flags on triangles (THPS names, believed): wall ridable 38,750, non collidable 35,396, no skater shadow wall 4,628, vert 1,544, skatable 597, trigger 555, ... (most triangles have no flags). Nodes: 12,524, of which **11,574 rail nodes and 9,272 rail segments** (kerbs, roof edges, railings; long straight ones across streets look like power lines). In the picture, the streets, the skatepark's bowl, the hospital and the club line up with the 4-0 landmarks; the collision reaches far beyond the playable area (x -492..2998 m, y -2795..-64 m in GTA terms), likely background.
**Still broken / not tested:** not run on the user's PC yet (needs Python). Face flag names are THPS's, not confirmed for THAW. The node reader only knows the item types seen in LSE (vector, checksum, array, integer). Rails built from nodes only; THAW may also grind some collision edges by flag (not checked).
**Next:** 4-2b: the same for GTA's own collision of that area (from `gta3.img`), to lay the two on top of each other.

## [2026-10-08] Phase 4-2: whole-map research, converter plan

**Changed:** `docs/DESIGN.md` new section 8.9. No code.
**Why:** step 4-2 (user wants the whole map skateable).
**Tested how:** web research (thpsgoat.com "Using reMAP"; github.com/denetii/io_thps_scene; gta.fandom.com "Collision File"); io_thps_scene's source read in the cloud scratch area.
**Result:** Los Santos East was a THUGPro custom level converted with reMAP (Node.js, ZedekThePD), which wraps SceneConverter (THUG2 scene/collision -> THAW) and NodeQBC (scripts and node lists). The SA -> THUGPro step was the creator's own (most likely Blender + io_thps_scene, which has an autorail tool). GTA SA collision ("COL3": spheres, boxes, triangle meshes with int16/128 vertices and surface materials) and its placements are in the user's own `gta3.img` and `data\maps`. Plan: an offline converter first (option A), live collision (B) later; research steps 4-2a to 4-2e.
**Still broken / not tested:** gitgud.io (where SceneConverter/NodeQBC live) can't be reached from the cloud. io_thps_scene has no licence file: used for understanding only. Not known: how big a reTHAWed map can be, how long switching maps takes, whether reTHAWed accepts a map with almost no visible geometry.
**Next:** 4-2a: a read-only Python tool that turns a reTHAWed map's collision (LSE) into a 3D file in `extracted/`.

## [2026-10-08] Phase 4-1 tested: skating on GTA's real streets (Los Santos East); fix for a rare turn-off

**Changed:** `src/gta/skatemode.cpp`: when the skater sample "kept changing while read" (8 tries all landed while the THAW side was writing), skate mode now skips that frame (CJ stays put for one frame; logged on the 1st and every 100th) instead of turning off; it still turns off if that lasts 60 frames in a row (~1 s).
**Why:** 4-1 test (00:14–00:30 run, user: "seemed to line up well"). `gta.log`: ON (F7) in ABSOLUTE mode, CJ moved **998.4 m** from where he stood to the skater's spot in East Los Santos (1953.83, -1403.18, 13.57) without trouble; later ON (L3 + R3) again in ABSOLUTE mode (moved 5.7 m), OFF with L3 + R3. 222 per-second reports: GTA moved CJ at most 0.30 m between frames. But at 209.9 s skate mode switched itself off with "skater sample unreadable": a single bad read was treated like a lost link.
**Tested how:** user's play test (4-1). The fix: not compiled, not run.
**Result:** 4-1 works; the fix is untested.
**Still broken / not tested:** 6,977 ground clamps in the run (CJ lifted up to GTA's ground many times): small height differences between Los Santos East and GTA; harmless so far, not looked at in detail. The skater can only go where Los Santos East goes; outside it, nothing to skate on.
**Next:** decide the next step (step 4-2: research for the whole-map converter).

## [2026-10-08] Phase 4-0 done (Los Santos East lined up) + 4-1 absolute mode written

**Changed:**
- `src/common/convert.h`: new `LevelAlignment`, `kLosSantosEast`, `FindLevelAlignment`, `MapAbsolute`: for an aligned level, GTA feet position = offset + scale x (x, -z, y). Los Santos East: level ID `0x0274F7DE`, scale **0.02422967 m per unit (41.27 units per metre)**, offset (1789.2105, -1397.8612, 24.9750), no rotation.
- `src/common/convert_test.cpp`: 8 new checks with the three landmarks (29 checks, all pass in the cloud).
- `src/gta/skatemode.cpp`: **absolute mode** (step 4-1): when skate mode turns on while reTHAWed is in an aligned level, CJ is moved (area loaded first, as F8 does) to the GTA spot matching the skater, and from then on placed at `MapAbsolute(skater)` + his feet height every frame instead of anchor + movement. The ON line says ABSOLUTE or relative and the level ID. Skate mode turns off if reTHAWed changes level.
**Why:** step 4-0 result. User marked 3 landmarks with F9 in both games (00:00–00:08 run): 1 = right corner of the skatepark, 2 = hospital steps, 3 = night club doors right of the palm tree. Pairs (skater -> CJ's feet): (7694.87, -515.66, 2136.50) -> (1976.10, -1449.69, 12.48); (9748.89, -370.69, 1063.16) -> (2024.92, -1423.47, 15.99); (3640.20, -513.87, 14186.43) -> (1877.47, -1741.69, 12.52). Fitted scale + rotation + offset: rotation 0.04 degrees (none), distances agree to 41.27 units per metre from every pair and from the heights; leftover errors 0.45 m, 0.53 m, 0.11 m sideways (how exactly CJ / the skater stood on the spot) and under 0.5 cm in height. The axes are the same as the ones found in phase 2 (GTA y = -THAW z), and the level's scale differs from the 0.0254 used for relative mode.
**Tested how:** unit test (cloud). Skate mode change not compiled, not run.
**Result:** 4-0 done. 4-1 none yet.
**Still broken / not tested:** 4-1 everything. If CJ is far away when skate mode goes on, the area has to load (the game may freeze for a moment, as with F8). Places where Los Santos East differs from GTA (edges, anything the porter changed) will still feel like invisible walls.
**Next:** user builds; skate test in Los Santos East: grind/hit something in the skatepark and see CJ at the same object in GTA.

## [2026-10-08] Phase 4-0: F9 landmark marks in both games (+ data attempt)

**Changed:**
- New `src/gta/mark.h` / `mark.cpp`: F9 with GTA in front logs "mark #N (F9): CJ at x y z | ground below z | heading". Read only.
- `src/thaw/main.cpp`: F9 with reTHAWed in front logs "mark #N (F9): skater at x y z | speed | level". Read only. Version line "phase 4-0".
- `src/gta/main.cpp`: calls the mark helper every world update; version line "phase 4-0". `CMakeLists.txt`: `mark.cpp` added.
**Why:** step 4-0 (line Los Santos East up with GTA). First tried from the data (read only, in the cloud scratch area): the level's collision has 7,532 objects (64-byte records from offset 0x50 after a 0x50-byte header: name checksum, counts, box min/max, offsets); their name checksums match **no** GTA model name from `data\maps\LA\*.ide`, and GTA's text map files list only 847 LA objects (most placements are in binary files inside `gta3.img`), so matching by shape gave no trustworthy answer (the vote landed on the same spot for every guess: an artefact of many small objects sitting near the level's origin). In the game is simpler and exact: stand the skater and CJ on the same landmarks and press F9 in each game.
**Tested how:** not compiled, not run.
**Result:** none yet.
**Still broken / not tested:** everything.
**Next:** user builds, picks 3 to 4 landmarks far apart, marks them in both games in the same order; Claude works out the formula and how far off each landmark is.

## [2026-10-07] Phase 4 research: the "Los Santos East" reTHAWed map

**Changed:** `docs/DESIGN.md` new section 8.8 (draft phase 4 plan). No code.
**Why:** user wants the whole map skateable eventually and pointed to "Los Santos East" (thpsgoat.com mod 598, original creator PunkyDTH, uploaded by yngzoch), a piece of GTA SA's East Los Santos already converted into a reTHAWed level; now installed in `reTHAWed\data\mod\UserMods\Maps\Los Santos East`.
**Tested how:** read only: copied `level.ini` and `SALSEAST.pak.wpc` to the cloud scratch area (not the repo) and read the PAK index and the collision header with a small script.
**Result:** `level.ini` says "Converted with reMAP". The PAK holds textures (~59 MB), visible geometry (~28 MB), the level script with nodes and rails (`SALSEAST.qb.wpc`, 2.9 MB; names like "GrindWoodRailing", "Railroad TP") and THAW collision version 10 (16.7 MB: 7,532 objects, 686,760 corners, 495,075 triangles; overall box about x -94,148..49,877, y (up) -11,043..12,572, z -55,045..57,672 THAW units, likely including distant background). So SA geometry -> THAW collision + rails has been done before and reTHAWed loads it as a user map.
**Still broken / not tested:** not known yet how its coordinates relate to GTA's (scale, axes, offset): that is step 4-0. Not loaded with our plugins yet.
**Next:** user approves (or changes) the phase 4 plan; then 4-0.

## [2026-10-07] Phase 3-4 tested: GTA ignores the controller in skate mode; phase 3 works

**Changed:** nothing (test only; docs updated).
**Why:** test of 3-4 (and 3-5, the play test, done in the same run).
**Tested how:** user built, airport in GTA, DualSenseX, launcher, reTHAWed level, then in GTA: F7 on, skated, right stick, Start, Start, L3 + R3 off, L3 + R3 on, Start, Start, L3 + R3 off, Start (23:24–23:30 run). Claude read both logs.
**Result:** **works.**
- Pad blanked every frame while skate mode was on (57–61 per second), not at all when off; start/stop logged at each toggle.
- **Start in skate mode paused only reTHAWed** (thaw 78.8 s; GTA's control flags stayed 0x0010, so GTA's menu did not open); skate mode logged "holding (reTHAWed paused)" and "moving again" after the second Start. After skate mode was off, Start opened GTA's menu as normal (226.6 s, flag 0x0001).
- **L3 + R3 turned skate mode off and on again** (207.4 s, 215.4 s, 225.5 s), read from the copy taken before blanking.
- F7 needed no extra tries this time (no "ignored" lines logged), so the earlier F7 problem is not explained; the new log line stays in for next time.
- User: when the skater hit a wall in reTHAWed, CJ stopped too, "as if there was an invisible wall". Expected: CJ copies the skater, and the skater lives in a THAW level whose walls don't exist in GTA. Planned fix: a nearly empty flat custom park as the hidden level (DESIGN section 5), later GTA's own ground and walls fed into THAW (phases 4 and 5).
**Still broken / not tested:** right-stick camera not confirmed (no complaint). Keyboard Esc during skate mode not tested. CJ has no skating animation.
**Next:** user's choice: an empty custom park in reTHAWed (no code), then plan phase 4.

## [2026-10-07] Phase 3-4: GTA ignores the controller in skate mode (+ F7 diagnostics)

**Changed:**
- `src/gta/padlink.cpp` / `.h`: after copying pad 0 into the link, our `UpdatePads` function now **blanks pad 0** (`NewState` all zero: buttons up, sticks centred) while skate mode is on, so GTA's pause menu, camera and CJ don't react to the controller. Logs when blanking starts and stops; the per-second "pad:" line now also counts blanked frames. New `PadLinkRawButtons()`: the last frame's buttons before blanking.
- `src/gta/skatemode.cpp`: L3 + R3 is now read from `PadLinkRawButtons()` (GTA's own pad is blank during skate mode, so the combo could otherwise never turn it off); falls back to GTA's pad if the hook isn't running. New: an F7 or L3 + R3 press while GTA isn't the front window is logged with the front window's process, class and title.
- `src/gta/main.cpp`: version line "phase 3-4".
**Why:** step 3-4 (Start opened GTA's pause menu while skating); and the open F7 question from 3-3: user said GTA was "in a small window" (reTHAWed full screen behind it) and they watched CJ in GTA, so F7 was pressed with GTA visible but, by the log, not the front window. The new log line will say which window was.
**Tested how:** not tested, not compiled. (No cloud syntax check: these files need plugin-sdk's headers.)
**Result:** none yet.
**Still broken / not tested:** everything. Keyboard keys that GTA merges into the pad are blanked too during skate mode; whether Esc still opens GTA's menu (if GTA reads it directly) is unknown. To pause GTA while skating: F7 off first.
**Next:** user builds; skate test; Start should pause only reTHAWed (skate mode then holds).

## [2026-10-07] Phase 3-3 tested: skating live from GTA works

**Changed:** nothing (test only; docs updated).
**Why:** test of 3-3.
**Tested how:** user built, loaded GTA (airport), started DualSenseX and the launcher, loaded a reTHAWed level, switched to GTA, F7, skated with the controller (23:09–23:14 run). Claude read `gta.log` and `thaw.log`.
**Result:** **works.** Import slots in `rthw.dll` were SDL2.dll's own and were replaced; `reTHAWed.exe` doesn't import them. Source changes logged as expected: REAL while reTHAWed was in front, NOTHING with skate mode off and reTHAWed behind, **GTA from the moment skate mode went on** (thaw 116.8 s = GTA 217.1 s). reTHAWed then asked 1,200 questions per second (20 per frame at 60 fps), all answered from GTA's frames. CJ followed: e.g. stick forward / left / right in GTA's log (217–231 s) and CJ moving ~50 m around the airport with heading changes and two small hops (ollies), target = actual every second. Skating stopped when Start opened GTA's menu (243.4 s, skate mode held as designed); the user then quit GTA, and reTHAWed switched to NOTHING ("GTA's controller frames stopped").
User: "CJ slid around as I skated".
**Still broken / not tested:**
- User had to Alt+Tab out of and back into GTA a couple of times before F7 worked. The log shows no refused F7 (refusals are logged), so the key wasn't seen with GTA as the front window: GTA's front-window check (`GetForegroundWindow`) said "not GTA" until 217 s, though reTHAWed wasn't the front window either from GTA 119 s on. Unknown which window was. Possibly related to 3-1 (reTHAWed no longer hears it lost focus, so it may stay drawn on top). Asked the user what was on screen.
- Start still opens GTA's pause menu (step 3-4).
**Next:** clear up the F7 / front-window question; then step 3-4.

## [2026-10-07] Phase 3-3: reTHAWed uses the controller from GTA

**Changed:**
- New `src/thaw/input.h` / `input.cpp`: on start, finds `SDL_GameControllerGetButton` and `SDL_GameControllerGetAxis` in `rthw.dll`'s import table (from `SDL2.dll`; checked in the cloud from the file: slots at `rthw.dll`+`0x4D1998` / `+0x4D1994`) and replaces both with ours (logs the old values and whether they were SDL2.dll's own). Ours answer from one of three sources, chosen every ~15 ms and logged on every change with the reason: **REAL** (reTHAWed is the front window: SDL as before), **GTA** (reTHAWed behind, skate mode on in GTA, GTA's controller frame under 100 ms old: values from the link) or **NOTHING** (otherwise: nothing pressed, sticks centred). Mapping: Cross=A, Circle=B, Square=X, Triangle=Y, Select=Back, Start, L3/R3=stick clicks, L1/R1=shoulders, D-pad; sticks GTA x256 (clamped), L2/R2 GTA 0..255 -> 0..32767. Once per second: how many questions each source answered.
- `src/thaw/main.cpp`: calls it every pass and every second; version line "phase 3-3".
- `CMakeLists.txt`: `input.cpp` added.
**Why:** step 3-3 of the phase 3 plan. partymod's `pollController` (which reTHAWed's input follows) reads the controller only through these two SDL functions, with sticks as `(axis >> 8) + 128` and L2/R2 as pressed when `(axis >> 7) > 128`; the scaling above fits that.
**Tested how:** `input.cpp` passes a syntax check in the cloud (clang, Windows 32-bit target, stand-in headers). Not compiled with Visual Studio, not run.
**Result:** none yet.
**Still broken / not tested:** everything. reTHAWed must already have the PS5 controller as player 1 (pressing a button on it in reTHAWed's menus does that, as before); otherwise reTHAWed never asks. GTA still reacts to the controller in skate mode too (Start opens GTA's pause menu; the right stick turns GTA's camera) until 3-4. Not known whether reTHAWed reads anything else (e.g. keyboard, which stays real).
**Next:** user builds; skate test with GTA in front: skater must not move with skate mode off, must follow the controller with skate mode on, and CJ should follow live.

## [2026-10-07] Phase 3-2 tested: the controller arrives in reTHAWed

**Changed:** nothing (test only; docs updated).
**Why:** test of 3-2.
**Tested how:** user built, loaded a GTA game, started DualSenseX and the launcher, pressed buttons, sticks and triggers in GTA with skate mode off (22:55–23:01 run). Claude compared `gta.log` and `thaw.log`.
**Result:** **works.**
- Hook: the call at `0x53BEE6` went to GTA's own `CPad::UpdatePads` (`0x541DD0`, inside `gta_sa.exe`), so **GInput does not hook this call**; after ours it went to our function. Installed at 18.4 s (first world update). No later change reported.
- Every world update sends a frame (49 to 61 per second; GTA ran at ~50–54 frames per second with reTHAWed running, 60 when not).
- reTHAWed received every press, in order, each as a separate change: Cross, Circle, Square, Triangle, L1, Start, D-pad Down; stick and trigger values match GTA's (left stick about −128 to 127, R2 0 to 255, right stick seen too).
- Age on arrival: 0 to 16 ms for button changes, at most 32 ms (two GTA frames) in the per-second check.
**Still broken / not tested:** nothing is used by reTHAWed yet. GTA's own stick values only reach about ±128 (vs SDL's ±32767): the scale for reTHAWed is set in 3-3. Not checked: keyboard (GTA merges it into the same pad), GTA's main menu (not sent, by design).
**Next:** step 3-3: reTHAWed uses the input from GTA when it is not the front window.

## [2026-10-07] Phase 3-2: GTA sends the controller to reTHAWed (logged only)

**Changed:**
- `src/common/link.h`: shared block **version 3** (`Local\MyPassthrough_v3`): new controller section written by GTA (sequence lock, time, frame count, 14 button bits named after GTA's pad buttons, 6 axes: sticks and L2/R2 as GTA sees them). Still 128 bytes. Both plugins must be rebuilt together (they are).
- New `src/gta/padlink.h` / `padlink.cpp`: on the first world update, points the call at `0x53BEE6` (inside `CGame::Process` at `0x53BEE0`, its first call: `CPad::UpdatePads`, `0x541DD0`) at our function, which calls whatever the call went to before (GTA, or a mod that got there first: logged with its file) and then copies pad 0's `NewState` into the link. The 5 bytes are logged before and after; once per second a "pad:" line (frames, sent, buttons held, stick values) and a check that the call still goes to us.
- `src/gta/main.cpp`: calls it every world update and every second; version line "phase 3-2".
- `src/thaw/main.cpp`: reads the controller section every pass (~15 ms); logs every button change at once (with frame number and age in ms), and once per second frames received, the oldest age, and the stick values. Version line "phase 3-2". Read only.
- `CMakeLists.txt`: `padlink.cpp` added.
**Why:** step 3-2 of the phase 3 plan (`docs/DESIGN.md` 8.7). Found by reading `gta_sa.exe` (read only, in the cloud): `CPad::UpdatePads` is called at 6 places; in the world update it is the very first call of `CGame::Process` (`0x53BEE6`), before the pause menu's `CMenuManager::Process` (`0x53BF44`), so this is also the place to blank the pad in step 3-4.
**Tested how:** link v3 publish/read logic: 15/15 checks pass in the cloud (g++, with a stand-in for `windows.h`). The plugins themselves are not compiled or run.
**Result:** none yet.
**Still broken / not tested:** not compiled with Visual Studio. GInput may hook this call or read the controller elsewhere (the log shows where the call went). In GTA's main menu (no game loaded) nothing is sent (that loop calls `UpdatePads` from `0x53E78B`, not hooked). Nothing is fed to reTHAWed yet; the controller still reaches reTHAWed directly as before.
**Next:** user builds, presses buttons in GTA; Claude compares `gta.log` "pad:" lines with `thaw.log` "input from GTA" lines.

## [2026-10-07] Phase 3-1 tested: reTHAWed keeps running behind GTA

**Changed:** nothing (test only; docs updated).
**Why:** test of 3-1 attempt 1.
**Tested how:** user built, started GTA, DualSenseX and the launcher, rolled in a reTHAWed level, Alt+Tab to GTA and back, twice (22:47–22:49 run). Claude read `thaw.log`.
**Result:** **works.** The plugin found reTHAWed's window (class `SDL_app`, title "reTHAWed", 3840x2160) 2.4 s after start and wrapped it (Unicode). Each Alt+Tab away was swallowed (one each of `WM_ACTIVATE` inactive, `WM_ACTIVATEAPP` false, `WM_KILLFOCUS`, `WM_NCACTIVATE` false; 3 times in total including one at start-up); THAW's focus flag `0x008A35D5` stayed 1 the whole time. With GTA in front (~44 s to ~76 s and ~86 s to ~110 s) the skater kept moving at ~500 units/s, turning, going up and down, and turning round at a ramp (110 s: 24 s of continuous movement). User: "It looked like the skater kept moving".
**Still broken / not tested:** at 55.45 s, while GTA was in front, reTHAWed's pause flag turned ON (off again at 77.8 s, after coming back). **Confirmed by the user: they pressed Start while in GTA**, and it reached reTHAWed: with focus loss hidden, SDL now gives reTHAWed the PS5 controller even when GTA is in front (the expected side effect, fixed in 3-3). No stuck keys reported.
**Next:** step 3-2: controller state from GTA into the link (read only).

## [2026-10-07] Phase 3-1 attempt 1: reTHAWed never hears it lost focus

**Changed:**
- New `src/thaw/focus.h` / `focus.cpp`: our THAW plugin finds reTHAWed's window (every visible, unowned window of the process is listed in the log once; the biggest is used), remembers its window procedure, and puts ours in front of it (`SetWindowLongPtr`). Ours swallows `WM_ACTIVATE` (inactive), `WM_ACTIVATEAPP` (false), `WM_KILLFOCUS` and `WM_NCACTIVATE` (false); everything else, including "got focus", goes to the original. Counts are logged once per second when they change. Also reads (read only, after a code-byte check of the `WM_ACTIVATE` handler at `0x00543D59` / `0x00543D6F`) THAW's focus flags `0x008A35D5` and `0x0072E850` and logs every change.
- `src/thaw/main.cpp`: calls it every loop pass and once per second; version line "phase 3-1: keep running behind GTA".
- `CMakeLists.txt`: `focus.cpp` added to the THAW plugin.
**Why:** phase 3-0 showed reTHAWed pauses the moment it loses focus. THAW and SDL both learn about focus only from these window messages, so hiding them should keep it running (SkyCraft does the same for Minecraft).
**Tested how:** not tested. `focus.cpp` passes a syntax check in the cloud (clang, Windows 32-bit target, with a stand-in for `windows.h`); not compiled with Visual Studio.
**Result:** none yet.
**Still broken / not tested:** everything. Not known whether something else (rthw.dll, THAW's scripts, `GetForegroundWindow`) also triggers the pause. Side effects to watch: SDL thinks it still has focus, so the PS5 controller may move the skater while you're in GTA (fixed in 3-3); a key held during Alt+Tab (Alt itself) may look stuck to reTHAWed until it is in front again. Our window procedure stays until reTHAWed closes (the plugin is never unloaded before that). Attempt 2 if it still pauses: keep `0x008A35D5` at 1 and find what turns the pause on.
**Next:** user builds and repeats the 3-0 test; Claude reads `thaw.log`.

## [2026-10-07] Phase 3-0 test: reTHAWed pauses itself behind GTA

**Changed:** `docs/DESIGN.md` (3-0 result, new plan for 3-1, findings). No code.
**Why:** step 3-0: does reTHAWed keep running when GTA is in front?
**Tested how:** user rolled in reTHAWed, Alt+Tab to GTA, waited, came back, quit (22:27–22:29 run). Claude read `thaw.log` and `gta.log`.
**Result:** **no, it stops.** At 97.66 s (the Alt+Tab) `thaw.log` shows `paused flag at 0x007CDD2C = ON`, and from 97.69 s the skater's position and speed (503) don't change at all until the user quit (120.5 s). User said the skater "kept rolling"; the log says he froze mid-roll (a frozen frame of a rolling skater looks the same). GTA kept running at 60/60 the whole time. Then read only, from `reTHAWed.exe` on disk: the window procedure's `WM_ACTIVATE` handler (`0x00543D51`) sets THAW's focus flag `0x008A35D5` (the one partymod calls `isFocused`) and clears `0x0072E850` on focus loss. Not found yet: what turns the pause on.
**Still broken / not tested:** reTHAWed can't run behind GTA yet.
**Next:** step 3-1 attempt 1: our THAW plugin hides focus loss from reTHAWed's window (swallow the "lost focus" messages).

## [2026-10-07] Phase 3 plan written (play it live)

**Changed:** `docs/DESIGN.md` new section 8.7. No code.
**Why:** user asked to plan phase 3. Research: partymod `input.c` (reTHAWed's input code follows it): THAW's "waiting for focus" loop is replaced by `processEventsUnfocused`, focus flag `isFocused_again`; controller read through `SDL_GameControllerGetButton` / `SDL_GameControllerGetAxis`, which `rthw.dll` imports from `SDL2.dll`. plugin-sdk: GTA reads the controller in `CPad::UpdatePads` (`0x541DD0`), no ready-made event for it. Chosen: GTA reads the controller and passes it to reTHAWed through the link (option B); option A (reTHAWed reads the controller in the background) kept as fallback.
**Tested how:** not tested (plan only).
**Result:** plan, steps 3-0 to 3-5.
**Still broken / not tested:** everything in phase 3. First unknown: whether reTHAWed's game keeps running when it isn't the front window (step 3-0, a test with no code).
**Next:** user approves the plan; step 3-0 test.

## [2026-10-07] Skate mode fix 1 tested: phase 2-2 works

**Changed:** nothing (test only; docs updated).
**Why:** retest of fix 1 (mirror, GTA menu hold).
**Tested how:** user rebuilt (log shows "phase 2-2, fix 1"), F8 to the airport, F7 on, skated in reTHAWed with a left U-turn and ollies, paused with Start, came back to GTA, F7 off, walked. 22:08–22:15 run. Claude read `gta.log`.
**Result:** works.
- **Left/right fixed:** CJ's heading went 76 → 88 (west) → 155 → −102 → −85 degrees (east): from west through south to east, a **left** U-turn, matching reTHAWed. User: "cj appeared to be facing the same way as the skater".
- **Pause:** Start opened GTA's menu (323.6 s, flag 0x0001); skate mode logged "holding (GTA menu open)" and **stayed on**. When the menu closed (339.8 s) GTA put back 0x0010 as expected, skate mode carried on ("holding (reTHAWed paused)"), and F7 off (382.5 s) left flags at 0x0000. No stuck controls.
- Following: target = actual every second, GTA moved CJ at most 0.027 m between frames, 1 ground clamp (ollie).
**Still broken / not tested:** while GTA's menu is open CJ doesn't follow; here the skater was unpaused and skated ~40 m during that time (GTA menu still open), so CJ stopped where the menu opened, not where the skater ended (his facing was ~25 degrees off the skater's final direction). After the menu closes CJ should jump to the skater's current spot; not seen, because reTHAWed was paused again by then. CJ shifted 0.24 m by himself while held (GTA settling him; harmless). Start reaching GTA is the shared-controller problem, for phase 3. Scale (inches) and the heading convention still not independently proven, but the path and facing look right to the user.
**Next:** plan phase 3 (watch it live: reTHAWed hidden, GTA keeps the controller and passes the inputs to reTHAWed) in `docs/DESIGN.md` before any code.

## [2026-10-07] First skate mode test (phase 2-2) + fix 1: mirror, GTA menu hold

**Changed:** after the test (results below):
- `src/gta/skatemode.cpp`: mapping `ySign` +1 → **-1** (left/right mirror). GTA's pause menu or map being open now **holds** skate mode (stays on, CJ not moved), checked before the automatic-off list. New: if our control bit (0x0010) comes back while skate mode is off, it is cleared and logged. Turning on is refused while GTA's menu is open.
- `src/gta/main.cpp`: version line "phase 2-2, fix 1: mirror + GTA menu hold". `src/common/convert.h`: comment only (default stays +1 so the unit test is unchanged; still 20/20 in the cloud).
**Why (test results, 15:37–15:45 run, `gta.log`):**
- **Works:** F8 teleport (waypoint at the airport, CJ moved there and the ground search worked). F7 on/off and L3 + R3 on (logged with reason). While on: CJ followed the skater 1:1 (target = actual every second, GTA moved him 0.000 to 0.020 m between frames, 2 ground clamps during ollies), ~30 m path, heading set every frame.
- **Mirror:** reTHAWed path (THAW x, z): straight along +z, then +x, then −z (a U-turn). With ySign +1 that is north → east → south in GTA, a **right** U-turn; the test asked for a left turn and the user saw CJ facing the wrong way. With −1 the same velocities give headings −180 → −125 → −32 → −2 degrees (south, turning left through east, to north): a left U-turn (checked in the cloud with the real velocities).
- **Pause problem:** pausing reTHAWed with Start **also opened GTA's pause menu** (GTA drew 0 frames from 246.3 s to 284 s and set control flag 0x0001), because GTA reads the same controller through DualSenseX even in the background. Our code took 0x0001 for "a script disabled player controls" and turned skate mode off, so CJ stopped before the skater's last 2 m and kept the heading of that moment. When GTA's menu closed (284.1 s), GTA put back the flags saved when it opened, **including our 0x0010**, so CJ's controls stayed off until the user pressed F7 twice (292.8 s / 299.0 s).
**Tested how:** fix 1 not compiled or run. Convert unit test still passes (cloud).
**Result:** none yet for fix 1.
**Still broken / not tested:** fix 1 not tested. Start in reTHAWed still opens GTA's menu (now harmless: skate mode holds; close GTA's menu when back in GTA). All controller presses meant for reTHAWed reach GTA too while skating; our control flag stops CJ walking, but not menus. Proper fix is phase 3 (input from GTA passed to reTHAWed). Heading direction convention (atan2(−dx, dy)) still believed, now consistent with the log. Scale (inches) still not proven. L3 + R3 crouch: not reported.
**Next:** user rebuilds and repeats the skate test (turn left, then pause with Start); check CJ turns left and skate mode survives the pause.

## [2026-10-07] Controller fix attempt 2: also turn off SDL's raw input reading

**Changed:** `src/launcher/main.cpp`: besides the ignore list from attempt 1, the launcher also sets `SDL_JOYSTICK_RAWINPUT=0` for reTHAWed and logs it.
**Why:** attempt 1 half worked. reTHAWed's `debug.txt` (15:29 run): at start-up only `Detected controller "PS5 Controller"` (before the fix the Xbox 360 one was detected too), but a bit later `Joystick added: Xbox 360 Controller` and then `Added player 1: PS5 Controller` / `Added player 2: Xbox 360 Controller`, the same as before. Reading SDL 2.26.2's source (`src/joystick`): the XInput, DirectInput, Windows.Gaming.Input and HIDAPI ways of finding controllers all check the ignore list (`SDL_ShouldIgnoreJoystick`), but the raw input way (`windows/SDL_rawinputjoystick.c`) does not, and it is on by default. So the virtual controller got in a second time through raw input. With raw input off, it is only seen through XInput, which drops it. The PS5 controller comes through HIDAPI (SDL's own PS5 support, name "PS5 Controller"), which raw input doesn't affect.
**Tested how:** user built it, ran DualSenseX (Xbox 360 emulation) and the launcher, used the reTHAWed menus with the PS5 controller, quit (15:35 run). Claude read `launcher.log` and reTHAWed's `debug.txt`.
**Result:** works. `launcher.log` shows both settings. `debug.txt`: only `Detected controller "PS5 Controller"`, `Joystick added: PS5 Controller`, `Added player 1: PS5 Controller`; no Xbox 360 lines at all. User: "It worked that time".
**Still broken / not tested:** a player with a real Xbox 360 controller would get no controller in reTHAWed; must become an option before release (DESIGN.md section 11). Not tested in a level, only menus. (Was attempt 2: it did not need STATUS.md.)
**Next:** F8 teleport test and phase 2-2 skate mode test. Controller option: before release.

## [2026-10-07] Launcher: reTHAWed ignores the virtual Xbox 360 controller (controller fix)

**Changed:** `src/launcher/main.cpp`: before starting reTHAWed, sets `SDL_GAMECONTROLLER_IGNORE_DEVICES=0x045E/0x028E` in the launcher's own environment (reTHAWed inherits it) and logs it. No other change.
**Why:** user plays with a PS5 controller and uses **DualSenseX** to emulate an Xbox 360 controller for GTA, because GTA's controller mod GInput is XInput (Xbox) only (its `ReadMe.txt`: it "ditches DirectInput in favour of XInput"; only DualShock 3 via SCP is mentioned). reTHAWed reads controllers through SDL2 (2.26.2 per its log) and supports the PS5 controller natively, so with DualSenseX running it saw two controllers (`PS5 Controller` and `Xbox 360 Controller` in the 00:30 `debug.txt`), which causes problems. partymod's input code (which reTHAWed's follows) only adds a controller if `SDL_IsGameController` is true, and SDL's ignore list makes that false for the listed device. 045E:028E is the Xbox 360 controller ID ViGEmBus (used by DualSenseX) reports.
**Tested how:** not tested, not compiled.
**Result:** half worked (user: "It did not work"). Built and ran; launcher logged the setting; reTHAWed no longer detected the Xbox 360 controller at start-up, but it was added later anyway and became player 2. See attempt 2 above.
**Still broken / not tested:** not confirmed that DualSenseX's virtual controller reports exactly 045E:028E, or that SDL applies the ignore list to devices found through XInput. Any real Xbox 360 controller would also be ignored by reTHAWed (when started by our launcher). Expect reTHAWed's `debug.txt` to still say `Joystick added: Xbox 360 Controller` (that line is before the check) but **not** `Detected controller "Xbox 360 Controller"` / `Added player ...: Xbox 360 Controller`. If it fails: attempt 2 would also turn off SDL's XInput reading for reTHAWed (`SDL_XINPUT_ENABLED=0`).
**Next:** user builds; DualSenseX running; launcher; check reTHAWed's controls; Claude reads `launcher.log` and reTHAWed's `debug.txt`.

## [2026-10-07] Test helper: F8 teleports CJ to the map waypoint

**Changed:** new `src/gta/teleport.h` / `teleport.cpp`: on F8 (GTA in front), reads the waypoint from `FrontEndMenuManager.m_nTargetBlipIndex` → `CRadar::GetActualBlipArrayIndex` → `CRadar::ms_RadarTrace[slot].m_vecPos`, loads the area (`CStreaming::LoadScene`), gets ground height (`CWorld::FindGroundZForCoord`), and moves CJ there with `CEntity::Teleport`, 1 m above the ground. Refuses (and logs why) if: no waypoint, CJ in a vehicle or dead, cutscene, black bars, any control flag set, GTA paused, skate mode on. Exception-protected. `skatemode.h/.cpp`: `SkateModeIsOn()`. `src/gta/main.cpp`: calls it from the world-update hook. `CMakeLists.txt`: adds `teleport.cpp`.
**Why:** user's request (option 2 of 2; user chose F8): an easy way to put CJ in an open area for the skate mode test, without installing CLEO + a third-party teleport script into GTA's main folder.
**Tested how:** not tested, not compiled. All plugin-sdk names checked in its source.
**Result:** none yet.
**Still broken / not tested:** not compiled or run. `LoadScene` may freeze GTA for a moment. If the ground there isn't loaded, the height may be wrong (CJ could land high and fall, or inside something); the log line shows the ground height used. Waypoint marker must be placed on the pause-menu map first.
**Next:** user builds; tests F8 (place waypoint, F8, check log), then the 2-2 skate mode test.

## [2026-10-07] Phase 2-2: skate mode (F7 / L3 + R3) moves CJ to follow the skater

**Changed:**
- New `src/gta/skatemode.h` / `skatemode.cpp`. Called once per GTA world update (after `CGame::Process`) from the existing hook, wrapped in exception protection (an error turns skate mode off, clears our control bit, and is logged).
  - **Toggle:** F7 (`GetAsyncKeyState`) or L3 + R3 (`CPad::GetPad(0)->NewState.ShockButtonL/R`), once per press, only while GTA is the foreground window.
  - **Turn on** only if: CJ exists, not in a vehicle, health > 0, player state "playing", no cutscene, no black bars, no other control flag set, GTA not paused, reTHAWed connected, sample < 1 s old, skater valid, reTHAWed in a level. Otherwise logs why not. On: anchors CJ's position and the skater's position, measures CJ's height above GTA's ground, sets our control bit (bit 4, `0x0010`).
  - **Each frame while on:** auto-off (with reason) on any of the above failing; **hold** (no movement) while GTA or reTHAWed is paused; otherwise CJ = anchor + mapped skater movement (`convert.h`: 0.0254 m/unit, GTA y = THAW z, y sign +1), nudged ahead by velocity × sample age (max 50 ms), never below GTA ground + the anchor height; speed set to zero; heading from the skater's velocity (if faster than 20 units/s).
  - **Logs:** every on/off/hold with reason; any change of the control flags value; once per second: frames moved/held, CJ's actual vs target position, how far GTA's physics moved CJ between frames, ground clamps, sample age, heading we set vs the heading CJ's matrix reports (checks GTA's heading convention).
- `src/gta/main.cpp`: hook calls `SkateModeFrameSafe(g_link, &Log)`; version line "phase 2-2". `CMakeLists.txt`: adds `skatemode.cpp`.
- Also: user ran `build\Release\convert_test.exe` on Windows: **passed** (user report, 14:41).
**Why:** step 2-2 of `docs/DESIGN.md` 8.6; first change to a game.
**Tested how:** not tested, not compiled (no Windows compiler in the cloud). Every plugin-sdk name used was checked in its source first.
**Result:** none yet.
**Still broken / not tested:** not compiled or run. Not done: hiding L3 + R3 from GTA (the combo is seen after GTA has processed the frame, so CJ may crouch / look back for a moment when toggling with the controller). Unknown: unit size, left/right mirroring, GTA's heading convention, whether bit 4 is really unused, whether CJ jitters, whether GInput reads the controller while GTA isn't in front.
**Next:** user builds and runs the 2-2 test (flat open area in GTA; F7 on/off, L3 + R3 on/off; skate straight, turn left, ollie, pause/unpause in reTHAWed; check CJ in GTA; off; walk).

## [2026-10-07] Phase 2-1 tested: GTA receives reTHAWed's skater ~95 times per second

**Changed:** no code. `README.md`, `docs/DESIGN.md` 8.6 updated.
**Why:** record the test of step 2-1.
**Tested how:** user built (all four targets built at 14:07, first time on Windows: `convert_test.exe` 121,344 bytes, THAW plugin 124,928, GTA plugin 148,992), ran GTA and the launcher, loaded a level, skated, paused and unpaused, back to menu, quit. Claude read `gta.log`, `thaw.log`, `launcher.log` and matched GTA's received samples against reTHAWed's own position lines.
**Result:** works.
- **Rate and freshness:** GTA logged 156 once-per-second reports: **93 to 96 new samples per second** (median 95; `Sleep(10)` gave ~10.5 ms here, faster than the 15 ms planned), samples **0 to 16 ms old** (median 0), **0 re-reads** needed (the sequence lock never caught a half-written sample). THAW side: 14,646 samples published in 156 s.
- **Values match:** of 34 GTA reports while the skater was moving, 9 match a reTHAWed position line exactly (to 2 decimals); all 34 lie on reTHAWed's logged path within one 100 ms step (max 42 units at ~700 units/s); the two logs' clocks differ by a constant 62.8 s (GTA's log started first).
- **States arrive correctly:** GTA saw `[state ?]`, `[menu]`, `[level]`, `[level, paused]`, then `[menu]` again, at the same moments reTHAWed logged them; level IDs `0x99CE4D72` (menus) and `0xBDC6CDE2` (this level).
- **Unpause now confirmed:** reTHAWed's pause flag turned ON / off three times (36.4/95.5 s, 107.0/111.5 s, 119.8/151.6 s).
- reTHAWed ran 156.4 s, exit code 0. Reading the skater ~95 times per second: **0 skater read failures**.
**Still broken / not tested:** ~100 state read failures (access violations, caught) in reTHAWed's first 1.3 s of start-up, none after; harmless but noisy. Not known whether the user ran `convert_test.exe` on Windows (asked).
**Next:** step 2-2 (skate mode moves CJ): first change to a game; user to confirm before it's written.

## [2026-10-07] Phase 2-1: link v2, reTHAWed publishes the skater; conversion + unit test

**Changed:**
- `src/common/link.h`: shared block **version 2**, name `Local\MyPassthrough_v2`, 128 bytes. New: `thawSeq` (sequence lock), `thawFlags` (skater valid / state valid / in level / paused), `thawLevelId`, `thawPos[3]`, `thawVel[3]`, `thawSampleTick`, `thawSampleCount`, `gtaSkateMode` (for 2-2). Helpers `PublishSkater` (THAW, the only writer) and `ReadSkaterShared` (GTA; retries up to 8 times if the numbers change while being copied).
- `src/thaw/main.cpp`: loop now `Sleep(10)` (about every 15 ms): reads state + skater and publishes every pass; position lines still logged every 100 ms; once per second also logs how many samples were published. Still read only inside reTHAWed.
- `src/gta/main.cpp`: once per second logs the published skater (in level / menu / paused, level ID, position, velocity, sample number, age in ms, samples in the last second). Read only: nothing in GTA changed.
- New `src/common/convert.h` (THAW → GTA mapping: scale 0.0254 m/unit, GTA x = THAW x, GTA y = THAW z × ySign, GTA z = THAW y; anchor-relative position; heading from velocity) and `src/common/convert_test.cpp`; `CMakeLists.txt` builds it as `convert_test.exe`.
**Why:** step 2-1 of `docs/DESIGN.md` 8.6.
**Tested how:** in Claude's cloud workspace: `convert_test.cpp` compiled with g++ (`-Wall -Wextra -Werror`) and run: **20 of 20 checks PASS**. `link.h` compiled against a small stand-in for `windows.h` (4-byte LONG, as on 32-bit Windows): block size is 128 bytes as required; publish → read round trip **9 of 9 checks PASS**, including refusing to read while the writer is mid-write. The plugins themselves: **not compiled, not tested** (no Windows compiler in the cloud).
**Result:** none in the games yet.
**Still broken / not tested:** not compiled on Windows. Reading the skater about 65 times per second from our own thread (was 10) is unsynchronised with reTHAWed; failures are caught and counted. GTA's heading formula in `convert.h` (atan2(-dx, dy)) is believed, not confirmed (that's for 2-2).
**Next:** user builds, runs `build\Release\convert_test.exe`, then GTA (into a game) + the launcher; load a reTHAWed level, skate ~10 s, pause, unpause, back to menu, quit both. Claude compares `gta.log` with `thaw.log`.

## [2026-10-07] Phase 2-0 tested: GTA keeps running while reTHAWed is in front

**Changed:** no code. `README.md`, `docs/DESIGN.md` 8.6 updated.
**Why:** record the test of `autoPause=0`.
**Tested how:** user started GTA, loaded into a game, started our launcher, stayed in reTHAWed, came back, quit both. User: "GTA did not pause". Claude read `gta.log`, `thaw.log`, `launcher.log`.
**Result:** works. GTA was in a game from 288.5 s; reTHAWed connected at 339.3 s and left at 388.4 s (ran 49.7 s, exit code 0). **GTA drew 60 frames and did 60 world updates per second throughout**, including all the time reTHAWed was in front (a few seconds at 54 to 59). reTHAWed's side saw GTA's drawn total rise +59 to +61 every second. **The Widescreen Fix setting is not needed.**
**Still broken / not tested:** not checked whether GTA reads the keyboard/controller while it isn't in front (matters for phase 2-2).
**Next:** step 2-1 (link v2: reTHAWed publishes the skater; GTA only logs it).

## [2026-10-07] Phase 2-0: Windowed Mode autoPause turned off

**Changed:** `D:\Games\GTA San Andreas\modloader\WindowedMode\iii.vc.sa.windowedmode.ini`: `autoPause=1` → `autoPause=0`. Only that character changed; the rest of the file is byte-for-byte the same (351 bytes before and after). Approved folder (AGENTS.md); not in the repo. To undo: set it back to 1.
**Why:** step 2-0 of `docs/DESIGN.md` 8.6: GTA must keep running while reTHAWed is in front.
**Tested how:** read the file back from the PC after writing: matches, shows `autoPause=0`. Not yet tested in the game.
**Result:** setting changed.
**Still broken / not tested:** unknown whether this is enough. GTA may also pause itself when it loses focus (Widescreen Fix has a separate `AllowAltTabbingWithoutPausing` option, currently 0, in a folder not approved).
**Next:** user test: GTA into a game, then the launcher, stay in reTHAWed ~20 s; Claude checks GTA's drawn frames in `gta.log` / `thaw.log`.

## [2026-10-07] Phase 2 plan: L3 + R3 added as a skate mode toggle; one monitor

**Changed:** `docs/DESIGN.md` 8.6: skate mode toggles with F7 **or clicking both sticks (L3 + R3)**, only while GTA is in front; test and risks updated for a single monitor. No code.
**Why:** user's request; user has one monitor.
**Tested how:** nothing to run. Checked plugin-sdk: GTA's controller state has `ShockButtonL` and `ShockButtonR` (`CPad` `NewState`); plugin-sdk's comment: R3 = look behind. In GTA on foot, L3 is crouch.
**Result:** plan updated.
**Still broken / not tested:** pressing L3 + R3 may make CJ crouch / look behind for a moment; we'll try hiding the combo from GTA. Unknown whether reTHAWed reacts to the combo while in the background.
**Next:** user approves; step 2-0.

## [2026-10-07] Phase 2 plan written (CJ follows the skater)

**Changed:** `docs/DESIGN.md`: new section 8.6. No code.
**Why:** next phase; plan before code; first phase that changes a game.
**Tested how:** nothing to run. Research in plugin-sdk's source (commit `15f15b60`): `FindPlayerPed`, `SetPosn`, `m_fHeadingCurrent/Goal` (`+0x558/+0x55C`), `m_vecMoveSpeed` (`+0x44`), `CPad::DisablePlayerControls` (`+0x10E`, flag set; plugin-sdk marks bits 1 and 4 unused), `CTimer::m_UserPause/m_CodePause`, `CCutsceneMgr::ms_running`, `TheCamera.m_bWideScreenOn`, `CPlayerInfo::m_nPlayerState`, `CWorld::FindGroundZFor3DCoord`. Hotkeys of installed mods read from their settings files: F5 (limit adjuster), F10/F11 (SkyGFX); Windowed Mode uses Alt+Enter.
**Result:** three steps: 2-0 GTA keeps running when not in front (settings only), 2-1 link v2 publishes the skater (no game changes), 2-2 F7 skate mode moves CJ.
**Still broken / not tested:** everything in phase 2. Open: unit size, left/right mirroring, GTA pausing when unfocused, GInput reading the controller while unfocused, whether bit 4 of the control flags is really unused.
**Next:** user approves the plan; then step 2-0.

## [2026-10-07] Phase 1-2 attempt 2 tested: menus vs level works

**Changed:** no code. `README.md`, `docs/DESIGN.md` 8.5 updated.
**Why:** record the test of attempt 2.
**Tested how:** user built (THAW plugin 124,416 bytes), ran the launcher: main menu, loaded a level, skated, paused, back to the main menu, quit. Claude read `thaw.log`, `launcher.log`.
**Result:** works.
- All 9 code checks `MATCH` (InFrontend now checked on its first 8 bytes only). Both groups enabled.
- **InFrontend's jump confirmed redirected by reTHAWed:** `E9 03 C9 3F 60` = jump to `0x60996F70`, **inside `D:\Games\reTHAWed\rthw.dll`**. The original check at `0x00594310` still answers correctly.
- Timeline: 1.6 s in front end **YES**, level ID `0x99CE4D72` (menus); 11.3 s in front end **no**, level ID `0xD94DB0CB` (the level); 21.1 s paused flag **ON**, skater position frozen from that moment; 29.9 s in front end **YES**, level ID back to `0x99CE4D72`, paused flag off.
- Position line tags: `[menu]` 2.3 to 11.2 s, `[level]` 11.3 to 29.8 s, `[menu]` 29.9 to 40.6 s.
- One state read failure at 0.3 s (access violation `0xC0000005`, game still starting up), caught; none after.
- **While paused, velocity still reads ~534** although the skater doesn't move: velocity is not reset on pause, so later code must use the pause flag.
**Still broken / not tested:** **unpausing not observed**: the game stayed paused from 21.1 s until the return to the menu at 29.9 s (user apparently quit to menu from the pause menu), so "flag turns off on unpause" is not tested. reTHAWed's own replacement for InFrontend (in `rthw.dll`) was not looked at; not needed so far.
**Next:** phase 1 done (apart from the unpause check). Plan phase 2: CJ follows the skater.

## [2026-10-07] Phase 1-2 attempt 2: separate check groups; log where InFrontend's jump goes

**Changed:** `src/thaw/skater.h/.cpp`: each code check now belongs to a group (skater or state); the InFrontend check covers only its first 8 bytes (`mov eax,[0x8A3B5C]; mov ecx,[eax+0x20]`); new `ReadInFrontendJump()` reads the 5-byte jump at `0x0059A668`, works out its target and which loaded file contains it. `src/thaw/main.cpp`: checks each group separately (a failure only disables its own group); logs the InFrontend jump once at start; state reading and skater reading run independently. Still calls the original check at `0x00594310`. `docs/DESIGN.md` 8.5: note added.
**Why:** attempt 1 failed (previous entry).
**Tested how:** not tested, not compiled.
**Result:** none yet.
**Still broken / not tested:** not compiled or run. If reTHAWed redirected InFrontend for a reason, the original check at `0x00594310` may not answer "menus?" the way reTHAWed does; the test will show it. This is attempt 2 of 2: if it fails, Claude stops and writes `STATUS.md`.
**Next:** same test as attempt 1 (menu ~5 s, level, skate, pause ~5 s, unpause, quit to menu, quit).

## [2026-10-07] Phase 1-2 attempt 1 FAILED: a code check didn't match in the running game

**Changed:** no code in this entry (fix is the next entry).
**Why:** record the failed test.
**Tested how:** user built and ran the menus / level / pause / back-to-menu test. Claude read `thaw.log`, `launcher.log`.
**Result:** 8 of 9 code checks matched; **`0x0059A660` (InFrontend) DIFFERENT at byte 9: expected `A3`, found `03`**. Byte 9 is inside InFrontend's final jump (to `0x00594310` in the file), so in the running game that jump points somewhere else; most likely reTHAWed's `rthw.dll` redirects `InFrontend` to its own code (not yet confirmed). Because the checks were all-or-nothing, the plugin then read **neither** the state **nor the skater** ("code checks FAILED; not reading the skater"). No harm: nothing was read or written; reTHAWed ran 69.9 s and exited normally (exit code 0); 69 heartbeats. **No data on menus/level/pause from this run.**
**Lessons:** (1) reTHAWed patches some of THAW's functions in memory, so a byte check against the file can fail in the running game; (2) skater and state checks must not block each other.
**Next:** attempt 2 (of 2 before writing STATUS.md): separate the two groups of checks; for InFrontend check only the unchanged first 8 bytes and log where its jump goes now (and in which module); keep calling the original check `0x00594310` (which matched).

## [2026-10-07] Phase 1-2: tell reTHAWed's menus from a level (read only)

**Changed:** `docs/DESIGN.md`: new section 8.5 (plan). `src/thaw/skater.h/.cpp`: 4 more code checks (`0x0059A660`, `0x00594310`, `0x0059C230`, `0x0059BD30`; exact bytes copied from the game file); new `ReadGameState()` in its own exception protection: game-flow pointer at `0x008A3B5C`, "in front end" by calling `0x00594310` on the object at game-flow `+0x20`, level ID at game-flow `+0x64`, pause flag byte at `0x007CDD2C`. `src/thaw/main.cpp`: reads it every 100 ms, logs each value when it changes, tags position lines `[menu]` / `[level]` / `[?]`. `README.md` updated.
**Why:** reTHAWed has a skater in its menus too; phase 2 must only follow the skater in a level.
**Tested how:** not tested, not compiled. Research: disassembled the script commands `InFrontend`, `GetCurrentLevel`, `GameIsPaused` and the table lookup `0x004D0510` (only reads; if the entry is missing, `0x00594310` would read an invalid address, which our exception protection catches).
**Result:** none yet.
**Still broken / not tested:** not compiled or run. If any of the now 9 code checks fails, the plugin reads neither skater nor state (all-or-nothing). Unknown whether the pause flag is set by the pause menu.
**Next:** user builds; launcher; main menu ~5 s; load a level; skate a little; pause ~5 s; unpause; quit to main menu; quit. Claude reads `thaw.log`.

## [2026-10-07] Phase 1-1 tested: skater position and velocity found and confirmed

**Changed:** no code. `README.md`, `docs/DESIGN.md` (sections 4 and 8.4) updated.
**Why:** record the first test of reading the skater.
**Tested how:** user built, ran our launcher, loaded a level, skated, did several ollies (some while moving, not exactly in the planned order; enough data), quit to menu, quit. Claude read `thaw.log` (389 position samples, 39 once-per-second checks) and `launcher.log`.
**Result:** works.
- All 5 code checks `MATCH`. **No read failures.** reTHAWed exited normally (exit code 0) after 41.9 s.
- Manager pointer at `0x0074FB44`: 0 at 0.1 s, then `0x1A08E740` from 0.3 s. Local skater found at 2.3 s (`0x192589A0`), **while still in the menus**: reTHAWed has a skater in its menus too.
- **Teleports** (position jumps with zero velocity): 2.4 s, 10.9 s (menu spots, e.g. `84.83 -67.50 21.70`), 18.3 s (level start, `-2336.49 96.38 -2482.72`), and back to the menu spots at 38.4 to 38.8 s after quitting the level.
- **Self-check while skating straight:** 23.2 s: moved 499.29 per s, average speed 500.87, **ratio 1.00**; 24.2 s: 505.97 vs 497.94, **ratio 1.02**. Lower ratios (0.6 to 0.9) only in seconds with ollies or slowing down, as expected for straight-line distance.
- **Up axis is y**: each ollie: y rises 65 to 90 units above the ground and falls back, x and z unchanged when in place; vertical velocity starts at about +400 to +430 and drops about 135 per 0.1 s (game gravity about 1,350 units/s²).
- **Skating direction:** along z (x constant at -2336.49); pushing speed settles at about 500 units/s. Going down a slope at 22.5 to 24.1 s: y 96 → 0 with vertical velocity -50 to -70.
**Still broken / not tested:** **units not confirmed**: 500 units/s and ~75-unit ollies fit inches (12.7 m/s, 1.9 m) but THAW exaggerates speed and jumps, so this isn't proof. Can't yet tell "menu skater" from "level skater" in code. Sign of the axes (which way is +x / +z relative to the level) not yet related to anything. Read from our own thread, 10 times per second; not tied to THAW's frames (no THAW hook yet).
**Next:** decide phase 1 is done; next steps could be units/handedness (compare a known THAW object size), a THAW per-frame hook (0b-3), or phase 2 (CJ follows the skater).

## [2026-10-07] Phase 1-1: read and log the skater's position and velocity (read only)

**Changed:** new `src/thaw/skater.h` and `src/thaw/skater.cpp`: 5 code checks (exact bytes at `0x005A9407`, `0x005A9416`, `0x005A98E3`, `0x005A98F6`, `0x004A0160`, copied from the game file); `ReadSkater()` reads the manager at `0x0074FB44`, calls `0x004A0160` for the local skater, reads position `+0x68/6C/70` and velocity `+0xC8/CC/D0`, all inside Windows exception protection. `src/thaw/main.cpp`: runs the code checks once (skater reading only if all match), then every 100 ms logs the sample (or state changes: manager/skater found or gone, read failures); once per second logs moved distance per second (using real elapsed time) vs average speed, and their ratio. Heartbeat and link unchanged (still once per second). `CMakeLists.txt`: adds `skater.cpp`. `README.md` updated.
**Why:** step 1-1 of `docs/DESIGN.md` 8.4.
**Tested how:** not tested, not compiled.
**Result:** none yet.
**Still broken / not tested:** not compiled or run. The ratio uses straight-line distance, so it will read low while turning. Calling `0x004A0160` from our own thread is read-only but not synchronised with the game; failures are caught and counted.
**Next:** user builds; launcher; load a level; stand still ~5 s, skate straight ~5 s, stop, 3 ollies in place; quit. Claude reads `thaw.log`.

## [2026-10-07] Phase 1 research and plan: where reTHAWed keeps the skater

**Changed:** `docs/DESIGN.md`: new section 8.4 (phase 1 plan). No code.
**Why:** phase 1 needs the skater's position from inside reTHAWed.
**Tested how:** read-only analysis in Claude's scratch area: cloned PARTYMANX's partymod-thaw (MIT, commit `5700a673`), compared its byte patterns with a copy of `reTHAWed.exe`, searched the exe's script command table, disassembled a few functions with `objdump`. Nothing run, nothing in the game folder changed.
**Result:**
- partymod's patcher accepts a 21,442,560-byte `THAW.exe` (same size as the user's retail exe) and produces the patched game; `reTHAWed.exe` matches partymod's expected bytes at **9 of 9** addresses checked, so it has THAW 1.01's code layout.
- `GetSkaterPosition` → `0x005A98D0`: reads floats at object `+0x68/+0x6C/+0x70` as x/y/z (object = script `+0xC4`).
- `SkaterSpeedGreaterThan` → `0x005A93F0`: global `0x0074FB44`, call `0x004A0160` (local skater), velocity floats at `+0xC8/+0xCC/+0xD0`. That global + call pair occurs 66 times in the game code.
- `PrintSkaterPosition` / `SkaterDebugOn` are empty stubs (`mov al,1; ret`) in the released game.
- partymod uses `0x0072E850` as THAW's "window focused" flag (useful for phase 3).
**Still broken / not tested:** none of these addresses tested in the running game yet. Not confirmed that the local-skater object has its position at `+0x68`.
**Next:** user approves the plan; then step 1-1 (read-only logging of skater position and velocity).

## [2026-10-07] Phase 0c confirmed with our full setup (GTA windowed): phase 0 done

**Changed:** no code. `README.md`, `docs/DESIGN.md` updated.
**Why:** confirm the link with our launcher and plugin and GTA in a window, after the freeze fix.
**Tested how:** user started GTA (windowed via the Windowed Mode plugin), then our launcher, switched back and forth, quit reTHAWed, then GTA. Claude read `gta.log`, `thaw.log`, `launcher.log`.
**Result:** works, **no freeze** (user report). Launcher: plugin loaded in 0.172 s, reTHAWed closed after 63.3 s, exit code 0. THAW side: `GTA connected`, GTA beat **+1 per second 61 times** (one +0 at connection). GTA side: `THAW connected` at 29.1 s, THAW beat **+1 per second 62 times**, `THAW not connected` at 92.2 s after reTHAWed closed.
**Still broken / not tested:** GTA's log has no "first drawn frame" line and GTA's drawn total stayed at 0, so **GTA stayed on its main menu** this run; switching with GTA in a loaded game and in a window is not tested yet. Borderless mode not tested.
**Next:** plan phase 1 (find the skater's position inside reTHAWed).

## [2026-10-07] Freeze explained: full-screen GTA; with GTA in a window reTHAWed keeps running

**Changed:** no code. `docs/DESIGN.md` (section 8.3 result, new requirement in section 10), `README.md` (requirements, what works).
**Why:** record the windowed-mode test.
**Tested how:** user started GTA, switched it to windowed with Alt+Enter, started reTHAWed with its own launcher (our THAW plugin not loaded), switched GTA, then back to reTHAWed, a few times. Claude read `logs\gta.log` and the Windowed Mode plugin's saved settings.
**Result:** **reTHAWed did not freeze.** So the freeze came from GTA running in **exclusive full screen**: when GTA takes the screen, reTHAWed (Direct3D 9) can't recover when switched back to.
- Windowed Mode plugin saved its settings in its own folder (`modloader\WindowedMode\iii.vc.sa.windowedmode.ini`): `mode=1` (window), 1218×813, `autoPause=1`, `autoResume=1`. Nothing new in GTA's main folder.
- `gta.log`: our hook spots unchanged at 5 s (the Windowed Mode plugin does not use them). GTA stayed on its main menu this run (0 drawn / 0 world updates, as in earlier main-menu tests). THAW not connected, as expected (reTHAWed started without our plugin).
**Still broken / not tested:** not yet re-tested with **our** launcher and plugin and GTA in a window together. Borderless mode (`mode=2`) not tested. GTA still pauses itself when not in front (`autoPause=1`).
**Next:** confirmation run: GTA windowed + our launcher, switch back and forth; then phase 0c is done.

## [2026-10-07] Installed ThirteenAG's Windowed Mode plugin for GTA (third-party)

**Changed:** installed `III.VC.SA.WindowedMode.asi` v2.1 (799,744 bytes) and its `README.md` into `D:\Games\GTA San Andreas\modloader\WindowedMode\` (user approved this path; recorded in `AGENTS.md`). Not our code; not in the repo.
**Why:** reTHAWed freezes when switching back to it from full-screen GTA (user: froze "when I alt tabbed to gta then back to rethawed"), even without our code. Next test needs GTA in a window / borderless. None of the installed mods can do that (checked the settings files of Widescreen Fix, SilentPatch, SkyGFX, GInput, Framerate Vigilante, limit adjuster).
**Tested how:** downloaded the official v2.1 release zip from github.com/ThirteenAG/III.VC.SA.WindowedMode (zip SHA-256 `b97d74b6...2505`); checked the plugin is a 32-bit Windows plugin; README lists "Classic GTA San Andreas v1.0 US". After copying, read it back from the PC: SHA-256 `c8d37c75...c85a`, same as the download.
**Result:** installed. Not yet run.
**Still broken / not tested:** not known whether it works with our GTA plugin and the other installed mods. It saves its window size/style in a settings file (location not checked yet). Alt+Enter cycles window styles. Widescreen Fix's `AllowAltTabbingWithoutPausing = 0` (only works in windowed mode) left unchanged.
**Next:** test: GTA in borderless/windowed mode + reTHAWed (its own launcher), switch back and forth, see if reTHAWed still freezes.

## [2026-10-07] Comparison test: reTHAWed freezes without our code too

**Changed:** nothing.
**Why:** find out whether our plugin caused the reTHAWed freeze in the phase 0c-1 test.
**Tested how:** user started GTA in full screen (no change), then reTHAWed with **its own launcher** `ReTHAWedLaunch.exe` (so our THAW plugin was not loaded), and switched between the two windows.
**Result:** **reTHAWed froze again.** So the freeze is not caused by our plugin or our launcher. It happens with GTA (full screen) and reTHAWed running together.
**Still broken / not tested:** cause still not confirmed. Next suspect: GTA's full-screen mode. Not yet known exactly when reTHAWed freezes (on switching away, or on switching back).
**Next:** same test with GTA in windowed mode. This is a design question too: reTHAWed must keep running while GTA is in front.

## [2026-10-07] Phase 0c-1 tested: link works both ways; reTHAWed froze (cause unknown)

**Changed:** no code. `README.md` updated.
**Why:** record the first test with both games running and linked.
**Tested how:** user built, started GTA (into a game), then our launcher (reTHAWed), switched between the windows, quit reTHAWed, then GTA. User reports: "GTA ran fine but reTHAWed froze". Claude read `gta.log`, `thaw.log`, `launcher.log` and reTHAWed's `debug.txt` (compared with the earlier runs).
**Result:**
- **Link: works.** GTA created the shared block, THAW opened it. GTA's log: `THAW connected, process id 28684` at 80.2 s, then THAW's beat rose **+1 every second, 133 times in a row**; `THAW not connected` at 214.3 s after reTHAWed closed. THAW's log: `GTA connected, process id 21748`, GTA's beat **+1 every second** for all 134 s.
- **GTA stops completely when it is not the front window:** 0 drawn frames and 0 world updates per second whenever reTHAWed had focus (e.g. seconds 34 to 98). When GTA came back to the front it showed the pause-menu pattern (0 drawn, 60 world updates per second), then 60/60 when playing. GTA drew 465 frames in total during the 134 s reTHAWed ran.
- **reTHAWed froze** at some point while running (user report). Our THAW plugin's own thread kept logging every second without a gap (heartbeat 1 to 134). The launcher saw reTHAWed close normally after 134.9 s with exit code 0, and reTHAWed's `debug.txt` ends with the same shutdown sequence as the earlier normal quit. `debug.txt` has no line about losing focus, a lost graphics device, or an error that the earlier runs don't also have.
**Still broken / not tested:** the reTHAWed freeze is **not explained**. User's details (03:42 to 03:44): menu didn't respond to keyboard or controller; **no music and nothing moving**, so reTHAWed's game loop itself had stopped (not just input); closed with the X on the taskbar, after which it shut down cleanly (exit code 0); **GTA was in full screen**. Possible cause, not confirmed: GTA's full-screen mode takes over the display and reTHAWed's graphics (Direct3D 9) stop until they can recover, or reTHAWed pauses itself when it isn't the front window. Not yet known whether reTHAWed does the same **without our code**. No fix attempted yet.
**Next:** comparison test without our code: GTA in full screen, reTHAWed started with its own launcher, switch between them. Matters for the whole design: in phase 3 reTHAWed must keep running while GTA is in front.

## [2026-10-07] Phase 0c-1: shared memory link between the two plugins

**Changed:** new `src/common/link.h` (shared block `Local\MyPassthrough_v1`: magic "MPT1", version 1, `gtaPid`, `thawPid`, `gtaBeat`, `thawBeat`, `gtaDrawn`; 64 bytes, size checked at build time; open-or-create helper). `src/gta/main.cpp`: opens the block, writes its process id, once per second writes its beat and drawn-frame total, logs the THAW side ("connected", beat and rise per second, "not connected"); clears its process id on exit. `src/thaw/main.cpp`: same from the THAW side, also logs GTA's drawn total and its rise. `README.md` updated.
**Why:** step 0c-1 of `docs/DESIGN.md` 8.3.
**Tested how:** not tested, not compiled.
**Result:** none yet.
**Still broken / not tested:** not compiled or run. Not known whether GTA keeps drawing while reTHAWed has focus (the test will show it in `gtaDrawn`).
**Next:** user builds; starts GTA, then the launcher; switches windows a few times (~30 s); quits reTHAWed, then GTA. Claude reads `gta.log` and `thaw.log`.

## [2026-10-07] Phase 0c plan written (link between the two games)

**Changed:** `docs/DESIGN.md`: new section 8.3, the phase 0c plan. No code.
**Why:** user chose phase 0c next (over 0b-3). Plan before code.
**Tested how:** nothing to run.
**Result:** one step, 0c-1: a shared memory block `Local\MyPassthrough_v1` (layout in a new `src/common/link.h`) where each plugin writes its process id and a once-per-second beat and reads the other's; GTA also publishes its drawn-frame total.
**Still broken / not tested:** everything in phase 0c.
**Next:** user approves the plan; then step 0c-1.

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
