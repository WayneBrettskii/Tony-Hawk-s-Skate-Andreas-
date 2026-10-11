// my-passthrough: 8.32 R3b test (docs/DESIGN.md 8.32): show the F5 test rail.
//
// The THAW plugin moves one of reTHAWed's rails in front of the skater when F5
// is pressed and puts its two ends in the link. Rails are invisible in GTA, so
// while skate mode is on this draws a row of orange lights (GTA coronas) along
// it, to aim at. Temporary, for the rail pool tests.

#pragma once

#include "skatemode.h"

namespace mp::gta {

// GTA's main thread, once per world update.
void TestRailFrame(LinkBlock* link, LogFn log);

} // namespace mp::gta
