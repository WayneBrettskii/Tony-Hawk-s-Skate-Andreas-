// my-passthrough launcher
//
// Phase 0b-2: starts reTHAWed and loads our THAW plugin into it.
// Steps, all written to logs\launcher.log (and the console):
//   1. check D:\Games\reTHAWed\reTHAWed.exe exists and is the expected size
//      (we never start THAW.exe, the retail exe with SecuROM; see AGENTS.md rule 5),
//      and that our plugin my-passthrough-thaw.dll is next to this launcher
//   2. start reTHAWed.exe from its own folder, like reTHAWed's launcher does
//      (no updater, no start-up options)
//   3. wait until reTHAWed's own code (rthw.dll) is loaded in the game, and log
//      how long that took plus the list of loaded program files
//   4. load our plugin into the game: copy its path into the game's memory and
//      ask the game to run Windows' LoadLibraryW on it ("DLL injection").
//      This adds our code; it changes no game files and no protection.
//   5. wait for the game to close and log how it closed
//
// Controller fix (2026-10-07): before starting reTHAWed, sets the SDL setting
// SDL_GAMECONTROLLER_IGNORE_DEVICES in this launcher's own environment, which
// reTHAWed inherits. reTHAWed reads controllers through SDL2, so it then ignores
// the virtual Xbox 360 controller DualSenseX creates (045E:028E) and uses only
// the real PS5 controller. Affects only reTHAWed started by this launcher; GTA
// (GInput, which needs the virtual Xbox controller) and Windows are untouched.
// Attempt 2: also sets SDL_JOYSTICK_RAWINPUT=0. In SDL 2.26.2 the "raw input"
// way of reading controllers skips the ignore list, and that is how the virtual
// controller still got in on attempt 1. The PS5 controller is read another way
// (SDL's own PS5 support), so it is not affected.
//
// It only reads from the reTHAWed folder; it never writes there.

// 8.27: also starts reTHAWed paused and, before it runs, lets its sound effects
// keep playing behind other windows (PatchSoundFocus; in memory only).

#include <windows.h>
#include <tlhelp32.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
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

// SDL setting passed to reTHAWed: controllers SDL must not treat as game
// controllers, as "0xVENDOR/0xPRODUCT". 045E:028E = Xbox 360 controller, which
// is what DualSenseX's virtual controller (via ViGEmBus) reports.
constexpr wchar_t kSdlIgnoreVar[] = L"SDL_GAMECONTROLLER_IGNORE_DEVICES";
constexpr wchar_t kSdlIgnoreValue[] = L"0x045E/0x028E";
// SDL setting that turns off its "raw input" controller reading (it ignores the
// list above). The virtual controller is then only seen through XInput, which
// does honour the list.
constexpr wchar_t kSdlRawInputVar[] = L"SDL_JOYSTICK_RAWINPUT";
constexpr wchar_t kSdlRawInputValue[] = L"0";

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

// Full path of our THAW plugin: next to this launcher exe.
std::wstring PluginPath()
{
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring p = self;
    const size_t slash = p.find_last_of(L'\\');
    p = (slash == std::wstring::npos) ? L"" : p.substr(0, slash + 1);
    return p + L"my-passthrough-thaw.dll";
}

