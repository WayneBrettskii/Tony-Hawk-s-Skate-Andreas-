// my-passthrough: an even 60 frames a second for GTA (phase 5-3j,
// docs/DESIGN.md 8.20).
//
// GTA's own frame limiter (with FramerateVigilante at 60) lets frames come
// 14 and 19.5 ms apart in turn (frame recorder, 2026-10-09). Motion copied
// from reTHAWed then judders a little at speed. This pacer waits, once per
// frame, until exactly 1/60 s after the previous frame (high-resolution clock).
//
// It only works when GTA's own "Frame Limiter" (Options > Display Setup) is
// OFF; with it on, the pacer stands aside so the two don't fight.

#pragma once

#include "skatemode.h"

namespace mp::gta {

// Once per drawn frame (from main.cpp's drawing hook).
void PacerFrame(LogFn log);

} // namespace mp::gta
