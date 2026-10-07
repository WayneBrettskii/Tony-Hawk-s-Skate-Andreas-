# AGENTS.md

## Project
We are creating a mod that implements gameplay mechanics and features from Rethawed a mod for Tony Hawks American Wasteland into Grand Theft Auto Sanandreas

## Hard rules, never break these

1. **Never write game assets, decompiled code, or extracted game files into this
   repository.** They stay on this machine, untracked. If you need to read game
   data, read it from the install path at runtime or extract it to a gitignored
   folder.
2. **`.gitignore` is a whitelist.** It ignores everything and includes only
   source files. Do not switch it to a normal ignore list.
3. **Do not run `git commit` unless I asked.** Stage nothing beyond what the
   current task requires.
4. **Do not touch anything outside this project folder** unless I explicitly name
   the path. This includes my game installs: read them, never write to them.
5. **Single-player and offline only.** If this touches online play, anti-cheat, or
   DRM, stop and tell me. Offline play with anti-cheat switched off through the
   game's own option is fine. Bypassing anti-cheat is never fine.
   - *Decision 2026-10-07:* both games run from executables whose copy
     protection was removed before this project (reTHAWed ships `reTHAWed.exe`
     without the retail `THAW.exe`'s SecuROM 7; the downgraded GTA exe is the
     1.0 US "HoodLum" build). The user chose to proceed using them as installed.
     Our code never removes, bypasses or touches copy protection itself, and
     never uses the protected `THAW.exe`.
6. **Never put credentials in the repo or in any file you can read:** no API keys,
   no tokens, no passwords.

## How to work

- **Plan before code.** For anything more than a small fix, write the plan to
  `docs/DESIGN.md` first, then implement one step at a time.
- **One thing at a time.** Do not bundle unrelated changes. I want to be able to
  revert a single step.
- **Log, don't look.** You cannot see the game. Instrument instead: write
  positions, counts, timings, and state transitions to a log file so you can
  verify from the numbers. Do not try to visually inspect the game.
- **Tell me how to test it.** After every change, say the exact command to run and
  what I should see. "Done" without a test procedure is not done.
- **Ask before large refactors.** If you think the architecture is wrong, say so
  and explain, then wait for me.
- **Explain in plain language.** I am not the programmer here. If you use a term,
  explain it the first time.

## Honesty

- If something is not tested, write **"not tested"**. Never imply you verified
  something you didn't.
- If you are not sure, say you are not sure. A confident wrong answer costs me
  hours.
- If you hit something you cannot solve after two real attempts, stop and write
  up `STATUS.md` (see `STATUS-handoff.md`) instead of trying variations at random.
- Record failures alongside successes. A dead end I can see is worth more than a
  dead end I have to watch you repeat.

## Keep these files updated

- `MODLOG.md`: add an entry after every change. Template in
  `MODLOG-template.md`.
- `docs/DESIGN.md`: how the project works, in plain language. Update when the
  architecture changes, not on every commit.
- `README.md`: the "what works / what doesn't work" list. Test before you claim
  something works.

## Environment

- OS: [Windows 11 is the safe answer; most loaders are Windows-only]
- Game A: [Grand Theft Auto SanAndreas] [Version 1.0], installed at [D:\Games\GTA San Andreas]
- Game B: [reTHAWed] [5.0.0.1], installed at [D:\Games\reTHAWed]
- Loader: [SKSE / F4SE / Fabric / ...] [version]
- Which game is authoritative for the player: A (GTA), except while skate mode
  is on, when B (reTHAWed) is authoritative for the player's movement. See
  `docs/DESIGN.md` section 2.
- Approved write path in a game install: `D:\Games\GTA San Andreas\modloader\my-passthrough\`
  (our plugin files only). Nothing is ever written to `D:\Games\reTHAWed`.
- Language and version: [ C++ with MSVC]
- Agent: Claude
