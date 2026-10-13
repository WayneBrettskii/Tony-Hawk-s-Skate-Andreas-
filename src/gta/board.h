// SkateAndreas: a skateboard under CJ's feet while skate mode is on
// (phase 5-1, docs/DESIGN.md 8.13).
//
// GTA SA has no skateboard model, so this is a simple stand-in board made by
// our own code: a deck with kicked nose and tail, two trucks, four wheels, in
// plain colours. It is not a GTA model file: the plugin draws it itself each
// frame with RenderWare's immediate mode, right after GTA draws the world, at
// the spot skate mode works out. Nothing is added to or changed in GTA's
// files. Later (phase 5-2) the shape can be swapped for the reTHAWed board.

#pragma once

#include "skatemode.h"

class CPlayerPed;

namespace mp::gta {

// How high the deck's top is above the ground (m). Skate mode lifts CJ by this
// much so his feet stand on the deck.
constexpr float kBoardLift = 0.112f;

// Once, from DllMain: hook GTA's drawing so the board is drawn each frame.
void BoardAddHook();

// Once per frame while skate mode moves CJ: where the board's wheels touch
// (GTA metres) and whether CJ is on the ground (board follows the slope) or
// in the air (board levels out). The direction comes from CJ's heading.
// Used when reTHAWed's board bone is not available.
void BoardUpdate(CPlayerPed* ped, float x, float y, float z, bool onGround, LogFn log);

// Phase 5-3b: the board placed exactly as reTHAWed's board bone says (flips,
// shove-its, ramps). GTA world: where the wheels touch (origin), and the
// board's right, forward (nose) and up (out of the deck) directions.
void BoardUpdatePose(const float origin[3], const float right[3], const float forward[3], const float up[3],
                     LogFn log);

// Skate mode went off: stop drawing the board.
void BoardHide(LogFn log);

} // namespace mp::gta
