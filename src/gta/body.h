// SkateAndreas: CJ's body copies the reTHAWed skater's body (phase 5-3d,
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

// 8.29 B7 (BMX): after copying the body, bend CJ's arms / legs so his hands
// and feet land on these GTA world spots: [0] grip on the bike's right, [1]
// grip on its left, [2] right pedal, [3] left pedal; weight 0..1 = how much
// (reTHAWed's rider's hand / foot on it). Each frame while wanted.
void BodySetLimbTargets(const float (*pos)[3], const float* weight);
void BodyClearLimbTargets();
// For the log: limbs bent onto a target since the last call.
unsigned BodyReachedAndReset();

// 8.53 step 3 (gun mode): after copying the body, turn CJ's chest towards this GTA world point (at most
// 70 degrees), stretch his right arm at it and, for two-handed guns, put his left hand under the barrel;
// weight 0..1 eases it in and out. Each frame while aiming; BodyClearAim when done.
// Minigun uses a low carry with bent arms instead of the standard extended-arm pose.
void BodySetAim(const float* point, float weight, bool twoHanded, bool minigun);
void BodyClearAim();
// For the log: frames drawn with the aim pose since the last call, how many of them had the point
// further round than the chest limit, the last chest turn used (degrees), and in how many the arm's
// direction was pulled back in front of the chest (test 48).
unsigned BodyAimFramesAndReset(unsigned* beyond, float* chestDeg, unsigned* armClamped);
// test 46: one detail line about the first aim frame after each BodyClearAim (nullptr when none waits).
const char* BodyTakeAimInfo();
// Test 50: worst gun-hand basis sample since last call, separate from the chest detail.
const char* BodyTakeGunInfo(unsigned* samples);

// No body this frame (skate mode off, or reTHAWed didn't send it). Also
// clears the limb targets.
void BodyClear();

// For the once-per-second log: frames the body was copied since the last call
// (and samples skipped as half-updated),
// and a one-time description of CJ's skeleton (nullptr once it was logged).
unsigned BodyFramesAndReset(unsigned* skipped);
const char* BodyTakeInfo();

} // namespace mp::gta
