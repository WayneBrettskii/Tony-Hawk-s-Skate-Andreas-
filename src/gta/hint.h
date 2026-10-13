// SkateAndreas: the on-screen hints, in GTA's own black message box (DESIGN 8.37).
//
// Shows how to start each mode, using the same black box GTA uses for its own tips:
//   on foot, no mode on   -> "Press L3 + R3 to Skate"
//   on a BMX, no mode on  -> "Press L3 + R3 to toggle BMX mode"
// It never overwrites a message GTA itself is showing, and it comes back at most
// once every kHintRepeatMs, so it is a reminder rather than a nuisance.

#pragma once

#include "skatemode.h"

namespace mp::gta {

// GTA's main thread, once per world update.
void HintFrame(LogFn log);

} // namespace mp::gta
