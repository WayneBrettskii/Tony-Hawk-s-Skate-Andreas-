// my-passthrough: GTA San Andreas plugin (.asi)
//
// Phase 0a-2c: counts GTA's frames. It writes a log file to the project's logs
// folder (logs\gta.log):
//   - "loaded" plus some facts about where it is running
//   - the bytes at the two places we hook, before and after hooking, and again
//     5 seconds later (to spot other mods hooking the same places)
//   - once per second: how many frames GTA drew and how many times it updated
//     the game world in that second, plus running totals
//
// The hooks (via plugin-sdk) only count; they don't change what the game does.
//
// Phase 0c: also opens the shared block (src/common/link.h). Once per second it
// writes its own process id, beat and drawn-frame total there, and logs the
// THAW side's process id and beat.
//
// Phase 2-1: once per second also logs the skater the THAW side publishes
// (shared block version 2). Read only: nothing in GTA is changed.
//
// Phase 2-2: skate mode (skatemode.cpp). Toggled with F7 or L3 + R3 while GTA
// is in front; while on, CJ follows the reTHAWed skater. Off = no changes.
// Test helper (teleport.cpp): F8 moves CJ to the waypoint on GTA's map.
//
// Phase 3-2: copies the controller (pad 0) into the shared block every world
// update, for the THAW side (padlink.cpp). Phase 3-4: during skate mode it then
// blanks pad 0, so GTA itself ignores the controller.
// Phase 4-0: F9 logs CJ's position as a "mark" (mark.cpp), for lining up a
// reTHAWed level with GTA's world.
// Phase 5: during skate mode CJ gets skating poses (skateanim.cpp) and a
// stand-in skateboard, drawn right after GTA draws the world (board.cpp).
//
// Uses only Windows' own file functions (no C runtime file I/O), so logging is
// safe even while the game is starting up or shutting down.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CPlayerPed.h"
#include "common.h"
#include <windows.h>
#include "../common/link.h"
#include "skatemode.h"
#include "teleport.h"
#include "padlink.h"
#include "mark.h"
#include "board.h"
#include "body.h"
#include "pacer.h"
#include "hudshow.h"
#include "radio.h"
#include "spawner.h"
#include "tags.h"
#include "bmxmode.h"
#include "cars.h"
#include "gps.h"
#include "testrail.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

HMODULE g_self = nullptr;
wchar_t g_logPath[MAX_PATH] = {};
ULONGLONG g_startTick = 0;
volatile LONG g_stop = 0;

// The gta_sa.exe 1.0 US file size we found on the user's PC (see MODLOG).
constexpr ULONGLONG kExpectedExeSize = 14383616;

// The shared block with the THAW side (phase 0c); nullptr if it couldn't be opened.
HANDLE g_linkMapping = nullptr;
mp::LinkBlock* g_link = nullptr;

// Where plugin-sdk hooks GTA SA 1.0 US (from its shared/Events.h):
//   drawingEvent:     call at 0x53E293, after the scene is drawn (once per frame)
//   gameProcessEvent: call at 0x53E981, after CGame::Process (world update)
constexpr uintptr_t kDrawHookAddr = 0x53E293;
constexpr uintptr_t kProcessHookAddr = 0x53E981;

// Counters, bumped on GTA's main thread, read by our logging thread.
std::atomic<unsigned long> g_drawCount{0};
std::atomic<unsigned long> g_processCount{0};
std::atomic<ULONGLONG> g_firstDrawTick{0};
std::atomic<ULONGLONG> g_firstProcessTick{0};

// The 5 bytes at a hook address. A "call" instruction is E8 followed by a
// 4-byte offset; the call goes to (address + 5 + offset).
struct HookBytes {
    unsigned char b[5] = {};
};
HookBytes g_drawBefore, g_drawAfter, g_processBefore, g_processAfter;
bool g_hooksAdded = false;

HookBytes ReadHookBytes(uintptr_t addr)
{
    HookBytes h;
    memcpy(h.b, reinterpret_cast<const void*>(addr), sizeof(h.b));
    return h;
}

