# STATUS: tags (8.28 a): no Grove tag appears in GTA

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
`my-passthrough GTA East LS`). Wanted result: the map loads in reTHAWed and the
skater stands on invisible GTA ground.

## What works

- reTHAWed finds the map in `UserMods\Maps` and lists it ("GTA East Los Santos
  (my-passthrough)").
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
- Map output: `extracted\thaw_maps\my-passthrough GTA East LS\` (gitignored).
- Log: `D:\Games\reTHAWed\debug.txt` (overwritten on each run).
- History: `MODLOG.md` (both attempts), `docs/DESIGN.md` section 8.9.
- The map folder is still in `reTHAWed\data\mod\UserMods\Maps\`. It crashes
  only when that map is picked; the rest of reTHAWed is unaffected. It can be
  deleted at any time.
