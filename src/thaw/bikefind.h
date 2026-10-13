// SkateAndreas: find reTHAWed's bike skeleton (docs/DESIGN.md 8.29 B8, B9).
// READ ONLY.
//
// B8's memory scan (test 13:45) found it: reTHAWed's bike (model veh_bmx) has
// its own 8-bone skeleton, same layout as the rider's, with the bones
// Bone_Bike_Frame, _Crank, _Pedal_R, _Pedal_L, _Front (fork + bars),
// _Tire_Front, _Tire_Rear (and an unnamed root). It sat 0x3FE0 bytes after the
// rider's skeleton. This finds it quickly: first through the rider's object's
// components, then in the memory around the rider's skeleton, by its
// Bone_Bike_Frame name checksum. Cached; checked again every call.

#pragma once

namespace mp::thaw {

// Name checksums of the bike's bones.
constexpr unsigned long kBikeBoneFrame = 0x48A09A4E;   // Bone_Bike_Frame
constexpr unsigned long kBikeBoneCrank = 0x4CD56BFA;   // Bone_Bike_Crank
constexpr unsigned long kBikeBonePedalR = 0x6A32916A;  // Bone_Bike_Pedal_R
constexpr unsigned long kBikeBonePedalL = 0x903DAC09;  // Bone_Bike_Pedal_L
constexpr unsigned long kBikeBoneFront = 0x03A3C475;   // Bone_Bike_Front (fork + bars)
constexpr unsigned long kBikeBoneTireFront = 0x85CB3B73;
constexpr unsigned long kBikeBoneTireRear = 0xE29BAFDA;

// The bike skeleton's address, or 0. riderSkeleton = the rider's skeleton,
// skater = the rider's object. Searches at most twice a second when not found.
unsigned long FindBikeSkeleton(unsigned long riderSkeleton, unsigned long skater, void (*log)(const char*, ...));

// Forget the cached address (rider got off the bike).
void ForgetBikeSkeleton();

} // namespace mp::thaw
