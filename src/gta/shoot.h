// SkateAndreas: shooting while skating (DESIGN 8.53). Step 1, a probe: in skate mode, log
// once a second what CJ holds; F12 fires ONE shot of that gun from CJ's right hand along the
// camera, through GTA's own CWeapon::Fire, and logs what happened. Only acts with GTA in front.

#pragma once

namespace mp::gta {

using LogFn = void (*)(const char* fmt, ...);

// Call once per GTA world update, on GTA's main thread, after skate mode has placed CJ.
// Never crashes the game.
void ShootFrameSafe(LogFn log);

} // namespace mp::gta
