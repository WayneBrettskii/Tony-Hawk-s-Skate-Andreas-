// SkateAndreas: F9 = log a "mark" (phase 4-0, lining up Los Santos East with
// GTA). Writes CJ's position (and the ground height under him) to gta.log as
// "mark #N". The THAW plugin does the same for the skater when F9 is pressed in
// reTHAWed. Putting both on the same landmarks gives the pairs of positions the
// formula is worked out from. Read only: nothing in the game changes.
// Only acts with GTA in front.

#pragma once

namespace mp::gta {

using LogFn = void (*)(const char* fmt, ...);

// Call once per GTA world update, on GTA's main thread. Never crashes the game.
void MarkFrameSafe(LogFn log);

} // namespace mp::gta
