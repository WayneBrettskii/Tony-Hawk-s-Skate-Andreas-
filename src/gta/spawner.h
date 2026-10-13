// SkateAndreas: vehicle spawner (docs/DESIGN.md 8.28 d).
//
// F6 (GTA in front, skate mode off) opens a list of all GTA vehicles, BMX at
// the top, then A-Z. With the controller: D-pad / left stick up-down to move,
// Cross to spawn in front of CJ (GTA's own vehicle-cheat code), Circle or F6 to
// close. The keyboard's arrow keys and Enter work too. GTA ignores the
// controller while the list is open. Trains are left out (they can't be placed
// off the tracks).

#pragma once

#include "skatemode.h"

namespace mp::gta {

// GTA's main thread, once per world update: F6, and the spawn itself.
void SpawnerFrame(LogFn log);

// padlink.cpp, every controller frame (before GTA uses it): reads the list's
// buttons; returns true when GTA must not get the controller this frame.
bool SpawnerPadFilter();

// Once, from DllMain: hook GTA's HUD drawing (the list).
void SpawnerAddHook();

} // namespace mp::gta
