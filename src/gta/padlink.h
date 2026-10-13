// SkateAndreas: GTA's controller state into the link (phase 3-2).
//
// GTA reads the controller in CPad::UpdatePads (0x541DD0). The world update,
// CGame::Process (0x53BEE0), calls it first thing, at 0x53BEE6, before the pause
// menu (0x53BF44) and the rest of the world read the result. We point that one
// call at our function, which calls whatever was there before (GTA's own code or
// another mod's) and then copies pad 0 into the shared block for the THAW side.
//
// Phase 3-2: copies pad 0 into the link.
// Phase 3-4: while skate mode is on, then blanks pad 0 (all buttons up, sticks
// centred), so GTA itself ignores the controller: no pause menu on Start, no
// camera on the right stick. The controller still reaches reTHAWed (copied
// first). Keyboard keys GTA merges into the pad are blanked too.
//
// Installed on the first world update, not at load time, so any other mod that
// hooks the same call (GInput, for example) has already done so and we chain
// after it. The call's 5 bytes are logged before and after.

#pragma once

#include "../common/link.h"

namespace mp::gta {

using PadLogFn = void (*)(const char* fmt, ...);

// GTA's main thread, once per world update (from the gameProcessEvent hook).
// Installs the redirect the first time.
void PadLinkFrame(LinkBlock* link, PadLogFn log);

// The buttons of the last controller frame, before any blanking (false if the
// pad hook isn't running). Used for the L3 + R3 toggle.
bool PadLinkRawButtons(LONG& buttons);

// Our logging thread, once per second: summary of what was published, and a
// check that the redirect is still ours.
void PadLinkSecond(PadLogFn log);

} // namespace mp::gta
