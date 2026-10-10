// my-passthrough: skate mode for the GTA plugin (phase 2-2, docs/DESIGN.md 8.6).
//
// While skate mode is on, CJ is moved to follow the reTHAWed skater (position
// and facing), his speed is zeroed, and one control flag of our own is set so
// he ignores the player's buttons. Off = GTA completely normal.
// Toggle: F7 or L3 + R3, only while GTA is the front window.

#pragma once

#include "../common/link.h"

namespace mp::gta {

using LogFn = void (*)(const char* fmt, ...);

// Call once per GTA world update (after CGame::Process), on GTA's main thread.
// Never throws or crashes the game: any error inside turns skate mode off and
// is logged.
void SkateModeFrameSafe(LinkBlock* link, LogFn log);

// True while skate mode is on (used by the F8 teleport helper to refuse).
bool SkateModeIsOn();

// 8.28 a: a reTHAWed point (THAW units) and direction in GTA terms, with the
// same mapping CJ uses right now. False when skate mode is off.
bool SkateModeThawToGta(const float thawPos[3], const float thawDir[3], float gtaPos[3], float gtaDir[3]);

} // namespace mp::gta
