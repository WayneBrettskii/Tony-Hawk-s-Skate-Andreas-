// my-passthrough: reTHAWed's music follows GTA's radio choice (8.27 step R1).
//
// GTA writes what it wants into the link (reserved[1]: kMusicPlay / kMusicPause /
// kMusicLeave, reserved[2]: its tick). On reTHAWed's own game thread we then
// call reTHAWed's PauseMusic (0x004B4DE0) to pause or resume only its music;
// sound effects keep playing. We only resume music we paused ourselves.
// No word from GTA for 5 s (GTA closed) = music back to normal.

#pragma once

#include "../common/link.h"

namespace mp::thaw {

// Our main thread, once at start: code checks. Nothing happens if they fail.
bool MusicCodeChecksPass(void (*log)(const char*, ...));

// reTHAWed's game thread (from our controller hook); does its own timing.
void MusicGameThreadTick(LinkBlock* link);

// Our main thread, once per second: log what happened.
void MusicSecond(void (*log)(const char*, ...));

} // namespace mp::thaw
