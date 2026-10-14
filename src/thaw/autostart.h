// SkateAndreas 8.35 (test): can we start our map from reTHAWed's front end?
// See docs/DESIGN.md 8.35.
//
// reTHAWed has no "start in this level" setting and no command-line flag for it, so
// the plan is to run reTHAWed's own level script from our plugin. Our level's ID,
// 0x9F014B98, is the checksum of the name "load_MPGTALSE" (src/common/convert.h), so
// that is the script to run.
//
// This is deliberately a TEST, built to answer one question and then get out of the
// way: once reTHAWed has reached its front end, run that script a few times, and log
// reTHAWed's state (menus / level, and the level ID) for a while afterwards, so the
// answer is in logs\thaw.log. It runs once per reTHAWed start.
//
// It calls reTHAWed's script runner on reTHAWed's GAME THREAD, the same way BMX mode
// runs its bike toggle (src/thaw/bmx.cpp): script functions touch game objects, so
// they must run on the game's own thread.

#pragma once

namespace mp::thaw {

// Our main thread, once at start: remember the log, and check the script runner's
// code is what we expect. Nothing happens if the check fails.
bool AutoStartCodeChecksPass(void (*log)(const char*, ...));

// reTHAWed's game thread (our controller hook); does its own timing. link is unused
// for now but kept for later (e.g. only when a player is actually playing).
void AutoStartGameThreadTick(void* link);

// 8.35 recorder: note a front-end button state change. Called from the controller
// hook while reTHAWed is the front window and the player is driving its menus with
// the pad, so the navigation can be captured once and replayed later.
void AutoStartNoteFrontButton(int gtaMask, bool down);

// 8.35: the buttons the front-end replay is holding right now (0 = not replaying).
int AutoStartSyntheticButtons();

// 8.35 road 3 (2026-10-13): what the level menu itself does - SetGameType freeskate,
// SetCurrentGameType, change_level level = load_MPGTALSE - once, 8 s after reTHAWed's
// front end is up. Checks its code first (main thread, after AutoStartCodeChecksPass).
bool AutoStartRoad3ChecksPass(void (*log)(const char*, ...));

// From reTHAWed's Present (end of every drawn frame; our device hook, drawlog.cpp).
// device = the IDirect3DDevice9. Does its own timing; runs only on the window's thread.
void AutoStartPresentTick(void* device);

} // namespace mp::thaw
