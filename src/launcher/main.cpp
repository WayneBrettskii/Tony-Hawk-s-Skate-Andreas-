// my-passthrough launcher
//
// Phase 0b-1: starts reTHAWed and watches it. It does NOT load anything into
// the game yet. Steps, all written to logs\launcher.log (and the console):
//   1. check D:\Games\reTHAWed\reTHAWed.exe exists and is the expected size
//      (we never start THAW.exe, the retail exe with SecuROM; see AGENTS.md rule 5)
//   2. start reTHAWed.exe from its own folder, like reTHAWed's launcher does
//      (no updater, no start-up options)
//   3. wait until reTHAWed's own code (rthw.dll) is loaded in the game, and log
//      how long that took plus the list of loaded program files
//   4. wait for the game to close and log how it closed
//
// It only reads from the reTHAWed folder; it never writes there.

#include <windows.h>
#include <tlhelp32.h>
#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

#ifndef MP_LOG_DIR
#error "MP_LOG_DIR must be set by CMakeLists.txt"
#endif
#ifndef MP_RETHAWED_DIR
#error "MP_RETHAWED_DIR must be set by CMakeLists.txt"
#endif

namespace {

// reTHAWed 5.0.0.1's reTHAWed.exe on the user's PC (see MODLOG, phase 0b research).
constexpr ULONGLONG kExpectedExeSize = 3612672;
constexpr DWORD kRthwWaitMs = 30000;  // give up waiting for rthw.dll after 30 s
constexpr DWORD kPollMs = 100;

FILE* g_log = nullptr;
ULONGLONG g_startTick = 0;

// One line to the log file and the console: "[seconds since start] text".
void Log(const char* fmt, ...)
{
    char text[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    const double secs = (GetTickCount64() - g_startTick) / 1000.0;
    printf("[%9.3f] %s\n", secs, text);
    if (g_log) {
        fprintf(g_log, "[%9.3f] %s\n", secs, text);
        fflush(g_log);
    }
}

// ASCII path from CMake (forward slashes) -> Windows path (backslashes).
std::wstring Widen(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w += (*s == '/') ? L'\\' : static_cast<wchar_t>(static_cast<unsigned char>(*s));
    return w;
}

// Lower-case copy of a file name, for comparing names.
std::wstring Lower(std::wstring s)
{
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

// Names of the program files (exe + DLLs) loaded in a process.
// Returns false if Windows can't list them yet (common while a game is starting).
bool ListModules(DWORD pid, std::vector<std::wstring>& names)
{
    names.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            names.push_back(me.szModule);
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return !names.empty();
}

bool HasModule(const std::vector<std::wstring>& names, const wchar_t* wanted)
{
    const std::wstring w = Lower(wanted);
    for (const auto& n : names)
        if (Lower(n) == w) return true;
    return false;
}

int Run()
{
    const std::wstring gameDir = Widen(MP_RETHAWED_DIR);
    const std::wstring exePath = gameDir + L"\\reTHAWed.exe";
    Log("my-passthrough launcher: phase 0b-1 (start and watch only, nothing injected)");
    Log("game folder: %ls", gameDir.c_str());

    // 1. Check the exe.
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (!GetFileAttributesExW(exePath.c_str(), GetFileExInfoStandard, &info)) {
        Log("ERROR: cannot find %ls (Windows error %lu). Stopping.", exePath.c_str(), GetLastError());
        return 1;
    }
    const ULONGLONG size = (static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (size != kExpectedExeSize) {
        Log("ERROR: reTHAWed.exe is %llu bytes, expected %llu (reTHAWed 5.0.0.1). "
            "Was reTHAWed updated? Stopping.", size, kExpectedExeSize);
        return 1;
    }
    Log("reTHAWed.exe: %llu bytes: MATCH", size);

    // 2. Start it from its own folder.
    std::wstring cmdLine = L"\"" + exePath + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exePath.c_str(), cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        gameDir.c_str(), &si, &pi)) {
        Log("ERROR: could not start reTHAWed.exe (Windows error %lu). Stopping.", GetLastError());
        return 1;
    }
    const ULONGLONG launchTick = GetTickCount64();
    Log("started reTHAWed.exe, process id %lu", pi.dwProcessId);

    // 3. Wait for rthw.dll.
    std::vector<std::wstring> modules;
    bool found = false;
    unsigned listFailures = 0;
    while (GetTickCount64() - launchTick < kRthwWaitMs) {
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            Log("ERROR: reTHAWed closed before rthw.dll was seen (exit code %lu).", code);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return 1;
        }
        if (!ListModules(pi.dwProcessId, modules)) {
            ++listFailures;
        } else if (HasModule(modules, L"rthw.dll")) {
            found = true;
            break;
        }
        Sleep(kPollMs);
    }
    const double waitSecs = (GetTickCount64() - launchTick) / 1000.0;
    if (listFailures) Log("(module list not available yet %u times while starting; normal)", listFailures);
    if (!found) {
        Log("ERROR: rthw.dll not seen within %.1f s. reTHAWed is left running; check it.", waitSecs);
    } else {
        Log("rthw.dll found %.3f s after start; %zu program files loaded:", waitSecs, modules.size());
        for (const auto& m : modules) Log("  module: %ls", m.c_str());
    }

    // 4. Wait for the game to close.
    Log("waiting for reTHAWed to close...");
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    Log("reTHAWed closed after %.1f s, exit code %lu", (GetTickCount64() - launchTick) / 1000.0, exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return found ? 0 : 1;
}

} // namespace

int main()
{
    g_startTick = GetTickCount64();
    const std::wstring logDir = Widen(MP_LOG_DIR);
    CreateDirectoryW(logDir.c_str(), nullptr); // fine if it already exists
    const std::wstring logPath = logDir + L"\\launcher.log";
    if (_wfopen_s(&g_log, logPath.c_str(), L"w") != 0) {
        g_log = nullptr;
        printf("warning: could not open %ls; logging to this window only\n", logPath.c_str());
    }

    const int result = Run();
    if (g_log) fclose(g_log);
    return result;
}
