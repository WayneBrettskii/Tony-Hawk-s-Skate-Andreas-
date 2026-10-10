// my-passthrough: Grove Street tags in GTA for reTHAWed's graffiti tags
// (docs/DESIGN.md 8.28 a, step T1).
//
// When the THAW side reports a finished tag (link reserved[5]) while skate mode
// is on, a Grove Street tag (GTA's own collectible tag model 1490 "tag_01",
// shown fully sprayed) is put on the GTA wall in front of CJ, or on the ground
// under him when there's no wall. Up to 50; the oldest goes when a 51st comes.
// Not saved; gone when GTA closes or a game is loaded.
//
// GTA would crash on a tag object that isn't one of its 100 collectibles, so
// two of GTA's calls go through us: for GTA's own tags they do exactly what
// GTA did; for ours they show the tag fully sprayed.

#pragma once

#include "skatemode.h"

namespace mp::gta {

// Once, from DllMain: the two call redirects (only if the calls are GTA's original).
void TagsAddHook();

// GTA's main thread, once per world update.
void TagsFrame(LinkBlock* link, LogFn log);

} // namespace mp::gta
