// SkateAndreas: 8.33 GTA cars (grind / skitch). C1: log only (DESIGN 8.33).
// Lists the GTA cars near CJ while skate / BMX mode is on, so the grind and
// skitch steps can be built on real data. Read only.

#pragma once

namespace mp { struct LinkBlock; }

namespace mp::gta {

// Every frame, GTA main thread, while skate mode is on: publish the roof rails of
// the cars near CJ (8.33 C2), in THAW units, so reTHAWed's skater can grind them.
void CarTick(LinkBlock* link, void (*log)(const char*, ...));

// Once per second, GTA main thread.
void CarScanSecond(void (*log)(const char*, ...));

} // namespace mp::gta
