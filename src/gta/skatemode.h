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

// 8.29 B10: switch skate mode on as soon as GTA allows it (CJ off the bike,
// no script holding him), trying for `forMs` milliseconds. BMX mode calls this
// when the rider gets off the bike in reTHAWed (L1 + R1). GTA's main thread.
void SkateModeRequestOn(const char* why, unsigned long forMs);

// 8.29 B11: switch skate mode off right now (BMX mode takes over: reTHAWed's
// rider got on his bike with Square). Also drops a pending request.
void SkateModeTurnOffNow(LinkBlock* link, LogFn log, const char* why);

// 8.33 C5: after a car hit turned skate mode off, ask for it again once CJ has had
// time to get up. Call once a second (GTA main thread).
void SkateModeBailTick();

// 8.29 B3: true while BMX mode is on (bmxmode.cpp).
bool BmxModeIsOn();

// Skate mode or BMX mode: reTHAWed has the controller (radio wheel, HUD, pad).
inline bool RideModeIsOn() { return SkateModeIsOn() || BmxModeIsOn(); }

// 8.28 a: a reTHAWed point (THAW units) and direction in GTA terms, with the
// same mapping CJ uses right now. False when skate mode is off.
bool SkateModeThawToGta(const float thawPos[3], const float thawDir[3], float gtaPos[3], float gtaDir[3]);

} // namespace mp::gta
