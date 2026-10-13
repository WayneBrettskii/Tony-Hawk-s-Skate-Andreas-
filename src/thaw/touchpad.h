// SkateAndreas: TEMPORARY test shortcut (user plays on a TV, 2026-10-09).
// Remove before release (docs/DESIGN.md 11).
//
// Reads the PS5 controller (DualSense) directly, only to see the touchpad
// click and the D-pad, so touchpad + D-pad Up can start the F11 draw log here
// and touchpad + D-pad Down the F9 recording in GTA. GTA's pad (Xbox buttons
// through DualSenseX) has no touchpad, which is why this reads the controller
// itself. Read only: nothing is sent to the controller. The result goes into
// the link (reserved[0], kPadRaw* bits) for the GTA side.

#pragma once

#include "../common/link.h"

namespace mp::thaw {

// Once, after the link is set up (link may be null): starts a small thread that
// looks for the controller every 2 seconds and reads it while connected.
void TouchpadStart(LinkBlock* link);

// Once per pass of the main loop: logs changes (found / lost / could not open)
// and returns the latest kPadRaw* bits.
LONG TouchpadTick(void (*log)(const char*, ...));

// From DllMain on unload: stop the thread and clear the bits in the link.
void TouchpadStop();

} // namespace mp::thaw
