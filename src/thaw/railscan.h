// my-passthrough: 8.32 rails (docs/DESIGN.md 8.32).
//
// R3a: once our GTA map has been loaded for 15 s, finds reTHAWed's block of rail
// node records (one 0x60-byte record per node, see railscan.cpp) on its own
// thread, using a rail position from rails_in_map.csv (written by the map tool).
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

} // namespace mp::thaw
