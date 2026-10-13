// SkateAndreas: phase 6 H4 (docs/DESIGN.md 8.25). Shows reTHAWed's HUD
// picture (shared by the THAW plugin, src/common/hudlink.h) over GTA's screen,
// right after GTA draws its own HUD, while skate mode is on.

#pragma once

#include "skatemode.h"

namespace mp::gta {

// Once, from DllMain: hook GTA's HUD drawing.
void HudShowAddHook(LogFn log);

} // namespace mp::gta
