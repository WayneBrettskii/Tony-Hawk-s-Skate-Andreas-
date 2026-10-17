// SkateAndreas: shooting while skating (DESIGN 8.53). Step 1: in skate mode, log once a second
// what CJ holds; F12 fires one test shot along the camera. Step 2: shoot mode (D-pad Left + X):
// R1 locks on, B fires CJ's gun at its own rate, through GTA's own CWeapon::Fire.

#pragma once

namespace mp::gta {

using LogFn = void (*)(const char* fmt, ...);

// Call once per GTA world update, on GTA's main thread, after skate mode has placed CJ.
// Never crashes the game.
void ShootFrameSafe(LogFn log);

// 8.53 step 2: in GTA's pad update, right after the radio wheel's filter, before reTHAWed gets the
// buttons: the Left + X toggle, and R1 / B kept from reTHAWed while shoot mode is on.
void ShootFilterPad(LONG& buttons, short* axes);

// The lock-on marker (a HUD draw hook). Call once at start.
void ShootAddHook();

} // namespace mp::gta
