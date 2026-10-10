// my-passthrough: reTHAWed plugin (my-passthrough-thaw.dll)
//
// Phase 0b-2: proves our code runs inside reTHAWed. It does nothing to the game.
// Our launcher loads it into reTHAWed.exe after reTHAWed's own code (rthw.dll)
// has started. It writes logs\thaw.log in the project folder:
//   - "loaded" plus some facts about where it is running
//   - the list of program files loaded in the game at that moment
//   - one "heartbeat" line per second, with a rising counter
// No hooks. Nothing network-related is touched (AGENTS.md rule 5).
//
// Phase 0c: also opens the shared block (src/common/link.h). Once per second it
// writes its own process id and beat there, and logs the GTA side's process id,
// beat and drawn-frame total.
//
// Phase 1-1: every 100 ms, reads the local skater's position and velocity
// (read only; see skater.h) and logs them; once per second it checks whether the
// distance moved matches the average speed.
//
// Phase 1-2: also reads menus/level/pause state every 100 ms (read only), logs
// changes, and tags each position line [menu] or [level].
//
// Phase 2-1: reads state and skater about every 15 ms and publishes them in the
// shared block (version 2) for the GTA side. The log still gets one position
// line per 100 ms. Still read only inside reTHAWed.
//
// Phase 3-1: first change to reTHAWed: wraps its window procedure so it never
// hears that it lost focus (it paused itself behind GTA). See focus.h.
//
// Phase 3-2: reads the controller state the GTA side publishes (link v3) and
// logs it: every change of the buttons at once, sticks once per second. Read only.
//
// Phase 3-3: reTHAWed takes its controller input from GTA while it is behind GTA
// and skate mode is on (input.cpp: two of rthw.dll's import entries replaced).
//
// Phase 4-0: F9 (with reTHAWed in front) logs the skater's position as a
// "mark", for lining up a reTHAWed level with GTA's world. Read only.
//
// Phase R4-1: also reads the skater's facing and skeleton (pose.cpp, read
// only) and logs them: facing + board + pelvis 10 times per second, the bone
// list once per skater, every bone on F9.
// Phase R4-2: reads them every pass and sends the facing and the board bone to
// GTA through the link (version 4).
//
// Uses only Windows' own file functions (no C runtime file I/O), so logging is
// safe even while the game is starting up or shutting down.

#include <windows.h>
#include "../common/link.h"
#include "skater.h"
#include "focus.h"
#include "input.h"
#include "pose.h"
#include "bikeparts.h"
#include "drawlog.h"
#include "touchpad.h"
#include "music.h"
#include "tags.h"
#include "bmx.h"
#include <tlhelp32.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

#ifndef MP_LOG_DIR
#error "MP_LOG_DIR must be set by CMakeLists.txt"
#endif

namespace {

HMODULE g_self = nullptr;
unsigned long g_repeatSamples = 0; // 5-3d fix 2: same reTHAWed frame read again
unsigned long g_newFrames = 0;     // 5-3g: new reTHAWed frames (skater moved) per second
// 8.26 body flips, for the once-per-second log
unsigned long g_flipSamples = 0, g_flippedFlag = 0;
float g_flipMax[3] = {};
constexpr long long kSettleMs = 4;  // 5-3f: wait this long after a new reTHAWed frame before reading it
wchar_t g_logPath[MAX_PATH] = {};
ULONGLONG g_startTick = 0;
volatile LONG g_stop = 0;

// reTHAWed 5.0.0.1's reTHAWed.exe on the user's PC (see MODLOG, phase 0b research).
constexpr ULONGLONG kExpectedExeSize = 3612672;

// The shared block with the GTA side (phase 0c); nullptr if it couldn't be opened.
HANDLE g_linkMapping = nullptr;
mp::LinkBlock* g_link = nullptr;

// Append one line to the log: "[seconds since start] text".
void Log(const char* fmt, ...)
{
    char line[512];
    const double secs = (GetTickCount64() - g_startTick) / 1000.0;
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%9.3f] ", secs);
    if (n < 0) n = 0;

    va_list args;
    va_start(args, fmt);
    int m = _vsnprintf_s(line + n, sizeof(line) - n, _TRUNCATE, fmt, args);
    va_end(args);
    if (m < 0) m = static_cast<int>(strlen(line + n)); // text was cut short

    int len = n + m;
    if (len > static_cast<int>(sizeof(line)) - 3) len = sizeof(line) - 3;
    line[len++] = '\r';
    line[len++] = '\n';

    HANDLE f = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, line, static_cast<DWORD>(len), &written, nullptr);
    CloseHandle(f);
}

