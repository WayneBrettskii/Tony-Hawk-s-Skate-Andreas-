// my-passthrough: BMX mode, GTA side (docs/DESIGN.md 8.29).
//
// B1: tells reTHAWed (link gtaBikeWish) whether CJ is riding a BMX, so
// reTHAWed's rider gets on / off its bike too.
// B2: before BMX mode starts, reTHAWed's rider is put where the GTA bike is.
// B3: BMX mode: the controller goes to reTHAWed (as in skate mode) and the GTA
// bike (with CJ on it) follows reTHAWed's rider. Off when CJ gets off, when
// GTA / a mission takes control, on F7 / L3 + R3, or when the rider gets off
// in reTHAWed (then GTA's normal BMX until the next ride or F7 / L3 + R3).

#pragma once

#include "skatemode.h"

namespace mp::gta {

// GTA's main thread, once per world update.
void BmxFrame(LinkBlock* link, LogFn log);

// Once, from DllMain: B4b, the GTA bike drawn turned like reTHAWed's bike.
void BmxAddHook();

} // namespace mp::gta