// Load our plugin into the game. Logs every step; returns true on success.
bool InjectPlugin(HANDLE process, const std::wstring& dllPath)
{
    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);

    // a) room in the game's memory for the path text
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        Log("inject: could not reserve memory in the game (Windows error %lu)", GetLastError());
        return false;
    }
    // b) copy the path there
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, dllPath.c_str(), bytes, &written) || written != bytes) {
        Log("inject: could not copy the plugin path (Windows error %lu)", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }
    // c) LoadLibraryW lives in kernel32.dll, which Windows loads at the same address
    //    in every 32-bit program, so our address for it is valid in the game too.
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (!loadLibrary) {
        Log("inject: could not find LoadLibraryW (Windows error %lu)", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }
    // d) run LoadLibraryW(path) on a new thread inside the game
    const ULONGLONG t0 = GetTickCount64();
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (!thread) {
        Log("inject: could not start the loading thread (Windows error %lu)", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }
    const DWORD wait = WaitForSingleObject(thread, 10000);
    if (wait != WAIT_OBJECT_0) {
        // Still running: leave the memory alone, the game may still be using it.
        Log("inject: loading did not finish within 10 s (wait result %lu)", wait);
        CloseHandle(thread);
        return false;
    }
    // e) the thread's exit code is LoadLibraryW's result: 0 means it failed
    DWORD result = 0;
    GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    Log("inject: LoadLibraryW finished in %.3f s, result 0x%08lX (%s)",
        (GetTickCount64() - t0) / 1000.0, result, result ? "loaded" : "FAILED");
    return result != 0;
}

// 8.27 sound fix (user 21:40: no skate sounds while GTA is in front).
// reTHAWed makes its sound-effect buffers (0x00443F3B..0x00443FBD, IDirectSound
// CreateSoundBuffer) with flags 0x400B0 / 0x400E0: no DSBCAPS_GLOBALFOCUS
// (0x8000), so Windows mutes them whenever another program is in front. Its
// music uses another path and kept playing. We add 0x8000 to both flag values
// in reTHAWed's memory (nothing on disk), checked byte for byte first.
// reTHAWed.exe has no address randomisation (image base 0x400000, checked).
void PatchSoundFocus(HANDLE process)
{
    struct Site {
        DWORD address;            // the instruction: mov dword [reg+0x18], flags
        unsigned char expect[7];
        const char* what;
    };
    const Site sites[] = {
        {0x00443F48, {0xC7, 0x41, 0x18, 0xB0, 0x00, 0x04, 0x00}, "3D sound effects (0x400B0)"},
        {0x00443F7B, {0xC7, 0x42, 0x18, 0xE0, 0x00, 0x04, 0x00}, "other sound effects (0x400E0)"},
    };
    for (const Site& s : sites) {
        unsigned char now[7] = {};
        SIZE_T got = 0;
        if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(static_cast<ULONG_PTR>(s.address)), now, sizeof(now), &got) ||
            got != sizeof(now) || memcmp(now, s.expect, sizeof(now)) != 0) {
            Log("sound fix: code at 0x%08lX is not as expected; sound effects left as they are (%s)", s.address, s.what);
            continue;
        }
        const ULONG_PTR flagByte = static_cast<ULONG_PTR>(s.address) + 4; // second byte of the flags: 0x00 -> 0x80
        DWORD old = 0;
        if (!VirtualProtectEx(process, reinterpret_cast<LPVOID>(flagByte), 1, PAGE_EXECUTE_READWRITE, &old)) {
            Log("sound fix: could not unlock 0x%08lX (Windows error %lu)", static_cast<unsigned long>(flagByte), GetLastError());
            continue;
        }
        const unsigned char value = 0x80;
        SIZE_T written = 0;
        const BOOL ok = WriteProcessMemory(process, reinterpret_cast<LPVOID>(flagByte), &value, 1, &written);
        DWORD tmp = 0;
        VirtualProtectEx(process, reinterpret_cast<LPVOID>(flagByte), 1, old, &tmp);
        FlushInstructionCache(process, reinterpret_cast<LPCVOID>(flagByte), 1);
        if (ok && written == 1)
            Log("sound fix: %s now keep playing behind other windows (global focus added at 0x%08lX)", s.what, s.address);
        else
            Log("sound fix: could not write 0x%08lX (Windows error %lu)", static_cast<unsigned long>(flagByte), GetLastError());
    }
}

int Run()
{
    const std::wstring gameDir = Widen(MP_RETHAWED_DIR);
    const std::wstring exePath = gameDir + L"\\reTHAWed.exe";
    Log("my-passthrough launcher: phase 0b-2 (start reTHAWed, load our THAW plugin)");
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

    const std::wstring dllPath = PluginPath();
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("ERROR: our plugin is missing: %ls (Windows error %lu). Rebuild. Stopping.",
            dllPath.c_str(), GetLastError());
        return 1;
    }
    Log("our plugin: %ls", dllPath.c_str());

    // Controller fix: reTHAWed inherits this launcher's environment.
    if (SetEnvironmentVariableW(kSdlIgnoreVar, kSdlIgnoreValue))
        Log("controller fix: %ls=%ls (reTHAWed will ignore Xbox 360 controllers, incl. DualSenseX's virtual one)",
            kSdlIgnoreVar, kSdlIgnoreValue);
    else
        Log("controller fix: could not set %ls (Windows error %lu); reTHAWed starts without it",
            kSdlIgnoreVar, GetLastError());
    if (SetEnvironmentVariableW(kSdlRawInputVar, kSdlRawInputValue))
        Log("controller fix: %ls=%ls (SDL's raw input controller reading off)", kSdlRawInputVar, kSdlRawInputValue);
    else
        Log("controller fix: could not set %ls (Windows error %lu)", kSdlRawInputVar, GetLastError());

    // 2. Start it from its own folder (paused, for the sound fix below).
    std::wstring cmdLine = L"\"" + exePath + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exePath.c_str(), cmdLine.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        gameDir.c_str(), &si, &pi)) {
        Log("ERROR: could not start reTHAWed.exe (Windows error %lu). Stopping.", GetLastError());
        return 1;
    }
    const ULONGLONG launchTick = GetTickCount64();
    Log("started reTHAWed.exe (paused), process id %lu", pi.dwProcessId);
    // 8.27 sound fix, before any of reTHAWed's code runs: its sound effects get
    // DirectSound's "global focus" flag, so they keep playing behind GTA.
    PatchSoundFocus(pi.hProcess);
    if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1))
        Log("ERROR: could not resume reTHAWed (Windows error %lu)", GetLastError());
    else
        Log("reTHAWed running");

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

        // 4. Load our plugin, then confirm it shows up in the game's module list.
        if (InjectPlugin(pi.hProcess, dllPath)) {
            if (ListModules(pi.dwProcessId, modules) && HasModule(modules, L"my-passthrough-thaw.dll"))
                Log("confirmed: my-passthrough-thaw.dll is loaded in reTHAWed (%zu program files now)",
                    modules.size());
            else
                Log("WARNING: plugin reported loaded but is not in the module list");
        } else {
            Log("ERROR: our plugin was not loaded. reTHAWed keeps running without it.");
            found = false; // report failure as the launcher's result
        }
    }

    // 5. Wait for the game to close.
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
