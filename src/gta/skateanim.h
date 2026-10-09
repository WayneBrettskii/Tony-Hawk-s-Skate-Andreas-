// my-passthrough: CJ's skating poses while skate mode is on (phase 5-0,
// docs/DESIGN.md 8.13).
//
// GTA has no skateboard and no skateboard animations, but it has the
// roller-skating animations its roller-skating pedestrians use (animation
// file "skate": skate_idle, skate_run, skate_sprint). While skate mode is on,
// CJ plays one of those, picked from the skater's speed, and GTA's own
// "jump glide" pose while the skater is in the air. Nothing is copied from
// reTHAWed; GTA's own files are only read by GTA itself.
//
// Off (or skate mode off) = CJ's normal animations; the animation file is
// released again.

#pragma once

#include "skatemode.h"

class CPlayerPed;

namespace mp::gta {

// Once per frame while skate mode moves CJ. speed: skater's speed along the
// ground in m/s; height: how far CJ's feet are above GTA's ground (m).
void SkateAnimUpdate(CPlayerPed* ped, float speed, float height, LogFn log);

// Skate mode went off: back to CJ's normal animations.
void SkateAnimStop(CPlayerPed* ped, LogFn log);

} // namespace mp::gta
