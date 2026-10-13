// SkateAndreas: reTHAWed's rider gets on / off the BMX when CJ does
// (docs/DESIGN.md 8.29, step B1).
//
// GTA says in the link whether CJ is riding a BMX (gtaBikeWish). On reTHAWed's
// game thread we compare that with the rider's "on bike" byte (component
// 0x5BD63B29, +0x524) and, when they differ, run reTHAWed's own bike toggle
// script (0x27065DA5, the one its Vehicles menu uses). The byte goes back into
// the link (thawBikeState).

#pragma once

#include "../common/link.h"

namespace mp::thaw {

// Our main thread, once at start: code checks. Nothing happens if they fail.
bool BmxCodeChecksPass(void (*log)(const char*, ...));

// reTHAWed's game thread (from our controller hook); does its own timing.
void BmxGameThreadTick(LinkBlock* link);

// Our main thread, once per second: log what happened.
void BmxSecond(void (*log)(const char*, ...));

} // namespace mp::thaw
