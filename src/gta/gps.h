// my-passthrough: GPS route to the map waypoint, like GTA V (docs/DESIGN.md 8.31 D5).
//
// Reads GTA's car roads from <GTA>\data\Paths\NODES*.DAT (read only, once),
// finds the shortest route along them from CJ to the waypoint set on the pause
// menu map, and draws it as a purple line on the radar and on the pause menu
// map. Reaching the waypoint (within 15 m) removes it.

#pragma once

namespace mp::gta {

using LogFn = void (*)(const char* fmt, ...);

// Once, from DllMain (AddHooks): the drawing hook on GTA's radar / map.
void GpsAddHook(LogFn log);

// GTA's main thread, once per world update.
void GpsFrameSafe(LogFn log);

} // namespace mp::gta
