// SkateAndreas 8.35 (test): start our map from reTHAWed's front end. See autostart.h
// and docs/DESIGN.md 8.35.

#include <windows.h>
#include <d3d9.h>

#include "autostart.h"
#include "skater.h"
#include "../common/link.h"

#include <cstring>

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

// ---- 8.35 road 3 (2026-10-13): do what the level menu does ------------------
// From reTHAWed's own scripts: the menu ends in `change_level level = <load script>`,
// which ends in ChangeLevel (the step that sets the current-level field). Before it,
// the game mode: SetGameType freeskate, SetCurrentGameType. Called from Present (it runs
// at the front end whether or not anyone touches the pad), only on the thread that owns
// reTHAWed's window. See docs/DESIGN.md 8.35, road 3.
namespace {

constexpr bool kRoad3 = true;                       // the test is on
constexpr unsigned long kStructInit = 0x004141A0;   // thiscall: empty parameter list (8 bytes)
constexpr unsigned long kAddChecksum = 0x00415E00;  // thiscall (name, value): add name = checksum
constexpr unsigned long kStructClear = 0x00415420;  // thiscall: free its entries
constexpr unsigned long kSetGameType = 0xD6F4A573ul;
constexpr unsigned long kSetCurrentGameType = 0xE46F078Dul;
constexpr unsigned long kChangeLevel = 0x39C58EA1ul; // the script change_level
constexpr unsigned long kFreeSkate = 0x34861A16ul;
constexpr unsigned long kLevelParam = 0x651533ECul;  // "level"
constexpr ULONGLONG kFrontEndSettleMs = 8000;        // front end up this long first
constexpr ULONGLONG kRoad3WatchMs = 60000;

using StructInitFn = void*(__thiscall*)(void*);
using AddChecksumFn = void(__thiscall*)(void*, unsigned long, unsigned long);
using StructClearFn = void(__thiscall*)(void*);

struct Check3 {
    unsigned long at;
    unsigned char bytes[11];
    int n;
    const char* what;
};
const Check3 kChecks3[] = {
    {kStructInit, {0x8B, 0xC1, 0x33, 0xC9, 0x89, 0x48, 0x04, 0x88, 0x48, 0x02, 0xC3}, 11, "parameter list set-up"},
    {kAddChecksum, {0xA1, 0xCC, 0x10, 0x74, 0x00, 0x56, 0x8B, 0xF1}, 8, "add name = checksum"},
    {0x00415E5D, {0x80, 0xCA, 0x1A}, 3, "add name = checksum: checksum type"},
    {kStructClear, {0xE9, 0x1B, 0xFE, 0xFF, 0xFF}, 5, "parameter list clear"},
};

volatile LONG g_r3ok = 0;
volatile LONG g_r3busy = 0;      // re-entry guard (the load may draw frames itself)
int g_r3phase = 0;               // 0 waiting for the front end, 1 settling, 2 watching, 3 done
ULONGLONG g_r3feSince = 0, g_r3calledAt = 0, g_r3lastLog = 0;
DWORD g_r3windowThread = 0;

// One parameter list, in our own memory (reTHAWed's code keeps these on its stack too).
// Its entries come from reTHAWed's own pool and go back there in Clear.
struct alignas(4) ParamList {
    unsigned char raw[8];
};

int CallScript(unsigned long name, ParamList* params)
{
    __try {
        return reinterpret_cast<RunScriptFn>(kRunScript)(name, params, nullptr, 0, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_log("autostart road 3: script 0x%08lX FAULTED (0x%08X)", name, static_cast<unsigned>(GetExceptionCode()));
        return -1;
    }
}

bool Build(ParamList* p, unsigned long name, unsigned long value, bool add)
{
    __try {
        memset(p, 0, sizeof(*p));
        reinterpret_cast<StructInitFn>(kStructInit)(p);
        if (add) reinterpret_cast<AddChecksumFn>(kAddChecksum)(p, name, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_log("autostart road 3: building a parameter list FAULTED (0x%08X)", static_cast<unsigned>(GetExceptionCode()));
        return false;
    }
}

void Clear(ParamList* p)
{
    __try {
        reinterpret_cast<StructClearFn>(kStructClear)(p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_log("autostart road 3: clearing a parameter list FAULTED");
    }
}

// ---- 8.35 road 3 fix 1: give reTHAWed's player a controller ------------------
// reTHAWed (rthw.dll) only reads a controller that has joined as a player, and a
// controller joins only when reTHAWed sees one of its buttons pressed - which never
// happens while GTA is in front. When the player picks free skate by hand, their
// menu presses join the pad; road 3 skips the menu, so nobody joined and our input
// hook was never asked (user 2026-10-13: "got into skate mode but got no controller
// input"). Fix: do what that button press does - rthw's "add player" with the first
// controller reTHAWed has opened - then mark no buttons held (reTHAWed reads a newly
// joined pad only once the joining press is released). Addresses are rthw.dll
// 5.0.0.1 offsets, checked byte for byte (with rthw's actual load address) first.
constexpr DWORD kRthwAddPlayer = 0x00054C40;   // cdecl (SDL_GameController*)
constexpr DWORD kRthwSlots = 0x00269EF0;       // 2 x {controller*, held count byte, ...}, 8 bytes each
constexpr DWORD kRthwPlayerCount = 0x00269F00; // byte
constexpr DWORD kRthwPads = 0x00269FE8;        // SDL_GameController** (controllers reTHAWed opened)
constexpr DWORD kRthwPadCount = 0x00269FF0;    // int

using AddPlayerFn = void(__cdecl*)(void*);

bool RthwCodeMatches(unsigned long base)
{
    unsigned char want[21] = {0x83, 0xEC, 0x18, 0x80, 0x3D, 0, 0, 0, 0, 0x01, 0x77, 0x24, 0x8B, 0x15, 0, 0, 0, 0, 0x85, 0xD2, 0x74};
    const unsigned long a = base + kRthwPlayerCount, b = base + kRthwSlots;
    memcpy(&want[5], &a, 4);
    memcpy(&want[14], &b, 4);
    __try {
        return memcmp(reinterpret_cast<const void*>(base + kRthwAddPlayer), want, sizeof(want)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void JoinFirstController()
{
    const unsigned long base = reinterpret_cast<unsigned long>(GetModuleHandleA("rthw.dll"));
    if (!base) {
        g_log("autostart road 3: rthw.dll not loaded; can't join a controller");
        return;
    }
    if (!RthwCodeMatches(base)) {
        g_log("autostart road 3: code check in rthw.dll (add player, 0x%08lX) DIFFERENT; not joining a controller",
              base + kRthwAddPlayer);
        return;
    }
    __try {
        void** slot0 = reinterpret_cast<void**>(base + kRthwSlots);
        if (*slot0) {
            g_log("autostart road 3: player 1 already has a controller (0x%p); nothing to join", *slot0);
            return;
        }
        const int count = *reinterpret_cast<const int*>(base + kRthwPadCount);
        void** pads = *reinterpret_cast<void***>(base + kRthwPads);
        if (count <= 0 || !pads || !pads[0]) {
            g_log("autostart road 3: reTHAWed has no controller open (%d); none to join - touch the pad with "
                  "reTHAWed in front once", count);
            return;
        }
        reinterpret_cast<AddPlayerFn>(base + kRthwAddPlayer)(pads[0]);
        unsigned char* held = reinterpret_cast<unsigned char*>(base + kRthwSlots + 4);
        const unsigned char was = *held;
        *held = 0; // no buttons held: reTHAWed reads it from now on
        g_log("autostart road 3: joined controller 0x%p (of %d open) as player 1 (players now %u, held count %u -> 0)",
              pads[0], count, *reinterpret_cast<const unsigned char*>(base + kRthwPlayerCount), was);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_log("autostart road 3: joining a controller FAULTED (0x%08X)", static_cast<unsigned>(GetExceptionCode()));
    }
}

void Road3Go()
{
    JoinFirstController();
    ParamList p;
    g_log("autostart road 3: front end up %llu s, no level: SetGameType freeskate, SetCurrentGameType, "
          "change_level level = load_MPGTALSE",
          kFrontEndSettleMs / 1000);
    if (Build(&p, 0, kFreeSkate, true)) { // unnamed: SetGameType freeskate
        const int rc = CallScript(kSetGameType, &p);
        g_log("autostart road 3: SetGameType freeskate returned %d", rc);
        Clear(&p);
    }
    if (Build(&p, 0, 0, false)) {
        const int rc = CallScript(kSetCurrentGameType, &p);
        g_log("autostart road 3: SetCurrentGameType returned %d", rc);
        Clear(&p);
    }
    if (Build(&p, kLevelParam, kLoadOurLevel, true)) {
        const int rc = CallScript(kChangeLevel, &p);
        g_log("autostart road 3: change_level level = load_MPGTALSE returned %d", rc);
        Clear(&p);
    }
}

// ---- 8.35 road 3 fix 2: reTHAWed's sound muted while it is behind GTA --------
// reTHAWed (rthw.dll) has a "MuteOnUnfocus" option, on by default: when SDL tells it
// the window lost focus it flips three jumps in reTHAWed.exe to the muted state, and
// when focus comes back it flips them back. With the menu done by hand, the player's
// visit to reTHAWed's window gave it focus (unmuted) before our focus guard (focus.cpp)
// started swallowing "lost focus"; road 3 never brings reTHAWed to the front, so it
// stays muted - user 2026-10-13: "wasn't getting audio on the reTHAWed radio station".
// Fix: while reTHAWed is in our level and those bytes show the muted state, hand
// reTHAWed the same "focus gained" event SDL would (SDL_PushEvent), so its own code
// unmutes. Nothing of reTHAWed's is written by us.
constexpr unsigned long kMuteA = 0x004374AB, kMuteB = 0x004374C6, kMuteC = 0x004B78ED;
constexpr ULONGLONG kUnmuteRetryMs = 5000;

using PushEventFn = int(__cdecl*)(void*);

int MuteState() // 1 muted, 0 not muted, -1 neither pattern (unknown: leave alone)
{
    __try {
        const unsigned char a = *reinterpret_cast<volatile unsigned char*>(kMuteA);
        const unsigned char b = *reinterpret_cast<volatile unsigned char*>(kMuteB);
        const unsigned char c = *reinterpret_cast<volatile unsigned char*>(kMuteC);
        if (a == 0x74 && b == 0x74 && c == 0x75) return 1;
        if (a == 0x75 && b == 0x75 && c == 0x74) return 0;
        return -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void AudioWatch(const GameState& st, ULONGLONG now)
{
    static int lastState = -2;
    static ULONGLONG lastPush = 0;
    static int pushes = 0;
    const bool ours = st.readOk && st.inFrontend == 0 && st.levelId == kLoadOurLevel;
    const int m = MuteState();
    if (m != lastState) {
        g_log("autostart audio: reTHAWed's sound is %s (%s)", m == 1 ? "MUTED (window counted as unfocused)"
                                                             : m == 0 ? "on" : "in an unknown state",
              ours ? "in our level" : "not in our level");
        lastState = m;
    }
    if (!ours || m != 1 || now - lastPush < kUnmuteRetryMs || pushes >= 10) return;
    lastPush = now;
    HMODULE sdl = GetModuleHandleA("SDL2.dll");
    auto push = sdl ? reinterpret_cast<PushEventFn>(GetProcAddress(sdl, "SDL_PushEvent")) : nullptr;
    if (!push) {
        g_log("autostart audio: SDL_PushEvent not found; can't unmute");
        pushes = 10;
        return;
    }
    unsigned char ev[56] = {};
    const unsigned int type = 0x200; // SDL_WINDOWEVENT
    memcpy(ev, &type, 4);
    ev[12] = 12; // SDL_WINDOWEVENT_FOCUS_GAINED
    int rc = -1;
    __try {
        rc = push(ev);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rc = -2;
    }
    ++pushes;
    g_log("autostart audio: told reTHAWed its window has focus (SDL_PushEvent returned %d, try %d)", rc, pushes);
}

} // namespace

bool AutoStartRoad3ChecksPass(void (*log)(const char*, ...))
{
    if (!kRoad3) return false;
    if (g_ok != 1) {
        log("autostart road 3: off (the script runner check failed)");
        return false;
    }
    for (const Check3& c : kChecks3) {
        __try {
            const unsigned char* b = reinterpret_cast<const unsigned char*>(c.at);
            for (int i = 0; i < c.n; ++i)
                if (b[i] != c.bytes[i]) {
                    log("autostart road 3: code check at 0x%08lX (%s) DIFFERENT at byte %d; off", c.at, c.what, i);
                    return false;
                }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log("autostart road 3: could not read 0x%08lX (%s); off", c.at, c.what);
            return false;
        }
    }
    InterlockedExchange(&g_r3ok, 1);
    log("autostart road 3: code checks OK; on reTHAWed's front end we will ask for free skate on our map");
    return true;
}

void AutoStartPresentTick(void* device)
{
    if (g_r3ok != 1) return;
    if (InterlockedCompareExchange(&g_r3busy, 1, 0) != 0) return; // re-entered from inside our own call
    const ULONGLONG now = GetTickCount64();
    const DWORD me = GetCurrentThreadId();
    if (!g_r3windowThread && device) {
        D3DDEVICE_CREATION_PARAMETERS cp = {};
        if (SUCCEEDED(static_cast<IDirect3DDevice9*>(device)->GetCreationParameters(&cp)) && cp.hFocusWindow) {
            g_r3windowThread = GetWindowThreadProcessId(cp.hFocusWindow, nullptr);
            g_log("autostart road 3: Present runs on thread %lu; reTHAWed's window belongs to thread %lu: %s", me,
                  g_r3windowThread, me == g_r3windowThread ? "same - OK" : "DIFFERENT - test off");
            if (me != g_r3windowThread) g_r3phase = 3;
        }
    }
    if (g_r3windowThread == me) { // fix 2: keep reTHAWed's sound on in our level (any start, not only road 3)
        static ULONGLONG nextWatch = 0;
        if (now >= nextWatch) {
            nextWatch = now + 500;
            AudioWatch(ReadGameState(), now);
        }
    }
    if (g_r3phase != 3 && g_r3windowThread == me) {
        const GameState st = ReadGameState();
        if (g_r3phase == 0 || g_r3phase == 1) {
            if (st.readOk && st.inFrontend == 0) {
                g_log("autostart road 3: a level is already loaded (0x%08lX); nothing to do", st.levelId);
                g_r3phase = 3;
            } else if (st.readOk && st.inFrontend == 1) {
                if (g_r3phase == 0) {
                    g_r3phase = 1;
                    g_r3feSince = now;
                    g_log("autostart road 3: reTHAWed's front end is up (level 0x%08lX); waiting %llu s", st.levelId,
                          kFrontEndSettleMs / 1000);
                } else if (now - g_r3feSince >= kFrontEndSettleMs) {
                    g_r3phase = 2; // before the calls: a load that draws frames must not start it again
                    g_r3calledAt = now;
                    Road3Go();
                    g_r3lastLog = 0;
                }
            } else {
                g_r3phase = 0;
            }
        } else if (g_r3phase == 2) {
            if (now - g_r3lastLog >= 1000) {
                g_r3lastLog = now;
                const bool ours = st.readOk && st.inFrontend == 0 && st.levelId == kLoadOurLevel;
                g_log("autostart road 3: %llu s after: inFrontend=%d level=0x%08lX%s", (now - g_r3calledAt) / 1000,
                      st.readOk ? st.inFrontend : -1, st.readOk ? st.levelId : 0UL,
                      ours ? " = IN FREE SKATE ON OUR MAP" : "");
                if (ours) {
                    g_log("autostart road 3: IT WORKED");
                    g_r3phase = 3;
                }
            }
            if (g_r3phase == 2 && now - g_r3calledAt > kRoad3WatchMs) {
                g_log("autostart road 3: not in our level after %llu s; stopping (see DESIGN 8.35 road 3)",
                      kRoad3WatchMs / 1000);
                g_r3phase = 3;
            }
        }
    }
    InterlockedExchange(&g_r3busy, 0);
}

} // namespace mp::thaw
