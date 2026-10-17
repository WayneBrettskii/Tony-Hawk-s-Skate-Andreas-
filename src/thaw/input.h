// SkateAndreas: reTHAWed takes its controller input from GTA (phase 3-3).
//
// reTHAWed (rthw.dll, following partymod's input.c) reads the controller only by
// asking SDL2.dll two questions, over and over:
//   SDL_GameControllerGetButton(controller, button)  -> 0 / 1
//   SDL_GameControllerGetAxis(controller, axis)      -> -32768 .. 32767
// rthw.dll finds both through its import table. We replace those two entries
// with ours (in memory only; nothing on disk), which answer from one of three
// sources, chosen about every 15 ms by our main loop:
//   - REAL:    reTHAWed is the front window: ask SDL as before (menus etc. work).
//   - GTA:     reTHAWed is behind, skate mode is on in GTA and GTA's controller
//              frame is fresh (under 100 ms old): answer from the link.
//   - NOTHING: otherwise: nothing pressed, sticks centred, so the skater doesn't
//              move while you play GTA.
// Every change of source is logged with the reason; once per second, how many
// questions each source answered.
//
// GTA's pad uses PlayStation names (GInput maps the Xbox pad onto them):
// Cross = A, Circle = B, Square = X, Triangle = Y, L1/R1 = shoulders,
// L3/R3 = stick clicks, Start, Select = Back, D-pad. Sticks: GTA about
// -128..128 -> SDL x256; L2/R2: GTA 0..255 -> SDL 0..32767.

#pragma once

#include "../common/link.h"

namespace mp::thaw {

using InputLogFn = void (*)(const char* fmt, ...);

// Our main loop, every pass. Installs the two import entries the first time
// (once rthw.dll is there) and picks the source.
void InputTick(LinkBlock* link, InputLogFn log);

// Once per second: counts per source.
void InputSecond(InputLogFn log);

// 8.54: how many times reTHAWed has asked our input hook (buttons and axes, any source) so far.
LONG InputCallTotal();

// Pure conversions (also used by the unit test).
short GtaStickToSdl(short gta);
short GtaTriggerToSdl(short gta);
// SDL button number -> our kBtn* bit (0 if GTA has no such button).
long SdlButtonToGtaBit(int sdlButton);

} // namespace mp::thaw
