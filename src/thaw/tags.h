// my-passthrough: watches reTHAWed's graffiti tags (docs/DESIGN.md 8.28 a, step T1).
//
// Two small hooks in reTHAWed:
//   - the graffiti component's per-frame update (vtable slot 0x006EE3C8): we
//     call the original, then note whether the skater is spraying right now
//     (component byte +0x1C);
//   - the call that puts a finished tag in the world (0x005F92E0): we count it
//     and note wall or ground, then call the original.
// The result goes into the link (reserved[5], kTag* bits) for GTA, which puts a
// Grove Street tag on the wall in front of CJ. Nothing else in reTHAWed changes.

#pragma once

#include "../common/link.h"

namespace mp::thaw {

// Our main thread, once at start: code checks, then the two hooks. Nothing is
// changed if a check fails.
bool TagsStart(LinkBlock* link, void (*log)(const char*, ...));

// Our main thread, once per second: log what happened.
void TagsSecond(void (*log)(const char*, ...));

} // namespace mp::thaw
