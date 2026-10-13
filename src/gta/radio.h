// SkateAndreas: radio while skating (docs/DESIGN.md 8.27, step R1).
//
// Stations: reTHAWed's own soundtrack (played by reTHAWed), GTA's radio stations
// (played by GTA on foot), Off. Hold Back (Create / Share on PlayStation, View
// on Xbox) for 0.4 s: a GTA V-style wheel of station logos opens (GTA's own
// logos from models\fronten1.txd, reTHAWed's from its install); point the right
// stick, let go of Back to choose. A quick tap of Back still reaches reTHAWed
// (on release).
// Only while skate mode is on; off = GTA radio stopped and reTHAWed's music
// paused (user 20:35: its music only plays in skate mode while GTA runs).

#pragma once

#include "skatemode.h"

namespace mp::gta {

// padlink.cpp, GTA's main thread, every controller frame before the pad goes
// to reTHAWed: the Back hold / tap filter and the wheel's right stick (axes in
// kAxis* order; the right stick is zeroed for reTHAWed while the wheel is open).
void RadioFilterPad(LONG& buttons, short* axes);

// 8.32: is the radio wheel open? (padlink keeps the right stick for GTA's camera
// except while the wheel is open, when the stick picks the station.)
bool RadioWheelIsOpen();

// GTA's main thread, once per world update.
void RadioFrame(LinkBlock* link, LogFn log);

// Once, from DllMain: hook GTA's HUD drawing (station name).
void RadioAddHook();

} // namespace mp::gta
