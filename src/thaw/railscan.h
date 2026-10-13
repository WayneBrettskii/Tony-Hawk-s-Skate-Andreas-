// SkateAndreas: 8.32 rails (docs/DESIGN.md 8.32).
//
// R3a: once our GTA map has been loaded for 15 s, finds reTHAWed's block of rail
// node records (one 0x60-byte record per node, see railscan.cpp) on its own
// thread, using a rail position from _railpool\rails_in_map.csv (the map tool
// writes that folder for whatever map it last built).
//
// R3b test: F5 in the level moves one far-away 2-node rail to just in front of
// the skater (6 m ahead, 25 m long, 50 cm up) (writes only that rail's two records: positions and boxes), then
// logs the skater's distance to it for 20 s, to show whether a moved rail can be
// ground. This is the first thing that writes to reTHAWed's rails.

#pragma once

namespace mp { struct LinkBlock; }

namespace mp::thaw {

// Once per second from the plugin's main loop. inOurLevel = reTHAWed is in the
// GTA map level (not menus, not another level).
void RailScanSecond(bool inOurLevel, void (*log)(const char*, ...));

// Every full pass. pos / vel = the skater's (nullptr = no skater), at = the
// skater's facing row (nullptr = unknown). logLine = this pass writes the
// 100 ms position line. link (may be nullptr): the test rail's ends go to GTA,
// which shows them as a row of lights.
void RailTestTick(LinkBlock* link, bool inOurLevel, const float* pos, const float* at, const float* vel,
                  bool logLine, void (*log)(const char*, ...));

// 8.32 R3c: the rail pool. On a pool map (DESIGN 8.32 R3c) the level's rails are
// parked spare rails; this copies the GTA rails near the skater into them, so the
// whole map can be ground with only a few thousand nodes loaded. pos / vel = the
// skater's THAW position and velocity (nullptr = no skater). Call every pass; it
// fills only when the skater has moved a little, and logs the moments the skater
// is on a placed rail (so grinds that stop - e.g. at a palm in a hedge - show).
void RailPoolTick(bool inOurLevel, const float* pos, const float* vel, void (*log)(const char*, ...));

// 8.33 C2: write the cars' roof rails (published by the GTA plugin over the link)
// into the pool's reserved slots, so reTHAWed's skater can grind a GTA car.
// pos = the skater's position: a reserved rail near him is never moved (a rail
// teleporting under him is what blows the grind up).
void CarRailTick(LinkBlock* link, bool inOurLevel, const float* pos, void (*log)(const char*, ...));

} // namespace mp::thaw
