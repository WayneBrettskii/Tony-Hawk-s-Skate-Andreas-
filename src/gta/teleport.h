// my-passthrough: test helper for the GTA plugin. F8 moves CJ to the waypoint
// marker placed on GTA's pause-menu map (user's request 2026-10-07).
//
// Only acts when F8 is pressed with GTA in front, CJ on foot, no cutscene /
// scripted scene / script control, GTA not paused, and skate mode off.
// Everything it does is logged.

#pragma once

namespace mp::gta {

using LogFn = void (*)(const char* fmt, ...);

// Call once per GTA world update (after CGame::Process), on GTA's main thread.
// Never crashes the game: errors are caught and logged.
void TeleportFrameSafe(LogFn log);

} // namespace mp::gta
