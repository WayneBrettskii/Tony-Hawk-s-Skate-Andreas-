// SkateAndreas: keep reTHAWed running when its window isn't in front (phase 3-1).
//
// Found in phase 3-0: reTHAWed pauses itself the moment it loses focus.
// Attempt 1 (docs/DESIGN.md section 8.7): wrap reTHAWed's window procedure and
// swallow the "you lost focus" messages, so neither THAW nor SDL ever hears
// about it (the SkyCraft trick). Messages that say "you got focus" pass as
// normal.
//
// Changes reTHAWed's window procedure (in memory only; nothing on disk). Our
// procedure stays installed until reTHAWed closes; this plugin is never unloaded
// before that, so it is never left pointing at missing code.
//
// Also reads (read only) THAW's two focus flags, set by its WM_ACTIVATE handler
// at 0x00543D51 (code checked first):
//   0x008A35D5 = window focused (partymod: isFocused / isFocused_again)
//   0x0072E850 = cleared when focus is lost (used by the drawing code)

#pragma once

namespace mp::thaw {

using FocusLogFn = void (*)(const char* fmt, ...);

// Call often (every loop pass). Finds reTHAWed's window once it exists and
// installs our procedure; checks that it is still installed; logs changes of
// the focus flags and how many messages were swallowed.
void FocusKeeperTick(FocusLogFn log);

// Once per second: log the swallowed-message counts if they changed.
void FocusKeeperSecond(FocusLogFn log);

} // namespace mp::thaw
