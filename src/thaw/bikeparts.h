// SkateAndreas: reTHAWed's bike for GTA (docs/DESIGN.md 8.29 B7, B9).
// READ ONLY inside reTHAWed.
//
// B9: from reTHAWed's own bike skeleton (bikefind.h), not the rider's "board"
// bones (B8: bar spins and tailwhips are only in the bike's skeleton).
//
// While the rider is on the bike, every pass:
//   - the bike frame in the rider's space, in the board bone's convention
//     (right, towards the back, up; position): from Bone_Bike_Frame, lined up
//     once with the rider's Bone_Board_Root in the riding pose (4 s after
//     getting on), so GTA's bike picture code stays as it is. Tailwhips turn it.
//   - the fork's turn since the riding pose (Bone_Bike_Front: bar spins),
//   - the crank's direction (axle -> pedal on the bike's right, from the pedal
//     bones: pedalling, crank flips),
//   - for each side, how much the rider's hand is on the grip and the foot on
//     the pedal (compared with the riding pose),
// all but the first in the bike frame's space, for the link (thawBike).

#pragma once

#include "../common/link.h"

namespace mp::thaw {

// Main thread, every pass with a skater. onBike = the link's thawBikeState says
// ON; riderSkeleton = the rider's skeleton (pose). Fills out[kLinkBikeFloats]
// and returns true once the riding pose is known. When board12 gets the bike
// frame (rows right, up, at, position, rider's space), *boardValid = true.
bool BikePartsTick(unsigned long skater, unsigned long riderSkeleton, bool onBike, void (*log)(const char*, ...),
                   float* out, float* board12, bool* boardValid);

// 8.29 B12: while the rider is OFF the bike: reTHAWed's bike lying in the world
// (to put the GTA bike there). out12 = the bike frame in THAW's world, in the
// board convention (rows right, up = towards the back, at = up, position).
// orient16 / pos3 = the rider's orientation (rows) and position. True only once
// the bike is known to stay in the world (the rider moved 10+ units away and it
// stayed put), not carried along with the rider.
bool DroppedBikeTick(unsigned long skater, unsigned long riderSkeleton, bool onBike, const float* orient16,
                     const float* pos3, void (*log)(const char*, ...), float* out12);

// Main thread, once per second: summary (only while on the bike).
void BikePartsSecond(void (*log)(const char*, ...));

} // namespace mp::thaw
