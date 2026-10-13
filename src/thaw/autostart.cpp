// SkateAndreas 8.35 (test): start our map from reTHAWed's front end. See autostart.h
// and docs/DESIGN.md 8.35.

#include <windows.h>

#include "autostart.h"
#include "skater.h"
#include "../common/link.h"

namespace mp::thaw {
namespace {

// reTHAWed's script runner (cdecl: script, params, object, 0, 0, 0). The same one
// BMX mode uses (src/thaw/bmx.cpp); its first bytes are checked before we call it.
constexpr unsigned long kRunScript = 0x004135E0;
constexpr unsigned long kLoadOurLevel = 0x9F014B98ul; // checksum of "load_MPGTALSE"

using RunScriptFn = int(__cdecl*)(unsigned long, void*, void*, DWORD, DWORD, DWORD);

// 8.35: the one-script load does not work (see AutoStartGameThreadTick). Kept here
// so it can be switched back on to re-test if reTHAWed changes.
constexpr bool kTryTheLoad = false;

// ---- 8.35: the front-end replay ---------------------------------------------
// The sequence the player actually presses to reach free skate on our map, captured
// from their own run on 2026-10-12 (logs\thaw.log, `frontend:` lines). Times are
// milliseconds from the first press; each line is one press or one release, and the
// last Cross is held - that is the "go" that starts the load.
struct FeStep {
    unsigned int atMs;
    int bit; // a kBtn* mask
    bool down;
};
constexpr FeStep kFrontEndMacro[] = {
    {0, kBtnDown, true},     {160, kBtnDown, false},   {480, kBtnDown, true},    {620, kBtnDown, false},
    {980, kBtnDown, true},   {1110, kBtnDown, false},  {2310, kBtnCross, true},  {2450, kBtnCross, false},
    {4000, kBtnCross, true}, {4140, kBtnCross, false}, {5110, kBtnCross, true},  {5250, kBtnCross, false},
    {6090, kBtnRight, true}, {6170, kBtnRight, false}, {6300, kBtnRight, true},  {6410, kBtnRight, false},
    {6520, kBtnRight, true}, {6590, kBtnRight, false}, {7590, kBtnDown, true},   {7700, kBtnDown, false},
    {7840, kBtnDown, true},  {7950, kBtnDown, false},  {8920, kBtnCross, true},  {10380, kBtnCross, false},
};
constexpr int kFrontEndMacroCount = sizeof(kFrontEndMacro) / sizeof(kFrontEndMacro[0]);
// 8.35: OFF after the 2026-10-12 runs. The replay cannot start itself: our synthetic
// presses only reach reTHAWed while it is calling GetButton, and reTHAWed's front end
// only polls the pad once the player touches it (the hook ran for 2 s in a 69 s run,
// and only after the player pressed something). Chicken and egg - see DESIGN 8.35.
constexpr bool kReplayFrontEnd = false;
// The navigation recorder (logs what the player presses on the front end). Off too,
// so it does not fill the log during normal play.
constexpr bool kRecordFrontEnd = false;
constexpr ULONGLONG kReplaySettleMs = 2500;   // after the front end first shows
constexpr ULONGLONG kReplayTimeoutMs = 180000; // never start after this long

void (*g_log)(const char*, ...) = nullptr;
volatile LONG g_ok = 0;

// Timing. Nothing happens for a while after start: reTHAWed still has its own
// start-up to get through before it will accept a level load.
//
// 8.35 (2026-10-12): this is set long on purpose for the comparison run - a manual
// free-skate load has to happen first, so we can see which level ID the FRONT END
// sets (the one the plugin recognises) against the one our script call leaves
// behind. If a level is already loaded when the wait is over, nothing is done.
constexpr ULONGLONG kStartDelayMs = 90000;  // wait this long after we load
constexpr ULONGLONG kGiveUpMs = 90000;      // never saw the front end: stop
constexpr ULONGLONG kRetryMs = 2500;        // between attempts
constexpr int kMaxAttempts = 3;
constexpr ULONGLONG kObserveMs = 15000;     // watch the state this long afterwards

int g_phase = 0; // 0 = waiting, 1 = trying, 2 = watching, 3 = finished
int g_attempts = 0;
ULONGLONG g_bootTick = 0;     // when we loaded
ULONGLONG g_nextAction = 0;
ULONGLONG g_observeUntil = 0;
ULONGLONG g_lastWatchLog = 0;
ULONGLONG g_triedAt = 0;

// The replay's own state.
int g_synth = 0;             // buttons the macro is holding right now
bool g_synthRun = false;     // the macro is playing
bool g_synthDone = false;    // played once; do not play again
int g_synthNext = 0;         // next step in kFrontEndMacro
ULONGLONG g_synthStart = 0;
ULONGLONG g_feSeen = 0;      // when the front end was first seen (settle timer)
ULONGLONG g_lastReplayCheck = 0;
bool g_userDrove = false;    // the player has pressed something in the front end

} // namespace

bool AutoStartCodeChecksPass(void (*log)(const char*, ...))
{
    g_log = log;
    static const unsigned char expect[8] = {0x55, 0x8B, 0x6C, 0x24, 0x08, 0x56, 0x55, 0xE8};
    __try {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(kRunScript);
        for (int i = 0; i < 8; ++i) {
            if (p[i] != expect[i]) {
                log("autostart: RunScript (0x%08lX) is not as expected at byte %d; the Play test is off",
                    kRunScript, i);
                return false;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("autostart: could not read RunScript (0x%08lX); the Play test is off", kRunScript);
        return false;
    }
    InterlockedExchange(&g_ok, 1);
    log("autostart: RunScript checked; on reTHAWed's front end we will try to load our map "
        "(script 0x%08lX, load_MPGTALSE)",
        kLoadOurLevel);
    return true;
}

void AutoStartGameThreadTick(void*)
{
    if (g_ok != 1) return;

    if (g_bootTick == 0) g_bootTick = GetTickCount64();

    // 8.35: the front-end replay (what the player's own presses do, played back).
    if (kReplayFrontEnd) {
        const ULONGLONG now = GetTickCount64();
        if (now - g_lastReplayCheck >= 40) {
            g_lastReplayCheck = now;
            const GameState st = ReadGameState();
            if (st.readOk) {
                // 8.35 diagnostic: once a second, say what the replay is waiting for.
                static ULONGLONG lastDiag = 0;
                if (now - lastDiag >= 1000) {
                    lastDiag = now;
                    g_log("frontend replay: inFrontend=%d settleSeen=%s userDrove=%d done=%d running=%d synth=0x%X",
                          st.inFrontend, g_feSeen ? "yes" : "no", g_userDrove ? 1 : 0, g_synthDone ? 1 : 0,
                          g_synthRun ? 1 : 0, g_synth);
                }
                if (st.inFrontend != 1) {
                    if (g_synthRun) {
                        g_log("frontend replay: done - reTHAWed left the front end (level 0x%08X)", st.levelId);
                        g_synthRun = false;
                        g_synth = 0;
                    }
                    g_feSeen = 0;
                } else if (g_synthRun) {
                    while (g_synthNext < kFrontEndMacroCount &&
                           now - g_synthStart >= kFrontEndMacro[g_synthNext].atMs) {
                        const FeStep& s = kFrontEndMacro[g_synthNext++];
                        if (s.down)
                            g_synth |= s.bit;
                        else
                            g_synth &= ~s.bit;
                    }
                    if (g_synthNext >= kFrontEndMacroCount && g_synth == 0) {
                        g_log("frontend replay: all %d presses sent", kFrontEndMacroCount);
                        g_synthRun = false;
                        g_synthDone = true;
                    }
                } else if (!g_synthDone) {
                    // 8.35: the replay ignores whether the player is also pressing things
                    // (`g_userDrove` is only logged): the first run stood down by design when
                    // a pad press beat it by 0.2 s, which meant it was never actually tested.
                    if (g_feSeen == 0) {
                        g_feSeen = now;
                    } else if (now - g_feSeen >= kReplaySettleMs && now - g_bootTick < kReplayTimeoutMs) {
                        g_synthRun = true;
                        g_synthStart = now;
                        g_synthNext = 0;
                        g_synth = 0;
                        g_log("frontend replay: starting (%d presses) - should end in free skate on our map",
                              kFrontEndMacroCount);
                    }
                }
            }
        }
    }

    if (!kTryTheLoad) return;
    // (the one-script load path below is kept for reference; see the note above)

    const ULONGLONG now = GetTickCount64();
    if (g_bootTick == 0) g_bootTick = now;
    if (g_phase == 3) return;

    if (g_phase == 0) {
        if (now - g_bootTick < kStartDelayMs) return;
        g_phase = 1;
        g_nextAction = now;
    }
    if (now < g_nextAction) return;

    const GameState st = ReadGameState();

    if (g_phase == 1) { // trying
        if (!st.readOk) {
            g_nextAction = now + 500;
            return;
        }
        if (st.inFrontend == 0) {
            g_log("autostart: reTHAWed is already in a level (0x%08X); nothing to do", st.levelId);
            g_phase = 3;
            return;
        }
        if (st.inFrontend != 1) {
            if (now - g_bootTick > kGiveUpMs) {
                g_log("autostart: never saw reTHAWed's front end within %llu s (inFrontend=%d); giving up",
                      kGiveUpMs / 1000, st.inFrontend);
                g_phase = 3;
            } else {
                g_nextAction = now + 1000;
            }
            return;
        }
        ++g_attempts;
        g_triedAt = now;
        g_log("autostart: attempt %d: reTHAWed is on its front end; running script 0x%08X (load_MPGTALSE)",
              g_attempts, kLoadOurLevel);
        __try {
            auto run = reinterpret_cast<RunScriptFn>(kRunScript);
            const int rc = run(kLoadOurLevel, nullptr, nullptr, 0, 0, 0);
            g_log("autostart: the call returned %d", rc);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_log("autostart: the call faulted (0x%08X)", static_cast<unsigned>(GetExceptionCode()));
        }
        if (g_attempts >= kMaxAttempts) {
            g_phase = 2;
            g_observeUntil = now + kObserveMs;
            g_lastWatchLog = 0;
        }
        g_nextAction = now + kRetryMs;
        return;
    }

    if (g_phase == 2) { // watching what reTHAWed did with the call
        if (st.readOk && st.inFrontend == 0) {
            g_log("autostart: IT WORKED - reTHAWed is in a level now (0x%08X)", st.levelId);
            g_phase = 3;
            return;
        }
        if (now - g_lastWatchLog >= 1000) {
            g_lastWatchLog = now;
            g_log("autostart: %llu s after the last try: inFrontend=%d levelId=0x%08X paused=%d readOk=%d",
                  (now - g_triedAt) / 1000, st.inFrontend, st.readOk ? st.levelId : 0, st.readOk ? st.paused : -1,
                  st.readOk ? 1 : 0);
        }
        if (now > g_observeUntil) {
            g_log("autostart: gave up after %llu s (still inFrontend=%d). The script on its own does not "
                  "load the level - the front end must do more.",
                  kObserveMs / 1000, st.inFrontend);
            g_phase = 3;
        }
        return;
    }
}

// 8.35: the buttons the front-end replay is holding right now (0 = not replaying).
int AutoStartSyntheticButtons()
{
    return g_synthRun ? g_synth : 0;
}

// 8.35: the front-end navigation recorder. While reTHAWed is the front window, the
// player drives its menus with the pad; every press / release is logged with its
// time so the sequence can be replayed. Nothing else happens here.
void AutoStartNoteFrontButton(int gtaMask, bool down)
{
    if (!kRecordFrontEnd) return;
    if (!g_log || gtaMask == 0) return;
    int index = -1;
    for (int i = 0; i < kButtonCount; ++i) {
        if (gtaMask == (1 << i)) {
            index = i;
            break;
        }
    }
    if (index < 0) return;

    static unsigned char prev[kButtonCount] = {};
    const unsigned char now = down ? 1 : 0;
    if (now == prev[index]) return;
    prev[index] = now;
    if (down) g_userDrove = true; // the player is driving the menu: do not replay over them

    static ULONGLONG first = 0;
    static bool started = false;
    const ULONGLONG t = GetTickCount64();
    if (!started) {
        started = true;
        first = t;
        g_log("frontend: recording the menu navigation from here");
    }
    g_log("frontend: %-8s %s  (+%.2f s)", ButtonName(index), now ? "DOWN" : "up", (t - first) / 1000.0);
}

} // namespace mp::thaw
