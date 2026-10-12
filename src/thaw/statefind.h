// my-passthrough: 8.32 research, LOG ONLY (docs/DESIGN.md). Find reTHAWed's camera
// object: the two earlier dumps showed it is NOT a field of the skater, so here we
// scan reTHAWed's writable memory for float triples (positions) near the skater.
// Press F3 once (stores the candidates), rotate the camera, press F3 again: the
// candidates that MOVED are logged - the camera object's position is among them.
// Read only.

#pragma once

namespace mp::thaw {

// Each pass. skater = the local skater object (0 = none). log = the plugin log.
void StateFindTick(unsigned long skater, void (*log)(const char*, ...));

} // namespace mp::thaw