void RecordFirst(std::atomic<ULONGLONG>& first)
{
    ULONGLONG expected = 0;
    first.compare_exchange_strong(expected, GetTickCount64());
}

void Log(const char* fmt, ...); // defined below

// Runs inside DllMain, before our thread starts: install the two hooks.
void AddHooks()
{
    g_drawBefore = ReadHookBytes(kDrawHookAddr);
    g_processBefore = ReadHookBytes(kProcessHookAddr);

    plugin::Events::drawingEvent.Add([] {
        RecordFirst(g_firstDrawTick);
        g_drawCount.fetch_add(1, std::memory_order_relaxed);
        // Phase 5-3j: even 60 fps when GTA's own frame limiter is off.
        mp::gta::PacerFrame(&Log);
    });
    plugin::Events::gameProcessEvent.Add([] {
        RecordFirst(g_firstProcessTick);
        g_processCount.fetch_add(1, std::memory_order_relaxed);
        // Phase 2-2: skate mode (does nothing unless toggled on with F7 / L3 + R3).
        mp::gta::SkateModeFrameSafe(g_link, &Log);
mp::gta::CarTick(g_link, &Log); // 8.33 C2: publish the cars' roof rails
        // Test helper: F8 = move CJ to the map waypoint.
        mp::gta::TeleportFrameSafe(&Log);
        // Phase 3-2: controller into the link (installs its hook the first time).
        mp::gta::PadLinkFrame(g_link, &Log);
        // Phase 4-0: F9 = log CJ's position as a landmark "mark".
        mp::gta::MarkFrameSafe(&Log);
        // 8.27: radio while skating (hold Back = next station).
        mp::gta::RadioFrame(g_link, &Log);
        // 8.28 d: F6 = vehicle spawner.
        mp::gta::SpawnerFrame(&Log);
        // 8.28 a: Grove tags for reTHAWed's graffiti tags.
        mp::gta::TagsFrame(g_link, &Log);
        // 8.29: BMX mode (B1: reTHAWed's rider gets on / off the bike with CJ).
        mp::gta::BmxFrame(g_link, &Log);
        // 8.31 D5: GPS route to the map waypoint.
        mp::gta::GpsFrameSafe(&Log);
        // 8.32 R3b test: show the F5 test rail as a row of lights.
        mp::gta::TestRailFrame(g_link, &Log);
    });

    // Phase 5-1: draw the stand-in board (only while skate mode is on).
    mp::gta::BoardAddHook();
    // Phase 5-3d: CJ's body copies the skater's (only while skate mode is on).
    mp::gta::BodyAddHook();
    // Phase 6 H4: reTHAWed's HUD over GTA's screen (only while skate mode is on).
    mp::gta::HudShowAddHook(&Log);
    // 8.27: station name on screen.
    mp::gta::RadioAddHook();
    // 8.28 d: the vehicle list on screen.
    mp::gta::SpawnerAddHook();
    // 8.28 a: GTA's tag calls, so our tags don't crash it.
    mp::gta::TagsAddHook();
    // 8.29 B4b: the BMX drawn like reTHAWed's bike.
    mp::gta::BmxAddHook();
    // 8.31 D5: the GPS route drawn on the radar and the pause menu map.
    mp::gta::GpsAddHook(&Log);

    g_drawAfter = ReadHookBytes(kDrawHookAddr);
    g_processAfter = ReadHookBytes(kProcessHookAddr);
    g_hooksAdded = true;
}

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

// The log goes in the project's logs folder, not next to the plugin: Mod Loader
// watches its own folders and rescans them every time a file there changes.
// MP_LOG_DIR is set by CMakeLists.txt (normally D:/Mods/my-passthrough/logs).
#ifndef MP_LOG_DIR
#error "MP_LOG_DIR must be set by CMakeLists.txt"
#endif

// If the logs folder can't be used, write one line next to the plugin saying why,
// then stay silent (a single write means a single Mod Loader rescan).
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

