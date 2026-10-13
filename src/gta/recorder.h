// SkateAndreas: frame recorder for finding the shimmy / stutter (STATUS.md,
// docs/DESIGN.md 8.18).
//
// While skate mode is on, F9 (GTA in front) records the next 3 seconds: one row
// per GTA frame with what skate mode got from reTHAWed and where it put CJ, the
// board and his hips, plus when GTA drew him. Written to
// <logs>\gta_frames_N.csv when done. Recording only reads; it changes nothing.

#pragma once

#include "skatemode.h"

namespace mp::gta {

enum RecCol {
    REC_T_MS,                         // time of the game update (ms since recording started)
    REC_FRAME,                        // GTA's frame counter
    REC_SAMPLE, REC_AGE_MS, REC_AHEAD_MS, // reTHAWed sample number, its age; 5-3f: blend position between the two frames shown (0-100, -1 = newest frame as is)
    REC_THAW_X, REC_THAW_Y, REC_THAW_Z,   // skater position (THAW units)
    REC_VEL_X, REC_VEL_Y, REC_VEL_Z,      // skater speed (THAW units / s)
    REC_TGT_X, REC_TGT_Y, REC_TGT_Z,      // where skate mode wanted CJ (GTA m)
    REC_PED_X, REC_PED_Y, REC_PED_Z,      // where CJ ended up (after the tilt)
    REC_UP_X, REC_UP_Y, REC_UP_Z,         // CJ's up direction
    REC_BOARD_X, REC_BOARD_Y, REC_BOARD_Z, // board (wheels' contact)
    REC_HIPS_X, REC_HIPS_Y, REC_HIPS_Z,   // body: hips target
    REC_CHEST_X, REC_CHEST_Y, REC_CHEST_Z, // body: chest's left-to-right direction (target)
    REC_ARM_X, REC_ARM_Y, REC_ARM_Z,       // body: right elbow target minus right shoulder target
    REC_DRAW_MS,                      // when GTA drew CJ with the copied body (ms), -1 = not drawn
    REC_DRAW_FRAME,                   // GTA's frame counter at that moment
    REC_DRAW_HIPS_X, REC_DRAW_HIPS_Y, REC_DRAW_HIPS_Z, // hips as drawn
    REC_COLS
};

// Skate mode, once per frame while it is on: start a recording on F9 (or,
// temporarily, touchpad + D-pad Down on the PS5 controller: padCombo); begin this
// frame's row.
void RecorderFrameBegin(LogFn log, bool padCombo = false);

// True while recording.
bool RecorderOn();

// Set one value of the current row (ignored when not recording).
void RecSet(RecCol col, float value);
void RecSet3(RecCol first, float x, float y, float z);

// Milliseconds since the recording started (for REC_T_MS / REC_DRAW_MS).
float RecNowMs();

} // namespace mp::gta
