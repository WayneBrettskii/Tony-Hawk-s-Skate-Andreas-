// my-passthrough: CJ's body copies the reTHAWed skater's body (phase 5-3d,
// docs/DESIGN.md 8.17).
//
// Each frame the THAW plugin sends 18 bone positions of the skater (hips,
// spine, neck, head, arms, legs). Right before GTA draws CJ, this code turns
// CJ's own bones so each limb points the same way as the skater's (thigh to
// knee, knee to ankle, shoulder to elbow, ...), and puts his hips where the
// skater's hips are. GTA's own animation still runs underneath; this only
// changes the drawn pose for that frame. Fingers, hands and face stay GTA's.
//
// Nothing is copied from reTHAWed's files; only the live bone positions.

#pragma once

#include "skatemode.h"

class CPed;

namespace mp::gta {

// Once, from DllMain: hook GTA's ped drawing.
void BodyAddHook();

// Once per frame while skate mode moves CJ: the skater's bones (18 x xyz in the
// skater's own space, THAW units), the skater's orientation (rows right, up,
// at), the level's scale and mirror, and the GTA spot of the skater's origin
// (where the board's wheels touch).
void BodySetTarget(const float* body, const float* orient, float scale, float ySign, float feetX, float feetY,
                   float feetZ);

// 5-3f: F10 switches copying on / off (to compare). Returns the new state.
bool BodyToggle();

// No body this frame (skate mode off, or reTHAWed didn't send it).
void BodyClear();

// For the once-per-second log: frames the body was copied since the last call
// (and samples skipped as half-updated),
// and a one-time description of CJ's skeleton (nullptr once it was logged).
unsigned BodyFramesAndReset(unsigned* skipped);
const char* BodyTakeInfo();

} // namespace mp::gta
