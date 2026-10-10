// my-passthrough: research, LOG ONLY (docs/DESIGN.md 8.29 B6): which of the
// rider's "board" bones turn in BMX tricks (bar spins, crank flips, ...).
//
// On the bike, Bone_Board_Root is the bike's frame (B4b). The other four board
// bones (Board_Nose, Trucks_Nose, Board_Tail, Trucks_Tail) are probably the
// bars / fork, the wheels and the cranks. While the rider is on the bike we
// work out each one's turn relative to Board_Root, compared with the riding
// pose (taken 4 s after getting on), and log it when it is more than 20
// degrees. Parts turned all the time (wheels) are only in the per-second
// summary. Nothing in reTHAWed is changed.

#pragma once

namespace mp::thaw {

// Main thread, every pass with a skater. onBike = the link's thawBikeState says ON.
void BikePartsTick(unsigned long skater, bool onBike, void (*log)(const char*, ...));

// Main thread, once per second: summary (only while on the bike).
void BikePartsSecond(void (*log)(const char*, ...));

} // namespace mp::thaw