// Log path: <MP_LOG_DIR>\gta.log. Creates the folder if it is missing.
bool SetUpLogPath()
{
    const wchar_t* dir = L"" MP_LOG_DIR;
    if (!CreateDirectoryW(dir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        ReportLogFailure(GetLastError());
        return false;
    }
    if (wcscpy_s(g_logPath, dir) != 0 || wcscat_s(g_logPath, L"\\gta.log") != 0) {
        ReportLogFailure(ERROR_FILENAME_EXCED_RANGE);
        return false;
    }

    // 8.31 D4: keep the last launch's log as gta.prev.log (a crash's log was
    // lost when the game was started again).
    wchar_t prev[MAX_PATH] = {};
    if (wcscpy_s(prev, dir) == 0 && wcscat_s(prev, L"\\gta.prev.log") == 0)
        MoveFileExW(g_logPath, prev, MOVEFILE_REPLACE_EXISTING);

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

// Log one hook address: raw bytes, and where the call goes if it is a call.
void LogHookBytes(const char* label, uintptr_t addr, const HookBytes& h)
{
    if (h.b[0] == 0xE8) {
        int32_t offset;
        memcpy(&offset, &h.b[1], sizeof(offset));
        const uintptr_t target = addr + 5 + static_cast<uintptr_t>(offset);
        Log("%s 0x%06X: %02X %02X %02X %02X %02X = call to 0x%08X", label,
            static_cast<unsigned>(addr), h.b[0], h.b[1], h.b[2], h.b[3], h.b[4],
            static_cast<unsigned>(target));
    } else {
        Log("%s 0x%06X: %02X %02X %02X %02X %02X = NOT a call instruction", label,
            static_cast<unsigned>(addr), h.b[0], h.b[1], h.b[2], h.b[3], h.b[4]);
    }
}

double SecondsSinceStart(ULONGLONG tick)
{
    return (tick - g_startTick) / 1000.0;
}

// ---- 8.31 D4: crash logging ----
// Windows' last-chance handler: runs only when nothing else caught an error
// (our own code catches its errors itself). Logs what and where, then passes
// it on to whatever handler was there before (the game then closes as usual).
LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;
volatile LONG g_crashLogged = 0;

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
{
    if (ep && ep->ExceptionRecord && InterlockedExchange(&g_crashLogged, 1) == 0) {
        __try {
            const EXCEPTION_RECORD* r = ep->ExceptionRecord;
            const uintptr_t addr = reinterpret_cast<uintptr_t>(r->ExceptionAddress);
            HMODULE mod = nullptr;
            char name[MAX_PATH] = "unknown";
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(r->ExceptionAddress), &mod) && mod) {
                char full[MAX_PATH] = {};
                if (GetModuleFileNameA(mod, full, MAX_PATH)) {
                    const char* slash = strrchr(full, '\\');
                    strcpy_s(name, slash ? slash + 1 : full);
                }
            }
            const uintptr_t offset = mod ? addr - reinterpret_cast<uintptr_t>(mod) : addr;
            if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
                Log("CRASH: exception 0x%08lX at 0x%08X = %s+0x%X (%s address 0x%08X)", r->ExceptionCode,
                    static_cast<unsigned>(addr), name, static_cast<unsigned>(offset),
                    r->ExceptionInformation[0] == 0 ? "reading" : (r->ExceptionInformation[0] == 1 ? "writing" : "running"),
                    static_cast<unsigned>(r->ExceptionInformation[1]));
            else
                Log("CRASH: exception 0x%08lX at 0x%08X = %s+0x%X", r->ExceptionCode, static_cast<unsigned>(addr),
                    name, static_cast<unsigned>(offset));
            if (ep->ContextRecord)
                Log("CRASH: registers eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX esp %08lX ebp %08lX",
                    ep->ContextRecord->Eax, ep->ContextRecord->Ebx, ep->ContextRecord->Ecx, ep->ContextRecord->Edx,
                    ep->ContextRecord->Esi, ep->ContextRecord->Edi, ep->ContextRecord->Esp, ep->ContextRecord->Ebp);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        __try {
            CPlayerPed* ped = FindPlayerPed();
            if (ped) {
                const CVector& p = ped->GetPosition();
                Log("CRASH: CJ at %.2f %.2f %.2f, %s, area %d", p.x, p.y, p.z,
                    ped->bInVehicle ? "in a vehicle" : "on foot", static_cast<int>(ped->m_nAreaCode));
            }
            Log("CRASH: skate mode %s", mp::gta::SkateModeIsOn() ? "ON" : "off");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

// Put our handler in front (again later: mods loaded after us may set their
// own; we then pass on to theirs).
void InstallCrashFilter(const char* when)
{
    LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(&CrashFilter);
    if (prev != &CrashFilter) {
        g_prevFilter = prev;
        Log("crash log: handler installed (%s), passes on to %s", when, prev ? "an earlier handler" : "Windows");
    }
}

// Open the shared block and put our process id in it.
void SetUpLink()
{
    bool createdHere = false;
    DWORD error = 0;
    const mp::LinkOpenResult r = mp::OpenLink(g_linkMapping, g_link, createdHere, error);
    if (r == mp::LinkOpenResult::Ok) {
        InterlockedExchange(&g_link->gtaPid, static_cast<LONG>(GetCurrentProcessId()));
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

// Log the other side once per second: connected / not connected, and its beat.
// Phase 2-1: once per second, read the skater the THAW side published and log
// it. Read only: nothing in GTA is changed.
void LogThawSkater()
{
    static LONG lastCount = -1;
    if (!g_link || g_link->thawPid == 0) {
        lastCount = -1;
        return;
    }
    mp::SkaterShared s;
    int retries = 0;
    if (!mp::ReadSkaterShared(g_link, s, retries)) {
        Log("link skater: could not get a stable copy (%d tries)", retries);
        return;
    }
    const LONG now = static_cast<LONG>(GetTickCount64() & 0xFFFFFFFFu);
    const LONG ageMs = now - s.sampleTick;
    const LONG perSecond = (lastCount < 0) ? 0 : s.sampleCount - lastCount;
    lastCount = s.sampleCount;

    const char* where = !(s.flags & mp::kStateValid) ? "state ?"
                        : (s.flags & mp::kInLevel)  ? "level"
                                                    : "menu";
    if (s.flags & mp::kSkaterValid) {
        Log("link skater: [%s%s] level 0x%08lX | pos %.2f %.2f %.2f | vel %.2f %.2f %.2f | "
            "sample #%ld, %ld ms old, %ld new in the last second%s",
            where, (s.flags & mp::kPaused) ? ", paused" : "", s.levelId, s.pos[0], s.pos[1], s.pos[2],
            s.vel[0], s.vel[1], s.vel[2], s.sampleCount, ageMs, perSecond,
            retries ? " (had to re-read)" : "");
    } else {
        Log("link skater: [%s%s] no skater | sample #%ld, %ld ms old, %ld new in the last second", where,
            (s.flags & mp::kPaused) ? ", paused" : "", s.sampleCount, ageMs, perSecond);
    }
}

void LogPeer(const char* name, LONG pid, LONG beat, mp::PeerWatch& w)
{
    if (pid == 0) {
        if (w.lastPid != 0) Log("link: %s not connected", name);
        w.lastPid = 0;
        return;
    }
    if (pid != w.lastPid) {
        Log("link: %s connected, process id %ld", name, pid);
        w.lastPid = pid;
        w.lastBeat = beat;
    }
    Log("link: %s beat %ld (+%ld)", name, beat, beat - w.lastBeat);
    w.lastBeat = beat;
}

DWORD WINAPI MainThread(LPVOID)
{
    if (!SetUpLogPath()) return 0; // nowhere to log; do nothing at all

    SYSTEMTIME now;
    GetLocalTime(&now);
    Log("my-passthrough GTA plugin: loaded (phase 4-0: F9 marks)");
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
        Log("game exe size: %llu bytes (expected %llu for 1.0 US): %s", size, kExpectedExeSize,
            size == kExpectedExeSize ? "MATCH" : "DIFFERENT");
    } else {
        Log("game exe size: could not read (error %lu)", GetLastError());
    }

    Log("process id: %lu", GetCurrentProcessId());
    InstallCrashFilter("at start"); // 8.31 D4

    // plugin-sdk's own version check (reads the game's memory, changes nothing).
    const unsigned int version = plugin::GetGameVersion();
    Log("plugin-sdk %s: game version %u = %s", PLUGIN_SDK_VERSION_STR, version,
        plugin::GetGameVersionName(static_cast<int>(version)));

    // What the hook places looked like before and right after we hooked them.
    if (!g_hooksAdded) {
        Log("hooks: NOT added");
    } else {
        LogHookBytes("draw hook    before", kDrawHookAddr, g_drawBefore);
        LogHookBytes("draw hook    after ", kDrawHookAddr, g_drawAfter);
        LogHookBytes("process hook before", kProcessHookAddr, g_processBefore);
        LogHookBytes("process hook after ", kProcessHookAddr, g_processAfter);
    }

    SetUpLink();
    mp::PeerWatch thawWatch;

    // Once per second: counts for that second, and running totals.
    unsigned long lastDraw = 0, lastProcess = 0, second = 0;
    bool loggedFirstDraw = false, loggedFirstProcess = false;
    while (!g_stop) {
        Sleep(1000);
        ++second;

        const ULONGLONG firstDraw = g_firstDrawTick.load();
        if (!loggedFirstDraw && firstDraw != 0) {
            Log("first drawn frame at %.3f s", SecondsSinceStart(firstDraw));
            loggedFirstDraw = true;
        }
        const ULONGLONG firstProcess = g_firstProcessTick.load();
        if (!loggedFirstProcess && firstProcess != 0) {
            Log("first world update at %.3f s", SecondsSinceStart(firstProcess));
            loggedFirstProcess = true;
            InstallCrashFilter("in game, after other mods loaded"); // 8.31 D4
        }

        const unsigned long draw = g_drawCount.load();
        const unsigned long process = g_processCount.load();
        Log("second %lu: drawn %lu (total %lu), world updates %lu (total %lu)", second,
            draw - lastDraw, draw, process - lastProcess, process);
        lastDraw = draw;
        lastProcess = process;

        // Link: publish our beat and drawn total, then report the THAW side.
        if (g_link) {
            InterlockedExchange(&g_link->gtaBeat, static_cast<LONG>(second));
            InterlockedExchange(&g_link->gtaDrawn, static_cast<LONG>(draw));
            LogPeer("THAW", g_link->thawPid, g_link->thawBeat, thawWatch);
            LogThawSkater();
        }
        mp::gta::PadLinkSecond(&Log);
mp::gta::SkateModeBailTick(); // 8.33 C5: back to skate mode after a car hit
mp::gta::CarScanSecond(&Log); // 8.33 C1: the GTA cars near CJ (log only)

        // 5 s in: has another mod re-hooked the same places since we did?
        if (second == 5 && g_hooksAdded) {
            LogHookBytes("draw hook    at 5 s", kDrawHookAddr, ReadHookBytes(kDrawHookAddr));
            LogHookBytes("process hook at 5 s", kProcessHookAddr, ReadHookBytes(kProcessHookAddr));
        }
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
        // Hooks go in now, as plugin-sdk plugins normally do (they use static
        // constructors, which also run during DllMain).
        AddHooks();
        // Do the real work on our own thread: DllMain must stay tiny.
        HANDLE t = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        // 8.31 D4: a normal exit is logged; a crash logs "CRASH" lines instead
        // (and may not get here at all).
        if (g_logPath[0]) Log("GTA closing (plugin unloading)%s", g_crashLogged ? " after a crash" : " normally");
        InterlockedExchange(&g_stop, 1);
        // Tell the other side we're gone (just a memory write; safe here).
        if (g_link) InterlockedExchange(&g_link->gtaPid, 0);
    }
    return TRUE;
}
