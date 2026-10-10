// my-passthrough: research, LOG ONLY (docs/DESIGN.md 8.29 B8): find reTHAWed's
// bike skeleton.
//
// B7 copied the rider's "board" bones, but in bar spins they don't turn
// (test 13:28: never more than 20 degrees). reTHAWed's bike is its own skinned
// model (data\models\Veh\veh_BMX\veh_bmx.skin), so it has its own skeleton and
// animations. This finds every skeleton in reTHAWed's memory once the rider is
// on the bike (same layout as the rider's: bone count +0x14, bone list +0x18,
// matrices +0x04), logs them, then follows the ones that aren't the rider's and
// logs which of their bones turn (bar spins, crank flips, tailwhips).
// Nothing in reTHAWed is changed.

#pragma once

namespace mp::thaw {

// Main thread, every pass. onBike from the link; riderSkeleton = the rider's
// skeleton address (to tell it apart), 0 if unknown.
void BikeFindTick(bool onBike, unsigned long riderSkeleton, void (*log)(const char*, ...));

// Main thread, once per second.
void BikeFindSecond(void (*log)(const char*, ...));

} // namespace mp::thaw