// If the logs folder can't be used, write one file next to the plugin saying why.
void ReportLogFailure(DWORD error)
{
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(g_self, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;
    wchar_t* dot = wcsrchr(path, L'.');
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!dot || (slash && dot < slash) || (dot - path) + 18 > MAX_PATH) return;
    wcscpy_s(dot, MAX_PATH - (dot - path), L"-log-failed.txt");

    char text[256];
    int n = _snprintf_s(text, sizeof(text), _TRUNCATE,
                        "Could not write log to %s (Windows error %lu)\r\n", MP_LOG_DIR, error);
    if (n < 0) n = static_cast<int>(strlen(text));
    HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, text, static_cast<DWORD>(n), &written, nullptr);
    CloseHandle(f);
}

// Log path: <MP_LOG_DIR>\thaw.log. Creates the folder if it is missing.
bool SetUpLogPath()
{
    const wchar_t* dir = L"" MP_LOG_DIR;
    if (!CreateDirectoryW(dir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        ReportLogFailure(GetLastError());
        return false;
    }
    if (wcscpy_s(g_logPath, dir) != 0 || wcscat_s(g_logPath, L"\\thaw.log") != 0) {
        ReportLogFailure(ERROR_FILENAME_EXCED_RANGE);
        return false;
    }

    // Start a fresh log on every launch.
    HANDLE f = CreateFileW(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        ReportLogFailure(GetLastError());
        return false;
    }
    CloseHandle(f);
    return true;
}

// Log the program files loaded in this (the game's) process.
void LogModules()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) {
        Log("module list: not available (Windows error %lu)", GetLastError());
        return;
    }
    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);
    unsigned count = 0;
    bool sawRthw = false, sawSelf = false;
    if (Module32FirstW(snap, &me)) {
        do {
            ++count;
            Log("  module: %ls", me.szModule);
            if (_wcsicmp(me.szModule, L"rthw.dll") == 0) sawRthw = true;
            if (me.hModule == g_self) sawSelf = true;
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    Log("module list: %u program files; rthw.dll %s; this plugin %s", count,
        sawRthw ? "PRESENT" : "MISSING", sawSelf ? "PRESENT" : "MISSING");
}

// Open the shared block and put our process id in it.
void SetUpLink()
{
    bool createdHere = false;
    DWORD error = 0;
    const mp::LinkOpenResult r = mp::OpenLink(g_linkMapping, g_link, createdHere, error);
    if (r == mp::LinkOpenResult::Ok) {
        InterlockedExchange(&g_link->thawPid, static_cast<LONG>(GetCurrentProcessId()));
        Log("link: shared block %s (%s it)", "ready", createdHere ? "created" : "opened");
    } else if (r == mp::LinkOpenResult::WrongLayout) {
        Log("link: %s: magic 0x%08lX version %ld; not using it", mp::LinkOpenResultText(r),
            g_link->magic, g_link->version);
        g_link = nullptr;
    } else {
        Log("link: %s (Windows error %lu); continuing without the link", mp::LinkOpenResultText(r), error);
        g_link = nullptr;
    }
}

// Once per second: publish our beat, then report the GTA side.
void ReportLink(unsigned long beat)
{
    static mp::PeerWatch gtaWatch;
    static LONG lastGtaDrawn = 0;
    if (!g_link) return;
    InterlockedExchange(&g_link->thawBeat, static_cast<LONG>(beat));
    const LONG pid = g_link->gtaPid;
    const LONG gtaBeat = g_link->gtaBeat;
    const LONG drawn = g_link->gtaDrawn;
    if (pid == 0) {
        if (gtaWatch.lastPid != 0) Log("link: GTA not connected");
        gtaWatch.lastPid = 0;
        return;
    }
    if (pid != gtaWatch.lastPid) {
        Log("link: GTA connected, process id %ld", pid);
        gtaWatch.lastPid = pid;
        gtaWatch.lastBeat = gtaBeat;
        lastGtaDrawn = drawn;
    }
    Log("link: GTA beat %ld (+%ld), GTA drawn total %ld (+%ld)", gtaBeat,
        gtaBeat - gtaWatch.lastBeat, drawn, drawn - lastGtaDrawn);
    gtaWatch.lastBeat = gtaBeat;
    lastGtaDrawn = drawn;
}

// ---- Phase 1: the skater (read only; see skater.h) ----

// Check the game code for one group of addresses. All in the group must match;
// a failure only disables that group (lesson from phase 1-2 attempt 1).
bool CheckCodeGroup(int group, const char* name)
{
    unsigned int count = 0;
    const mp::thaw::CodeCheck* checks = mp::thaw::CodeChecks(count);
    bool allOk = true;
    for (unsigned int i = 0; i < count; ++i) {
        if (checks[i].group != group) continue;
        unsigned int at = 0;
        unsigned char found = 0;
        if (mp::thaw::RunCodeCheck(checks[i], at, found)) {
            Log("code check 0x%08lX: MATCH (%s)", checks[i].address, checks[i].what);
        } else {
            Log("code check 0x%08lX: DIFFERENT at byte %u (expected %02X, found %02X) (%s)",
                checks[i].address, at, checks[i].bytes[at], found, checks[i].what);
            allOk = false;
        }
    }
    Log(allOk ? "%s: code checks passed; reading every 100 ms"
              : "%s: code checks FAILED; not reading it", name);
    return allOk;
}

// Where does InFrontend's final jump go in the running game? (file: 0x00594310)
void LogInFrontendJump()
{
    const mp::thaw::JumpInfo j = mp::thaw::ReadInFrontendJump();
    if (!j.isJump) {
        Log("InFrontend jump at 0x0059A668: %02X %02X %02X %02X %02X = NOT a jump",
            j.bytes[0], j.bytes[1], j.bytes[2], j.bytes[3], j.bytes[4]);
        return;
    }
    Log("InFrontend jump at 0x0059A668: %02X %02X %02X %02X %02X = jump to 0x%08lX (%s), inside %ls",
        j.bytes[0], j.bytes[1], j.bytes[2], j.bytes[3], j.bytes[4], j.target,
        j.target == 0x00594310 ? "the original check" : "NOT the original 0x00594310",
        j.module[0] ? j.module : L"(no loaded file)");
}

// What we remember between samples.
struct SkaterTrack {
    unsigned long manager = 1;   // 1 = "nothing logged yet" (real values are 0 or an address)
    unsigned long skater = 1;
    unsigned long exceptions = 0;
    // for the once-per-second check
    bool haveStart = false;
    float startPos[3] = {};
    unsigned int gaps = 0;       // samples since startPos
    ULONGLONG startTick = 0;     // real time of startPos (ms)
    float speedSum = 0.0f;
    unsigned int speedSamples = 0;
    float lastPos[3] = {};
    ULONGLONG lastTick = 0;      // real time of lastPos (ms)
    bool haveLast = false;
};
SkaterTrack g_track;

float Length3(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// ---- Phase 1-2: menus / level / pause ----

struct StateTrack {
    bool logged = false;
    unsigned long gameFlow = 0;
    int inFrontend = -2;
    unsigned long levelId = 0;
    int paused = -2;
    unsigned long failures = 0;
};
StateTrack g_state;

// Read the game state into `s`; log anything that changed. Returns the tag for
// position lines: "[menu]", "[level]" or "[?]".
const char* SampleGameState(mp::thaw::GameState& s)
{
    s = mp::thaw::ReadGameState();
    StateTrack& t = g_state;
    if (!s.readOk) {
        ++t.failures;
        if (t.failures == 1 || t.failures % 50 == 0)
            Log("state: read failed (exception 0x%08lX), %lu so far", s.exceptionCode, t.failures);
        return "[?]";
    }
    if (!t.logged || s.gameFlow != t.gameFlow) {
        Log("state: game-flow pointer at 0x008A3B5C = 0x%08lX%s", s.gameFlow, s.gameFlow ? "" : " (none)");
        t.gameFlow = s.gameFlow;
    }
    if (!t.logged || s.inFrontend != t.inFrontend) {
        Log("state: in front end (menus) = %s",
            s.inFrontend == 1 ? "YES" : s.inFrontend == 0 ? "no" : "unknown");
        t.inFrontend = s.inFrontend;
    }
    if (!t.logged || s.levelId != t.levelId) {
        Log("state: level ID = 0x%08lX", s.levelId);
        t.levelId = s.levelId;
    }
    if (!t.logged || s.paused != t.paused) {
        Log("state: paused flag at 0x007CDD2C = %s", s.paused == 1 ? "ON" : "off");
        t.paused = s.paused;
    }
    t.logged = true;
    if (s.inFrontend == 1) return "[menu]";
    if (s.inFrontend == 0) return "[level]";
    return "[?]";
}

// Read the skater into `s`. Writes a position line only when `logLine` is set
// (10 times per second), but uses every sample for the once-per-second check.
void SampleSkater(const char* tag, bool logLine, mp::thaw::SkaterSample& s)
{
    s = mp::thaw::ReadSkater();
    SkaterTrack& t = g_track;

    if (!s.readOk) {
        ++t.exceptions;
        if (t.exceptions == 1 || t.exceptions % 50 == 0)
            Log("skater: read failed (exception 0x%08lX), %lu so far", s.exceptionCode, t.exceptions);
        t.haveStart = false;
        t.haveLast = false;
        return;
    }
    if (s.manager != t.manager) {
        Log("skater: manager pointer at 0x0074FB44 = 0x%08lX%s", s.manager, s.manager ? "" : " (none)");
        t.manager = s.manager;
    }
    if (s.skater != t.skater) {
        if (s.skater)
            Log("skater: local skater found at 0x%08lX", s.skater);
        else
            Log("skater: no local skater");
        t.skater = s.skater;
        t.haveStart = false;
        t.haveLast = false;
    }
    if (!s.skater) return;

    const ULONGLONG nowTick = GetTickCount64();
    const float speed = Length3(s.vel);
    if (logLine)
        Log("skater %s: pos %.2f %.2f %.2f | vel %.2f %.2f %.2f | speed %.2f", tag, s.pos[0],
            s.pos[1], s.pos[2], s.vel[0], s.vel[1], s.vel[2], speed);

    if (!t.haveStart) {
        for (int i = 0; i < 3; ++i) t.startPos[i] = s.pos[i];
        t.startTick = nowTick;
        t.gaps = 0;
        t.speedSum = 0.0f;
        t.speedSamples = 0;
        t.haveStart = true;
    } else {
        ++t.gaps;
    }
    t.speedSum += speed;
    ++t.speedSamples;
    for (int i = 0; i < 3; ++i) t.lastPos[i] = s.pos[i];
    t.lastTick = nowTick;
    t.haveLast = true;
}

// Once per second: does the distance moved match the average speed?
void SkaterSecondCheck()
{
    SkaterTrack& t = g_track;
    if (!t.haveStart || !t.haveLast || t.gaps < 2 || t.speedSamples == 0) return;

    const float d[3] = {t.lastPos[0] - t.startPos[0], t.lastPos[1] - t.startPos[1],
                        t.lastPos[2] - t.startPos[2]};
    const float moved = Length3(d);
    // Real time between the first and last sample of this window.
    const float seconds = (t.lastTick - t.startTick) / 1000.0f;
    if (seconds <= 0.0f) return;
    const float movedPerSecond = moved / seconds;
    const float avgSpeed = t.speedSum / t.speedSamples;
    if (avgSpeed > 1.0f)
        Log("skater check: moved %.2f in %.1f s = %.2f per s; average speed %.2f; ratio %.2f",
            moved, seconds, movedPerSecond, avgSpeed, movedPerSecond / avgSpeed);
    else
        Log("skater check: moved %.2f in %.1f s; average speed %.2f (standing still?)",
            moved, seconds, avgSpeed);

    // Start the next second from the last sample, so no time is skipped.
    for (int i = 0; i < 3; ++i) t.startPos[i] = t.lastPos[i];
    t.startTick = t.lastTick;
    t.gaps = 0;
    t.speedSum = 0.0f;
    t.speedSamples = 0;
}

// ---- Phase 3-2: the controller from GTA (read only) ----

struct InputTrack {
    bool have = false;
    LONG lastButtons = 0;
    LONG lastCount = 0;      // at the last per-second report
    LONG maxAge = 0;         // largest age seen this second (ms)
    unsigned failures = 0;
    mp::InputShared last;
};
InputTrack g_input;

void ButtonList(char* out, size_t size, LONG buttons)
{
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < mp::kButtonCount; ++i) {
        if (!(buttons & (1 << i))) continue;
        const char* name = mp::ButtonName(i);
        const size_t len = strlen(name);
        if (used + len + 2 >= size) break;
        if (used) out[used++] = ' ';
        memcpy(out + used, name, len);
        used += len;
        out[used] = '\0';
    }
    if (!used && size > 4) memcpy(out, "none", 5);
}

// Every pass: read the latest controller frame; log button changes at once.
void TrackGtaInput(ULONGLONG now)
{
    if (!g_link) return;
    mp::InputShared in;
    if (!mp::ReadInputShared(g_link, in)) {
        if (++g_input.failures == 1) Log("input from GTA: could not read (kept changing)");
        return;
    }
    if (in.count == 0) return; // GTA hasn't published yet
    const LONG age = static_cast<LONG>(now & 0xFFFFFFFFu) - in.tick;
    if (age > g_input.maxAge) g_input.maxAge = age;
    if (!g_input.have || in.buttons != g_input.lastButtons) {
        char names[160];
        ButtonList(names, sizeof(names), in.buttons);
        Log("input from GTA: buttons %s (frame %ld, %ld ms old)", names, in.count, age);
        g_input.lastButtons = in.buttons;
    }
    if (!g_input.have) g_input.lastCount = in.count;
    g_input.have = true;
    g_input.last = in;
}

// Once per second: how many frames arrived, how old, and where the sticks are.
void ReportGtaInput()
{
    if (!g_link || !g_input.have) return;
    const mp::InputShared& in = g_input.last;
    Log("input from GTA: %ld frames in the last second, up to %ld ms old | LX %d LY %d RX %d RY %d L2 %d R2 %d",
        in.count - g_input.lastCount, g_input.maxAge, in.axes[mp::kAxisLX], in.axes[mp::kAxisLY],
        in.axes[mp::kAxisRX], in.axes[mp::kAxisRY], in.axes[mp::kAxisL2], in.axes[mp::kAxisR2]);
    g_input.lastCount = in.count;
    g_input.maxAge = 0;
}

// ---- Phase 4-0: F9 = log the skater's position as a "mark" ----

bool ReTHAWedInFrontNow()
{
    HWND w = GetForegroundWindow();
    if (!w) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

// Phase R4-1 helpers ------------------------------------------------------

const char* BoneLabel(unsigned long name)
{
    const char* n = mp::thaw::BoneName(name);
    return n ? n : "?";
}

void LogBoneMatrix(const char* label, const mp::thaw::BoneMatrix& b)
{
    const float* m = b.m;
    Log("  %s: pos %.1f %.1f %.1f | right %.2f %.2f %.2f | up %.2f %.2f %.2f | at %.2f %.2f %.2f", label, m[12],
        m[13], m[14], m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]);
}

// Every bone, full matrix: once per skater (positions only) and on F9 (all).
void LogAllBones(unsigned long skater, bool full, const char* why)
{
    static mp::thaw::BoneMatrix bones[256];
    const int n = mp::thaw::ReadAllBones(skater, bones, 256);
    if (n < 0) {
        Log("pose bones (%s): read failed", why);
        return;
    }
    Log("pose bones (%s): %d bones%s", why, n, n ? "" : " (no skeleton found)");
    for (int i = 0; i < n; ++i) {
        const float* m = bones[i].m;
        if (full) {
            char label[96];
            snprintf(label, sizeof(label), "#%d 0x%08lX %s", i, bones[i].name, BoneLabel(bones[i].name));
            LogBoneMatrix(label, bones[i]);
        } else {
            Log("  #%d 0x%08lX %s: pos %.1f %.1f %.1f", i, bones[i].name, BoneLabel(bones[i].name), m[12], m[13],
                m[14]);
        }
    }
}

// Every pass while there is a skater (phase R4-2: also sent to GTA); logs 10
// times per second.
void SamplePose(unsigned long skater, const char* tag, bool logLine, mp::thaw::PoseSample& p)
{
    static unsigned long lastSkater = 0;
    static unsigned long failures = 0;
    p = mp::thaw::ReadPose(skater);
    if (!p.readOk) {
        if (++failures == 1 || failures % 50 == 0)
            Log("pose: read failed (exception 0x%08lX), %lu so far", p.exceptionCode, failures);
        return;
    }
    static unsigned long lastSkeleton = 0;
    if (skater == lastSkater && p.skeleton != lastSkeleton && p.boneCount) {
        // 8.29 B6: the bike may bring its own skeleton
        Log("pose: skeleton changed 0x%08lX -> 0x%08lX, %d bones, board bone #%d", lastSkeleton, p.skeleton,
            p.boneCount, p.boardIndex);
        LogAllBones(skater, false, "skeleton changed");
    }
    lastSkeleton = p.skeleton;
    if (skater != lastSkater) {
        lastSkater = skater;
        Log("pose: skater 0x%08lX, skeleton 0x%08lX, %d bones, board bone #%d, pelvis bone #%d, body bones for GTA %s",
            skater, p.skeleton, p.boneCount, p.boardIndex, p.pelvisIndex, p.haveBody ? "all found" : "MISSING");
        if (p.boneCount) LogAllBones(skater, false, "new skater");
    }
    if (!logLine) return;
    const float* o = p.orient;
    Log("pose %s: facing right %.2f %.2f %.2f | up %.2f %.2f %.2f | at %.2f %.2f %.2f | pos row %.1f %.1f %.1f %.1f",
        tag, o[0], o[1], o[2], o[4], o[5], o[6], o[8], o[9], o[10], o[12], o[13], o[14], o[15]);
    if (p.boardIndex >= 0) LogBoneMatrix("board", p.board);
    if (p.pelvisIndex >= 0) LogBoneMatrix("pelvis", p.pelvis);
}

void TickMark(const mp::thaw::GameState& state, bool stateOk, const mp::thaw::SkaterSample& s, bool skaterOk,
              bool poseOk)
{
    static bool wasDown = false;
    static int count = 0;
    const bool down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    const bool pressed = down && !wasDown;
    wasDown = down;
    if (!pressed || !ReTHAWedInFrontNow()) return;
    ++count;
    if (!skaterOk || !s.readOk || !s.skater) {
        Log("mark #%d (F9): no skater to read", count);
        return;
    }
    Log("mark #%d (F9): skater at %.2f %.2f %.2f | speed %.1f | level 0x%08lX%s", count, s.pos[0], s.pos[1],
        s.pos[2], Length3(s.vel), stateOk && state.readOk ? state.levelId : 0UL,
        stateOk && state.readOk && state.paused == 1 ? " | PAUSED" : "");
    if (poseOk) LogAllBones(s.skater, true, "F9");
}

DWORD WINAPI MainThread(LPVOID)
{
    if (!SetUpLogPath()) return 0; // nowhere to log; do nothing at all

    SYSTEMTIME now;
    GetLocalTime(&now);
    Log("my-passthrough THAW plugin: loaded (phase 4-0: F9 marks)");
    Log("date: %04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond);

    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_self, path, MAX_PATH);
    Log("plugin file: %ls", path);

    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    Log("game exe: %ls", exe);

    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (GetFileAttributesExW(exe, GetFileExInfoStandard, &info)) {
        const ULONGLONG size = (static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        Log("game exe size: %llu bytes (expected %llu for reTHAWed 5.0.0.1): %s", size,
            kExpectedExeSize, size == kExpectedExeSize ? "MATCH" : "DIFFERENT");
    } else {
        Log("game exe size: could not read (error %lu)", GetLastError());
    }

    Log("process id: %lu", GetCurrentProcessId());
    LogModules();

    SetUpLink();
    mp::thaw::TouchpadStart(g_link); // temporary test shortcut (touchpad.h)
    const bool canReadSkater = CheckCodeGroup(mp::thaw::kGroupSkater, "skater");
    const bool canReadState = CheckCodeGroup(mp::thaw::kGroupState, "state (menus/level/pause)");
    LogInFrontendJump();
    const bool canReadPose = canReadSkater && mp::thaw::PoseCodeChecksPass(&Log);
    if (canReadPose) mp::thaw::DisplayCodeChecksPass(&Log); // 8.26 body flips
    mp::thaw::MusicCodeChecksPass(&Log); // 8.27 radio
    mp::thaw::TagsStart(g_link, &Log);   // 8.28 a graffiti tags
    mp::thaw::BmxCodeChecksPass(&Log);   // 8.29 BMX

    // Phase 5-3f: wake about every 1 ms and check only the skater's position;
    // when it changes, reTHAWed has a new frame: note the exact time, wait
    // kSettleMs for reTHAWed to finish that frame (skeleton), then do a full
    // pass. Without a change, a full pass still runs every 10 ms (menus,
    // controller, standing still).
    // Full pass:
    //   read state + skater, publish to the link (phase 2-1)
    //   every 100 ms:   one position line in the log (as before)
    //   every second:   heartbeat, distance-vs-speed check, link report
    unsigned long beat = 0;
    LONG published = 0;
    ULONGLONG nextLog = GetTickCount64();
    ULONGLONG nextSecond = nextLog + 1000;
    timeBeginPeriod(1); // lets Sleep(1) really sleep about 1 ms
    LARGE_INTEGER qpcFreq;
    QueryPerformanceFrequency(&qpcFreq);
    const long long settleTicks = qpcFreq.QuadPart * kSettleMs / 1000;
    unsigned long quickSkater = 0;   // skater found by the last full pass
    float quickPos[3] = {};
    bool changeSeen = false;          // new reTHAWed frame noticed, full pass pending
    long long changeQpc = 0, frameQpc = 0;
    ULONGLONG nextFull = 0;
    while (!g_stop) {
        Sleep(1);
        LARGE_INTEGER qn;
        QueryPerformanceCounter(&qn);
        if (quickSkater && !changeSeen) {
            float p[3];
            if (mp::thaw::ReadPosQuick(quickSkater, p) && memcmp(p, quickPos, sizeof(p)) != 0) {
                memcpy(quickPos, p, sizeof(p));
                changeSeen = true;
                changeQpc = qn.QuadPart;
            }
        }
        const ULONGLONG nowFull = GetTickCount64();
        const bool settled = changeSeen && qn.QuadPart - changeQpc >= settleTicks;
        if (!settled && (changeSeen || nowFull < nextFull)) continue; // a noticed frame waits until settled
        nextFull = nowFull + 10;
        const ULONGLONG now = nowFull;
        const bool fromChange = changeSeen; // this pass reads a frame the quick check noticed
        changeSeen = false;
        const bool logLine = now >= nextLog;
        if (logLine) nextLog = now + 100;

        mp::thaw::FocusKeeperTick(&Log);
        TrackGtaInput(now);
        mp::thaw::InputTick(g_link, &Log);

        mp::thaw::GameState state;
        const char* tag = canReadState ? SampleGameState(state) : "[?]";
        mp::thaw::SkaterSample sample;
        if (canReadSkater) SampleSkater(tag, logLine, sample);
        TickMark(state, canReadState, sample, canReadSkater, canReadPose);
        {
            // phase 6 H1: F11 = log one reTHAWed frame's drawing; temporarily
            // also touchpad + D-pad Up on the PS5 controller (user plays on a TV)
            const LONG pad = mp::thaw::TouchpadTick(&Log);
            const bool combo = (pad & mp::kPadRawTouch) && (pad & mp::kPadRawUp);
            // phase 6 H2: catch the HUD only in a level, with reTHAWed behind GTA
            const bool inLevel = canReadState && state.readOk && state.inFrontend == 0;
            const bool hudToggle = (pad & mp::kPadRawTouch) && (pad & mp::kPadRawLeft);
            const bool leanToggle = (pad & mp::kPadRawTouch) && (pad & mp::kPadRawRight); // C1
            mp::thaw::DrawLogTick(&Log, combo, inLevel && !ReTHAWedInFrontNow(), hudToggle, leanToggle);
        }
        mp::thaw::PoseSample pose;
        float bikeParts[mp::kLinkBikeFloats] = {};
        bool bikePartsOk = false;
        float droppedBike[12] = {};
        bool droppedOk = false;
        if (canReadPose && sample.readOk && sample.skater) {
            SamplePose(sample.skater, tag, logLine, pose);
            // 8.29 B9: reTHAWed's own bike skeleton: its frame (instead of the
            // rider's board bone, so tailwhips show), fork, cranks; hands / feet on them
            float bikeBoard[12];
            bool bikeBoardOk = false;
            bikePartsOk = mp::thaw::BikePartsTick(sample.skater, pose.readOk ? pose.skeleton : 0,
                                                  g_link && g_link->thawBikeState == mp::kBikeStateOn, &Log, bikeParts,
                                                  bikeBoard, &bikeBoardOk);
            if (bikeBoardOk && pose.readOk && pose.boardIndex >= 0)
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 3; ++c) pose.board.m[r * 4 + c] = bikeBoard[r * 3 + c];
            // 8.29 B12: reTHAWed's bike lying in the world while the rider is off it
            if (pose.readOk && sample.readOk)
                droppedOk = mp::thaw::DroppedBikeTick(sample.skater, pose.skeleton,
                                                      g_link && g_link->thawBikeState == mp::kBikeStateOn,
                                                      pose.orient, sample.pos, &Log, droppedBike);
        }
        quickSkater = sample.readOk ? sample.skater : 0;

        if (g_link) {
            mp::SkaterShared shared;
            if (canReadSkater && sample.readOk && sample.skater) {
                shared.flags |= mp::kSkaterValid;
                for (int i = 0; i < 3; ++i) {
                    shared.pos[i] = sample.pos[i];
                    shared.vel[i] = sample.vel[i];
                }
                // Phase R4-2: facing (3 rows) and the board bone (4 rows).
                if (pose.readOk && pose.boardIndex >= 0) {
                    shared.flags |= mp::kPoseValid;
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c) shared.orient[r * 3 + c] = pose.orient[r * 4 + c];
                    for (int r = 0; r < 4; ++r)
                        for (int c = 0; c < 3; ++c) shared.board[r * 3 + c] = pose.board.m[r * 4 + c];
                    // 8.26: body flips turn the board too
                    mp::thaw::ApplyDisplayRotation(pose.disp, nullptr, 0, shared.board);
                }
                // Phase 5-3d: body bone positions for CJ.
                if (pose.readOk && pose.haveBody) {
                    static_assert(mp::kLinkBodyBones == mp::thaw::kBodyBoneCount, "body bone lists differ");
                    shared.flags |= mp::kBodyValid;
                    for (int i = 0; i < mp::kLinkBodyBones * 3; ++i) shared.body[i] = pose.body[i];
                    // 8.26: body flips (reTHAWed turns the drawn model, not the skeleton)
                    mp::thaw::ApplyDisplayRotation(pose.disp, shared.body, mp::kLinkBodyBones, nullptr);
                }
                if (pose.disp.active) {
                    ++g_flipSamples;
                    for (int a = 0; a < 3; ++a) {
                        const float v = pose.disp.angle[a] < 0 ? -pose.disp.angle[a] : pose.disp.angle[a];
                        if (v > g_flipMax[a]) g_flipMax[a] = v;
                    }
                }
                if (pose.disp.flipped) ++g_flippedFlag;
                // 8.29 B7: bike parts (in the bike's own space, so body flips don't matter)
                if (bikePartsOk) {
                    shared.flags |= mp::kBikeValid;
                    for (int i = 0; i < mp::kLinkBikeFloats; ++i) shared.bike[i] = bikeParts[i];
                }
                if (droppedOk) {
                    shared.flags |= mp::kDroppedBikeValid;
                    for (int i = 0; i < 12; ++i) shared.droppedBike[i] = droppedBike[i];
                }
            }
            if (canReadState && state.readOk) {
                shared.flags |= mp::kStateValid;
                if (state.inFrontend == 0) shared.flags |= mp::kInLevel;
                if (state.paused == 1) shared.flags |= mp::kPaused;
                shared.levelId = static_cast<LONG>(state.levelId);
            }
            // 5-3d fix 2: reTHAWed draws about 60 frames a second, we read
            // about 65 times: the same frame is often read twice. Keep the time
            // we FIRST saw this frame, so GTA's "move ahead by speed x age"
            // doesn't push CJ forward and then snap him back (stutter).
            {
                static float lastPos[3] = {}, lastVel[3] = {};
                static ULONGLONG firstSeen = 0;
                const bool same = memcmp(lastPos, shared.pos, sizeof(lastPos)) == 0 &&
                                  memcmp(lastVel, shared.vel, sizeof(lastVel)) == 0;
                if (!same || now - firstSeen > 200) {
                    firstSeen = now;
                    memcpy(lastPos, shared.pos, sizeof(lastPos));
                    memcpy(lastVel, shared.vel, sizeof(lastVel));
                } else {
                    ++g_repeatSamples;
                }
                shared.sampleTick = static_cast<LONG>(firstSeen & 0xFFFFFFFFu);
                // 5-3f: exact time of this reTHAWed frame: when the quick check
                // saw the position change; if a full pass found new data without
                // it (first frame, or the check was busy), this pass's time.
                if (fromChange || !same) ++g_newFrames;
                if (fromChange) frameQpc = changeQpc;
                else if (!same) frameQpc = qn.QuadPart;
                if (shared.flags & mp::kSkaterValid) memcpy(quickPos, shared.pos, sizeof(quickPos));
                shared.frameQpc = frameQpc;
            }
            shared.sampleCount = ++published;
            mp::PublishSkater(g_link, shared);
        }

        if (now < nextSecond) continue;
        nextSecond += 1000;
        if (nextSecond < now) nextSecond = now + 1000; // after a long stall, don't catch up

        ++beat;
        Log("heartbeat %lu", beat);
        if (canReadSkater) SkaterSecondCheck();
        mp::thaw::FocusKeeperSecond(&Log);
        ReportGtaInput();
        mp::thaw::InputSecond(&Log);
        mp::thaw::MusicSecond(&Log);
        mp::thaw::TagsSecond(&Log);
        mp::thaw::BmxSecond(&Log);
        mp::thaw::BikePartsSecond(&Log);
        ReportLink(beat);
        if (g_link) Log("link: published %ld skater samples so far (%lu repeats of the same reTHAWed frame in the last second) | new reTHAWed frames in the last second: %lu",
                        published, g_repeatSamples, g_newFrames);
        g_repeatSamples = 0;
        g_newFrames = 0;
        if (g_flipSamples || g_flippedFlag)
            Log("flips: display rotation in %lu samples in the last second, largest angles x %.0f y %.0f z %.0f degrees | "
                "'flipped' byte set in %lu samples",
                g_flipSamples, g_flipMax[0], g_flipMax[1], g_flipMax[2], g_flippedFlag);
        g_flipSamples = g_flippedFlag = 0;
        g_flipMax[0] = g_flipMax[1] = g_flipMax[2] = 0.0f;
    }
    timeEndPeriod(1);
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        g_startTick = GetTickCount64();
        DisableThreadLibraryCalls(module);
        // Do the real work on our own thread: DllMain must stay tiny.
        HANDLE t = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_stop, 1);
        mp::thaw::TouchpadStop();
        // Tell the other side we're gone (just a memory write; safe here).
        if (g_link) InterlockedExchange(&g_link->thawPid, 0);
    }
    return TRUE;
}
