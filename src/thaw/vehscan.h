// SkateAndreas: 8.51 skitching route A (docs/DESIGN.md 8.51).
//
// Step 2: once our GTA map has been loaded for 25 s, finds the map's reTHAWed vehicles
// (the map tool's --proxy-cars; their places are in _railpool\proxies.csv) in reTHAWed's
// memory on its own thread: every copy of each one's position (x, y, z within about 1 m,
// and 10 m in height), and a rotation (3 unit rows) right before or after a copy. Read only.
//
// Step 4 test: F2 turns follow mode on / off: car 0 is put where the nearest moving GTA car
// is (the one the GTA plugin publishes for skitching, link v17), turned the way its nose
// points, every pass. The writes are made on reTHAWed's game thread (VehGameThreadTick from
// the controller hook). F1 turns the car's nose by 90 degrees (which of its rows is the nose
// is not known yet). Turn test: Shift+F2 puts car 0 in front of the skater (follow mode off), and F1
// with follow mode off turns it a quarter turn where it stands. While follow mode is on, our own skitch (8.42) steps aside, so Up is
// reTHAWed's own skitch.

#pragma once

namespace mp { struct LinkBlock; }

namespace mp::thaw {

// Once per second from the plugin's main loop. inOurLevel = reTHAWed is in the GTA map.
void VehScanSecond(bool inOurLevel, void (*log)(const char*, ...));

// Every full pass. pos = the skater's position (nullptr = no skater).
// at / vel: the skater's facing row and velocity (Shift+F2 puts car 0 in front of him).
void VehTestTick(LinkBlock* link, bool inOurLevel, const float* pos, const float* at, const float* vel,
                 void (*log)(const char*, ...));

// reTHAWed's game thread (the controller hook): writes car 0 where VehTestTick put it.
void VehGameThreadTick();

// True while follow mode is on (our own skitch then stays off).
bool VehFollowOn();

// reTHAWed's game thread: the skater object (0 = none), for the skitch facing fix (8.51 step 8).
void VehSetSkater(unsigned long skater);

} // namespace mp::thaw
