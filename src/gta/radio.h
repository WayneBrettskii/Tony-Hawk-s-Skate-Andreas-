// my-passthrough: radio while skating (docs/DESIGN.md 8.27, step R1).
//
// Stations: reTHAWed's own soundtrack (played by reTHAWed), GTA's radio stations
// (played by GTA on foot), Off. Hold Back (Create / Share on PlayStation, View
// on Xbox) for 0.4 s to step to the next station; its name shows at the top of
// the screen. A quick tap of Back still reaches reTHAWed (on release).
// Only while skate mode is on; off = GTA radio stopped and reTHAWed's music
// paused (user 20:35: its music only plays in skate mode while GTA runs).

#pragma once

#include "skatemode.h"

namespace mp::gta {

// padlink.cpp, GTA's main thread, every controller frame before the pad goes
// to reTHAWed: the Back hold / tap filter.
void RadioFilterPad(LONG& buttons);

// GTA's main thread, once per world update.
void RadioFrame(LinkBlock* link, LogFn log);

// Once, from DllMain: hook GTA's HUD drawing (station name).
void RadioAddHook();

} // namespace mp::gta
