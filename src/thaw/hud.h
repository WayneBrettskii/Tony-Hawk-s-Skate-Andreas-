// SkateAndreas: phase 6 H2 (docs/DESIGN.md 8.23). Catches reTHAWed's HUD in
// a see-through picture of our own instead of reTHAWed's screen, so it can
// later be shown in GTA (H3, H4: not done yet).
//
// The HUD is the part of a frame from the first DrawIndexedPrimitiveUP (only
// the HUD uses it, see the 8.23 log result) to the game's next render target
// change / EndScene / Present. Only while reTHAWed is in a level and not the
// front window. Called from drawlog.cpp's wrapped device functions (render
// thread) and from the plugin's loop (HudTick).

#pragma once

#include <windows.h>
#include <d3d9.h>

namespace mp::thaw {

// drawlog.cpp, once: the table of the device's original functions.
void HudInit(void* const* orig);

// drawlog.cpp, render thread:
// (the three return true when the HUD picture just started / ended, for the frame log)
bool HudBeforeSetRT(IDirect3DDevice9* dev, DWORD index); // game changes the target
bool HudBeforeIdxUP(IDirect3DDevice9* dev);              // a DrawIndexedPrimitiveUP
bool HudEnd(IDirect3DDevice9* dev);                      // EndScene / Present
void HudNewFrame();                                      // Present (after HudEnd)
bool HudFilterRS(D3DRENDERSTATETYPE s, DWORD v);         // true = keep it from the device for now
void HudNoteViewport(const D3DVIEWPORT9* v);             // game set a viewport
void HudBeforeReset();                                   // device reset: let go of our picture
void HudRequestShot();                                   // save the next caught HUD (with F11)

// Plugin loop: allowed = in a level and reTHAWed not in front; toggle =
// touchpad + D-pad Left (switch on/off). Writes the test picture and logs.
void HudTick(void (*log)(const char*, ...), bool allowed, bool toggle);

} // namespace mp::thaw
