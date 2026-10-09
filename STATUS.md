# STATUS: phase 4-2d step 1 (our own reTHAWed map from GTA collision)

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
