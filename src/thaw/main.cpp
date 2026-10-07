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
// Uses only Windows' own file functions (no C runtime file I/O), so logging is
// safe even while the game is starting up or shutting down.

#include <windows.h>
#include <tlhelp32.h>
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

DWORD WINAPI MainThread(LPVOID)
{
    if (!SetUpLogPath()) return 0; // nowhere to log; do nothing at all

    SYSTEMTIME now;
    GetLocalTime(&now);
    Log("my-passthrough THAW plugin: loaded (phase 0b-2)");
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

    // Heartbeat: once per second, until the game closes.
    unsigned long beat = 0;
    while (!g_stop) {
        Sleep(1000);
        ++beat;
        Log("heartbeat %lu", beat);
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
    }
    return TRUE;
}
