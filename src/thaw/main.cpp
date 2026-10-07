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
// Uses only Windows' own file functions (no C runtime file I/O), so logging is
// safe even while the game is starting up or shutting down.

#include <windows.h>
#include "../common/link.h"
#include "skater.h"
#include <tlhelp32.h>
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

// Read the game state; log anything that changed. Returns the tag for
// position lines: "[menu]", "[level]" or "[?]".
const char* SampleGameState()
{
    const mp::thaw::GameState s = mp::thaw::ReadGameState();
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

void SampleSkater(const char* tag)
{
    const mp::thaw::SkaterSample s = mp::thaw::ReadSkater();
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
    Log("skater %s: pos %.2f %.2f %.2f | vel %.2f %.2f %.2f | speed %.2f", tag, s.pos[0], s.pos[1],
        s.pos[2], s.vel[0], s.vel[1], s.vel[2], speed);

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

DWORD WINAPI MainThread(LPVOID)
{
    if (!SetUpLogPath()) return 0; // nowhere to log; do nothing at all

    SYSTEMTIME now;
    GetLocalTime(&now);
    Log("my-passthrough THAW plugin: loaded (phase 1-2, attempt 2)");
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
    const bool canReadSkater = CheckCodeGroup(mp::thaw::kGroupSkater, "skater");
    const bool canReadState = CheckCodeGroup(mp::thaw::kGroupState, "state (menus/level/pause)");
    LogInFrontendJump();

    // Loop every 100 ms: skater sample each time; heartbeat + link once per second.
    unsigned long beat = 0;
    unsigned int tick = 0;
    while (!g_stop) {
        Sleep(100);
        const char* tag = canReadState ? SampleGameState() : "[?]";
        if (canReadSkater) SampleSkater(tag);
        if (++tick % 10 != 0) continue;

        ++beat;
        Log("heartbeat %lu", beat);
        if (canReadSkater) SkaterSecondCheck();
        ReportLink(beat);
    }
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
        // Tell the other side we're gone (just a memory write; safe here).
        if (g_link) InterlockedExchange(&g_link->thawPid, 0);
    }
    return TRUE;
}
