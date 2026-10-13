// SkateAndreas: research step H1 for copying reTHAWed's HUD into GTA
// (docs/DESIGN.md 8.22). LOG ONLY: watches how reTHAWed draws one frame.
//
// reTHAWed keeps its Direct3D 9 device at 0x00852044 (set at 0x0052CDC0;
// it draws through DXVK, reTHAWed's own _d3d9.dll). F11 (any window) makes
// this code note every drawing call of the next whole frame (from one
// Present to the next): render target changes, clears, viewports, transforms,
// shaders, vertex formats and draws. Written to logs\thaw_frame_N.txt. From
// that we can see where the 3D world ends and the HUD starts, which the next
// step needs. The game draws exactly as before; nothing is changed except
// that its drawing calls pass through our small counting functions.
// Phase 6 H2: the same functions also send the HUD into our own picture
// (hud.h); that part does change where reTHAWed draws its HUD.

#pragma once

namespace mp::thaw {

// Once per pass of the plugin's loop. Installs the watch the first time the
// device exists (after the code check), and handles F11 (or, temporarily,
// touchpad + D-pad Up on the PS5 controller: padCombo).
// Phase 6 H2 (hud.h): hudAllowed = in a level and reTHAWed not in front;
// hudToggle = touchpad + D-pad Left (switches the HUD catching on/off).
// C1: leanToggle = touchpad + D-pad Right: reTHAWed "lean drawing" (its 3D world
// not drawn while it's in a level behind GTA; on by default) on / off.
void DrawLogTick(void (*log)(const char*, ...), bool padCombo, bool hudAllowed, bool hudToggle, bool leanToggle);

} // namespace mp::thaw
