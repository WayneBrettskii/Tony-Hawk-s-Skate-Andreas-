// Tony Hawk's SkateAndreas launcher (GUI)
//
// What it does, in plain language (see docs/DESIGN.md, section 8.34):
//   - a small window with a Play button, a few setup checks, a tick box and a
//     button that opens the logs folder;
//   - Play: check the setup, start reTHAWed and load our THAW plugin into it,
//     start GTA San Andreas, then wait until the games are closed;
//   - everything is written to logs\launcher.log, so a problem can be read
//     afterwards even after the window is gone.
//
// The old console behaviour is kept behind a hidden --selftest switch: it runs
// the checks and writes them to the log, starting nothing. That is how the
// launcher is tested without opening a game.
//
// The game work itself is unchanged from the console launcher (phase 0b-2):
//   1. reTHAWed.exe must exist and be the expected (5.0.0.1) size; we never run
//      THAW.exe, the retail exe with SecuROM (AGENTS.md rule 5)
//   2. start reTHAWed.exe from its own folder, paused, with the sound fix applied
//      and the controller environment set (both inherited from us)
//   3. wait until reTHAWed's own code (rthw.dll) is loaded
//   4. load our plugin into it (LoadLibraryW on a thread inside the game - "DLL
//      injection"; it changes no game files and no protection)
//   5. start GTA San Andreas
//   6. wait for the games to close; if GTA closes first, close reTHAWed (we
//      started it)
//
// Controller fix (2026-10-07): before starting reTHAWed, sets SDL_GAMECONTROLLER_
// IGNORE_DEVICES and SDL_JOYSTICK_RAWINPUT in this launcher's environment, which
// reTHAWed inherits, so it ignores the virtual Xbox 360 controller DualSenseX
// creates. GTA (GInput) is untouched. (Section 11: this should become a tick box.)
//
// It only reads from the game folders; it never writes there.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#ifndef MP_LOG_DIR
#error "MP_LOG_DIR must be set by CMakeLists.txt"
#endif
#ifndef MP_RETHAWED_DIR
#error "MP_RETHAWED_DIR must be set by CMakeLists.txt"
#endif
#ifndef MP_GTA_DIR
#error "MP_GTA_DIR must be set by CMakeLists.txt"
#endif

#include "../common/paths.h" // 8.38: the mod's home folder, set from here

namespace {

// reTHAWed 5.0.0.1's reTHAWed.exe on the user's PC (see MODLOG, phase 0b research).
constexpr ULONGLONG kExpectedExeSize = 3612672;
constexpr DWORD kRthwWaitMs = 30000; // give up waiting for rthw.dll after 30 s
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

// Window controls.
constexpr int kCheckCount = 5;
constexpr int IDC_PLAY = 1001;
constexpr int IDC_HIDDEN = 1002;
constexpr int IDC_LOGS = 1003;
constexpr int IDC_STATUS = 1004;
constexpr int IDC_BROWSE_GTA = 1005;
constexpr int IDC_BROWSE_THAW = 1006;
constexpr int IDC_GTA_PATH = 1007;
constexpr int IDC_THAW_PATH = 1008;
constexpr int IDC_BUILDMAP = 1009;
constexpr int IDC_CHECK0 = 1010; // .. 1010 + kCheckCount - 1
constexpr UINT WM_MP_STATUS = WM_APP + 1;
constexpr UINT WM_MP_DONE = WM_APP + 2;

FILE* g_log = nullptr;
ULONGLONG g_startTick = 0;

CRITICAL_SECTION g_cs;
std::wstring g_status;
HWND g_hwnd = nullptr;
HWND g_statusHwnd = nullptr;
HWND g_playHwnd = nullptr;
HWND g_hiddenHwnd = nullptr;
HWND g_checkHwnd[kCheckCount] = {};
bool g_checkOk[kCheckCount] = {};
bool g_checkWarn[kCheckCount] = {};
bool g_running = false;
bool g_startHidden = false;

// 8.36: the game folders. They start at the paths the build was made with, and the
// player can point them somewhere else with the Browse buttons; their choices are
// remembered in SkateAndreas.ini next to the launcher.
std::wstring g_gtaDir;
std::wstring g_rethawedDir;
std::wstring g_modHome; // 8.38: told to the games as SKATEANDREAS_DIR
HWND g_gtaPathHwnd = nullptr;
HWND g_thawPathHwnd = nullptr;
HWND g_buildHwnd = nullptr;

// ---- logging (same shape as the console launcher) ----

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

// Narrow text from wide text, for the log.
std::string Narrow(const wchar_t* w)
{
    std::string s;
    for (; w && *w; ++w) s += (*w < 128) ? static_cast<char>(*w) : '?';
    return s;
}

// ---------- small helpers (unchanged from the console launcher) ----------

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
    return p + L"SkateAndreas-thaw.dll";
}

// 8.36: the launcher's own folder (with a trailing backslash), where the settings
// file lives.
std::wstring LauncherDir()
{
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring p = self;
    const size_t slash = p.find_last_of(L'\\');
    return (slash == std::wstring::npos) ? L"" : p.substr(0, slash + 1);
}

std::wstring SettingsPath()
{
    return LauncherDir() + L"SkateAndreas.ini";
}

// 8.38: the folder this launcher treats as the mod's home, and tells both games about
// through SKATEANDREAS_DIR. Normally that is the launcher's own folder — but when it is
// running from a build output folder inside this project (`..\..` holds both `logs` and
// `CMakeLists.txt`, which is how this project is laid out), the project folder is the home:
// that is where the logs and `extracted\` already are. Both markers are required, so an
// ordinary folder a player unzipped into is never mistaken for the project.
std::wstring ModHome()
{
    wchar_t full[MAX_PATH] = {};
    const std::wstring up = LauncherDir() + L"..\\..";
    if (GetFullPathNameW(up.c_str(), MAX_PATH, full, nullptr) > 0) {
        std::wstring s = full;
        while (!s.empty() && s.back() == L'\\') s.pop_back();
        if (GetFileAttributesW((s + L"\\CMakeLists.txt").c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW((s + L"\\logs").c_str()) != INVALID_FILE_ATTRIBUTES)
            return s;
    }
    std::wstring own = LauncherDir();
    while (!own.empty() && own.back() == L'\\') own.pop_back();
    return own;
}

// Read the saved folders. A missing or empty value leaves the built-in default.
void LoadSettings()
{
    const std::wstring ini = SettingsPath();
    wchar_t buf[MAX_PATH] = {};
    if (GetPrivateProfileStringW(L"Paths", L"GtaDir", L"", buf, MAX_PATH, ini.c_str()) > 0 && buf[0])
        g_gtaDir = buf;
    if (GetPrivateProfileStringW(L"Paths", L"RethawedDir", L"", buf, MAX_PATH, ini.c_str()) > 0 && buf[0])
        g_rethawedDir = buf;
}

void SaveSettings()
{
    const std::wstring ini = SettingsPath();
    WritePrivateProfileStringW(L"Paths", L"GtaDir", g_gtaDir.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Paths", L"RethawedDir", g_rethawedDir.c_str(), ini.c_str());
}

// The plain Windows "pick a folder" window, starting where `out` already points.
bool BrowseForFolder(HWND owner, std::wstring& out)
{
    IFileDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (!out.empty()) {
        IShellItem* start = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(out.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
            dlg->SetFolder(start);
            start->Release();
        }
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                out = path;
                CoTaskMemFree(path);
                ok = true;
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

// Put the two folders on the window (shortened from the left: the end matters most).
void ShowPaths()
{
    auto show = [](HWND h, const std::wstring& p) {
        if (!h) return;
        std::wstring s = p;
        if (s.size() > 44) s = L"..." + s.substr(s.size() - 41);
        SetWindowTextW(h, s.c_str());
    };
    show(g_gtaPathHwnd, g_gtaDir);
    show(g_thawPathHwnd, g_rethawedDir);
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
    Log("inject: LoadLibraryW finished in %.3f s, result 0x%08lX (%s)", (GetTickCount64() - t0) / 1000.0, result,
        result ? "loaded" : "FAILED");
    return result != 0;
}

// 8.27 sound fix (user 21:40: no skate sounds while GTA is in front).
// reTHAWed makes its sound-effect buffers with flags 0x400B0 / 0x400E0: no
// DSBCAPS_GLOBALFOCUS (0x8000), so Windows mutes them whenever another program is
// in front. We add 0x8000 to both flag values in reTHAWed's memory (nothing on
// disk), checked byte for byte first. reTHAWed.exe has no address randomisation.
void PatchSoundFocus(HANDLE process)
{
    struct Site {
        DWORD address; // the instruction: mov dword [reg+0x18], flags
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
        if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(static_cast<ULONG_PTR>(s.address)), now, sizeof(now),
                               &got) ||
            got != sizeof(now) || memcmp(now, s.expect, sizeof(now)) != 0) {
            Log("sound fix: code at 0x%08lX is not as expected; sound effects left as they are (%s)", s.address, s.what);
            continue;
        }
        const ULONG_PTR flagByte = static_cast<ULONG_PTR>(s.address) + 4; // second byte of the flags: 0x00 -> 0x80
        DWORD old = 0;
        if (!VirtualProtectEx(process, reinterpret_cast<LPVOID>(flagByte), 1, PAGE_EXECUTE_READWRITE, &old)) {
            Log("sound fix: could not unlock 0x%08lX (Windows error %lu)", static_cast<unsigned long>(flagByte),
                GetLastError());
            continue;
        }
        const unsigned char value = 0x80;
        SIZE_T written = 0;
        const BOOL ok = WriteProcessMemory(process, reinterpret_cast<LPVOID>(flagByte), &value, 1, &written);
        DWORD tmp = 0;
        VirtualProtectEx(process, reinterpret_cast<LPVOID>(flagByte), 1, old, &tmp);
        FlushInstructionCache(process, reinterpret_cast<LPCVOID>(flagByte), 1);
        if (ok && written == 1)
            Log("sound fix: %s now keep playing behind other windows (global focus added at 0x%08lX)", s.what,
                s.address);
        else
            Log("sound fix: could not write 0x%08lX (Windows error %lu)", static_cast<unsigned long>(flagByte),
                GetLastError());
    }
}

// ---------- the setup checks ----------

struct SetupCheck {
    std::wstring label;
    bool ok = false;   // true = not a failure (a warning still counts as ok)
    bool warn = false; // worth knowing, but does NOT stop Play
    std::wstring note; // why it failed, or what the warning is
};

// Path of our GTA plugin (in GTA's Mod Loader folder).
std::wstring GtaPluginPath()
{
    return g_gtaDir + L"\\modloader\\SkateAndreas\\SkateAndreas.asi";
}

std::vector<SetupCheck> RunChecks()
{
    std::vector<SetupCheck> out;
    out.reserve(kCheckCount);

    // 1. reTHAWed.exe, present and the expected version.
    {
        SetupCheck c{L"reTHAWed 5.0.0.1", false, false, L""};
        const std::wstring exe = g_rethawedDir + L"\\reTHAWed.exe";
        WIN32_FILE_ATTRIBUTE_DATA info = {};
        if (!GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &info)) {
            c.note = L"not found: " + exe;
        } else {
            const ULONGLONG size = (static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            if (size != kExpectedExeSize) {
                // 8.43: a WARNING, not a refusal (user 2026-10-13: "let them try to launch at
                // least, maybe it will work"). The size is only a guess that this is the build we
                // were written against; if it is a different one the plugin's code checks say
                // exactly what moved, in thaw.log. So Play is allowed through.
                c.ok = true;   // not a failure
                c.warn = true; // but say so
                c.note = L"your copy is a different version - trying it anyway";
                static bool told = false;
                if (!told) {
                    told = true;
                    Log("launcher: reTHAWed.exe is %llu bytes, but this mod was written against 5.0.0.1 "
                        "(%llu bytes) - letting it start, and the plugin will report any code it does not "
                        "recognise",
                        size, kExpectedExeSize);
                }
            } else {
                c.ok = true;
            }
        }
        out.push_back(c);
    }
    // 2. our plugin next to the launcher.
    {
        SetupCheck c{L"reTHAWed plugin (SkateAndreas-thaw.dll)", false, false, L""};
        const std::wstring dll = PluginPath();
        if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES)
            c.note = L"not found next to the launcher: " + dll;
        else
            c.ok = true;
        out.push_back(c);
    }
    // 3. our plugin in GTA's Mod Loader folder.
    {
        SetupCheck c{L"GTA plugin (SkateAndreas.asi)", false, false, L""};
        const std::wstring asi = GtaPluginPath();
        if (GetFileAttributesW(asi.c_str()) == INVALID_FILE_ATTRIBUTES)
            c.note = L"not found: " + asi;
        else
            c.ok = true;
        out.push_back(c);
    }
    // 4. GTA San Andreas.
    {
        SetupCheck c{L"GTA San Andreas (gta_sa.exe)", false, false, L""};
        const std::wstring exe = g_gtaDir + L"\\gta_sa.exe";
        if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES)
            c.note = L"not found: " + exe;
        else
            c.ok = true;
        out.push_back(c);
    }
    // 5. the map itself (the map tool's output). Without it there are no rails at all.
    {
        SetupCheck c{L"SkateAndreas map built", false, false, L""};
        const std::wstring railList = g_modHome + L"\\extracted\\thaw_maps\\_railpool\\rails_in_map.csv";
        if (GetFileAttributesW(railList.c_str()) == INVALID_FILE_ATTRIBUTES)
            c.note = L"not built yet - press \"Build the map\" below (needs Python 3)";
        else
            c.ok = true;
        out.push_back(c);
    }
    return out;
}

// 8.40: a Python 3 to run the map tool with (defined with the map build below).
std::wstring FindPython();

// 8.38: --where — write down the folders the launcher and the plugins will use, then
// stop. Handy for checking that a downloaded copy points at its own folder and not at
// the one this project was built in.
int RunWhere()
{
    Log("where: launcher  = %ls", LauncherDir().c_str());
    Log("where: mod home  = %ls  (told to the games as SKATEANDREAS_DIR)", g_modHome.c_str());
    Log("where: logs      = %ls", mp::LogFolderW().c_str());
    Log("where: extracted = %s", mp::ExtractedFolderA().c_str());
    Log("where: GTA       = %ls", g_gtaDir.c_str());
    Log("where: reTHAWed  = %ls", g_rethawedDir.c_str());
    const std::wstring python = FindPython();
    Log("where: python    = %ls", python.empty() ? L"(none found - the map cannot be built)" : python.c_str());
    return 0;
}

// --selftest: write the checks to the log and stop. Starts nothing.
int RunSelfTest()
{
    const std::vector<SetupCheck> checks = RunChecks();
    bool all = true;
    for (const SetupCheck& c : checks) {
        Log("check: [%s] %s%s", !c.ok ? "--" : (c.warn ? "WARN" : "OK"), Narrow(c.label.c_str()).c_str(),
            c.note.empty() ? "" : (" - " + Narrow(c.note.c_str())).c_str());
        if (!c.ok) all = false;
    }
    Log("selftest: %s", all ? "all checks passed" : "one or more checks FAILED");
    return all ? 0 : 1;
}

// ---------- the window ----------

void SetStatus(const wchar_t* text)
{
    Log("launcher: %s", Narrow(text).c_str());
    EnterCriticalSection(&g_cs);
    g_status = text;
    LeaveCriticalSection(&g_cs);
    if (g_hwnd) PostMessageW(g_hwnd, WM_MP_STATUS, 0, 0);
}

void RefreshChecks()
{
    const std::vector<SetupCheck> checks = RunChecks();
    for (int i = 0; i < kCheckCount && i < static_cast<int>(checks.size()); ++i) {
        const SetupCheck& c = checks[i];
        g_checkOk[i] = c.ok;
        g_checkWarn[i] = c.warn;
        const wchar_t* mark = !c.ok ? L"--   " : (c.warn ? L"!!   " : L"OK   ");
        std::wstring text = std::wstring(mark) + c.label;
        if (!c.note.empty() && (!c.ok || c.warn)) text += L"   (" + c.note + L")";
        SetWindowTextW(g_checkHwnd[i], text.c_str());
        InvalidateRect(g_checkHwnd[i], nullptr, TRUE);
    }
}

// 8.40: a Python 3 to run the map tool with. The release ships an embeddable one under
// tools\python, so a player needs nothing installed; if that cannot run (a 32-bit Windows, say)
// a Python from PATH is used instead. Returns the command to run, or "" when there is none.
std::wstring FindPython()
{
    std::vector<std::wstring> commands;
    commands.push_back(L"\"" + g_modHome + L"\\tools\\python\\python.exe\"");
    commands.push_back(L"python");
    commands.push_back(L"python3");
    commands.push_back(L"py -3"); // the Windows Python launcher, when only that is installed

    for (const std::wstring& exe : commands) {
        std::wstring cmd = exe + L" --version";
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                            &pi))
            continue;
        WaitForSingleObject(pi.hProcess, 10000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (code == 0) {
            Log("build map: using python = %ls", exe.c_str());
            return exe;
        }
    }
    return L"";
}

// 8.39: build the map from the player's own GTA (the map tool), in its own console window so
// they can watch the progress. Then say what to do with the result. The map cannot be shipped
// with the mod - it is GTA's own collision, converted (DESIGN 8.39).
void RunMapBuild()
{
    const std::wstring python = FindPython();
    if (python.empty()) {
        Log("build map: no python found (no tools\\python, and none on PATH)");
        SetStatus(L"No Python 3 found. The release normally carries its own in tools\\python - if it "
                  L"is missing, install Python 3 from python.org (tick \"Add Python to PATH\").");
        return;
    }
    const std::wstring tool = g_modHome + L"\\tools\\gta_to_thaw_map.py";
    if (GetFileAttributesW(tool.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("build map: %ls is missing", tool.c_str());
        SetStatus(L"tools\\gta_to_thaw_map.py is missing - it must sit next to the launcher.");
        return;
    }

    const std::wstring cmd =
        python + L" \"" + tool + L"\" \"" + g_gtaDir +
        L"\" --box -3000 -3000 3000 3000 --no-geometry-nodes --rail-pool 4500 --rail-drop-max 0"
        L" --name \"San Andreas (SkateAndreas)\" --folder \"SkateAndreas San Andreas\"";
    Log("build map: %ls", cmd.c_str());
    SetStatus(L"Building the map - watch the new window. This takes a few minutes.");

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    std::wstring mutableCmd = cmd;
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr,
                        g_modHome.c_str(), &si, &pi)) {
        Log("build map: could not start it (Windows error %lu)", GetLastError());
        SetStatus(L"Could not start the map tool. See the logs folder.");
        return;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    Log("build map: finished, exit code %lu", code);

    if (code != 0) {
        SetStatus(L"The map build stopped with an error - the window that just closed shows where.");
        return;
    }
    SetStatus(L"Map built. Copy its folder from extracted\\thaw_maps\\ into reTHAWed's data\\mod\\UserMods\\Maps\\ "
              L"- the folder is open for you.");
    const std::wstring maps = g_modHome + L"\\extracted\\thaw_maps";
    ShellExecuteW(nullptr, L"open", maps.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

DWORD WINAPI BuildMapThread(LPVOID)
{
    RunMapBuild();
    if (g_hwnd) PostMessageW(g_hwnd, WM_MP_DONE, 0, 0);
    return 0;
}

// One line of the launch sequence, run on the worker thread.
void RunSequence()
{
    const std::wstring gameDir = g_rethawedDir;
    const std::wstring exePath = gameDir + L"\\reTHAWed.exe";
    const std::wstring dllPath = PluginPath();

    SetStatus(L"Setting up the controller...");
    if (SetEnvironmentVariableW(kSdlIgnoreVar, kSdlIgnoreValue))
        Log("controller fix: %ls=%ls (reTHAWed ignores Xbox 360 controllers, incl. DualSenseX's virtual one)",
            kSdlIgnoreVar, kSdlIgnoreValue);
    if (SetEnvironmentVariableW(kSdlRawInputVar, kSdlRawInputValue))
        Log("controller fix: %ls=%ls (SDL's raw input controller reading off)", kSdlRawInputVar, kSdlRawInputValue);

    SetStatus(g_startHidden ? L"Starting reTHAWed (minimised)..." : L"Starting reTHAWed...");
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    if (g_startHidden) {
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOWMINNOACTIVE;
    }
    std::wstring cmdLine = L"\"" + exePath + L"\"";
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exePath.c_str(), cmdLine.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        gameDir.c_str(), &si, &pi)) {
        Log("ERROR: could not start reTHAWed.exe (Windows error %lu)", GetLastError());
        SetStatus(L"Could not start reTHAWed. See the logs folder.");
        return;
    }
    const ULONGLONG launchTick = GetTickCount64();
    Log("started reTHAWed.exe (paused), process id %lu", pi.dwProcessId);
    PatchSoundFocus(pi.hProcess);
    if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1))
        Log("ERROR: could not resume reTHAWed (Windows error %lu)", GetLastError());

    // Wait for reTHAWed's own code (rthw.dll).
    SetStatus(L"Waiting for reTHAWed to come up...");
    std::vector<std::wstring> modules;
    bool found = false;
    while (GetTickCount64() - launchTick < kRthwWaitMs) {
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            Log("ERROR: reTHAWed closed before rthw.dll was seen (exit code %lu)", code);
            break;
        }
        if (ListModules(pi.dwProcessId, modules) && HasModule(modules, L"rthw.dll")) {
            found = true;
            break;
        }
        Sleep(kPollMs);
    }
    if (!found) {
        Log("ERROR: rthw.dll not seen within %u ms; closing reTHAWed again", kRthwWaitMs);
        SetStatus(L"reTHAWed did not start properly. See the logs folder.");
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return;
    }
    Log("rthw.dll found %.3f s after start", (GetTickCount64() - launchTick) / 1000.0);

    SetStatus(L"Loading the plugin into reTHAWed...");
    if (InjectPlugin(pi.hProcess, dllPath)) {
        if (ListModules(pi.dwProcessId, modules) && HasModule(modules, L"SkateAndreas-thaw.dll"))
            Log("confirmed: SkateAndreas-thaw.dll is loaded in reTHAWed");
        else
            Log("WARNING: plugin reported loaded but is not in the module list");
    } else {
        Log("ERROR: our plugin was not loaded; reTHAWed keeps running without it");
        SetStatus(L"The plugin did not load into reTHAWed. See the logs folder.");
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return;
    }

    // Start GTA San Andreas.
    const std::wstring gtaDir = g_gtaDir;
    const std::wstring gtaExe = gtaDir + L"\\gta_sa.exe";
    SetStatus(L"Starting GTA San Andreas...");
    std::wstring gtaCmd = L"\"" + gtaExe + L"\"";
    STARTUPINFOW gsi = {};
    gsi.cb = sizeof(gsi);
    PROCESS_INFORMATION gp = {};
    if (!CreateProcessW(gtaExe.c_str(), gtaCmd.data(), nullptr, nullptr, FALSE, 0, nullptr, gtaDir.c_str(), &gsi, &gp)) {
        Log("ERROR: could not start gta_sa.exe (Windows error %lu)", GetLastError());
        SetStatus(L"Could not start GTA San Andreas. See the logs folder.");
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return;
    }
    Log("started gta_sa.exe, process id %lu", gp.dwProcessId);
    SetStatus(L"Both games are running. Close GTA when you are done.");

    // Wait for GTA, then close reTHAWed (we started it, and it may be hidden).
    WaitForSingleObject(gp.hProcess, INFINITE);
    DWORD gtaCode = 0;
    GetExitCodeProcess(gp.hProcess, &gtaCode);
    Log("gta_sa.exe closed, exit code %lu", gtaCode);
    CloseHandle(gp.hThread);
    CloseHandle(gp.hProcess);

    if (WaitForSingleObject(pi.hProcess, 2000) == WAIT_TIMEOUT) {
        SetStatus(L"Closing reTHAWed...");
        Log("reTHAWed still running 2 s after GTA closed; closing it (we started it)");
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    DWORD thawCode = 0;
    GetExitCodeProcess(pi.hProcess, &thawCode);
    Log("reTHAWed closed, exit code %lu", thawCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    SetStatus(L"Done. Click Play to start again.");
}

DWORD WINAPI WorkerThread(LPVOID)
{
    RunSequence();
    if (g_hwnd) PostMessageW(g_hwnd, WM_MP_DONE, 0, 0);
    return 0;
}

HFONT g_font = nullptr;
HFONT g_fontBold = nullptr;
HFONT g_fontTitle = nullptr;

HWND MakeControl(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT font)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    if (c && font) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return c;
}

void LayoutControls()
{
    MakeControl(L"STATIC", L"Tony Hawk's SkateAndreas", SS_LEFT, 20, 16, 520, 30, -1, g_fontTitle);
    MakeControl(L"STATIC", L"Skate in GTA San Andreas with reTHAWed's skating.", SS_LEFT, 20, 48, 520, 20, -1,
                g_font);

    // 8.36: the game folders, so a player on another PC can point them out.
    MakeControl(L"STATIC", L"Game folders:", SS_LEFT, 20, 78, 520, 18, -1, g_fontBold);
    MakeControl(L"STATIC", L"GTA San Andreas:", SS_LEFT, 28, 100, 116, 18, -1, g_font);
    g_gtaPathHwnd = MakeControl(L"STATIC", L"", SS_LEFT, 148, 100, 300, 18, IDC_GTA_PATH, g_font);
    MakeControl(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 456, 98, 80, 22, IDC_BROWSE_GTA, g_font);
    MakeControl(L"STATIC", L"reTHAWed:", SS_LEFT, 28, 126, 116, 18, -1, g_font);
    g_thawPathHwnd = MakeControl(L"STATIC", L"", SS_LEFT, 148, 126, 300, 18, IDC_THAW_PATH, g_font);
    MakeControl(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 456, 124, 80, 22, IDC_BROWSE_THAW, g_font);

    MakeControl(L"STATIC", L"Setup:", SS_LEFT, 20, 156, 520, 18, -1, g_fontBold);
    for (int i = 0; i < kCheckCount; ++i)
        g_checkHwnd[i] = MakeControl(L"STATIC", L"...", SS_LEFT, 28, 178 + i * 20, 508, 18, IDC_CHECK0 + i, g_font);
    g_hiddenHwnd = MakeControl(L"BUTTON", L"Start reTHAWed minimised (only GTA on screen)",
                               BS_AUTOCHECKBOX | WS_TABSTOP, 24, 284, 508, 22, IDC_HIDDEN, g_font);
    MakeControl(L"STATIC", L"Status:", SS_LEFT, 20, 314, 520, 18, -1, g_fontBold);
    g_statusHwnd = MakeControl(L"STATIC", L"", SS_LEFT, 28, 334, 508, 56, IDC_STATUS, g_font);
    g_playHwnd = MakeControl(L"BUTTON", L"Play", BS_PUSHBUTTON | WS_TABSTOP, 24, 400, 240, 46, IDC_PLAY, g_fontBold);
    // 8.39: make the map from GTA's collision, for players who do not want a command prompt.
    g_buildHwnd = MakeControl(L"BUTTON", L"Build the map...", BS_PUSHBUTTON | WS_TABSTOP, 280, 400, 200, 22,
                              IDC_BUILDMAP, g_font);
    MakeControl(L"BUTTON", L"Open logs folder", BS_PUSHBUTTON | WS_TABSTOP, 280, 424, 200, 22, IDC_LOGS, g_font);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        LayoutControls();
        ShowPaths();
        RefreshChecks();
        SetStatus(L"Ready. Click Play to start.");
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND ctl = reinterpret_cast<HWND>(lParam);
        for (int i = 0; i < kCheckCount; ++i) {
            if (ctl == g_checkHwnd[i]) {
                // green = fine, amber = warning (Play still allowed), red = Play blocked
                const COLORREF colour = !g_checkOk[i] ? RGB(190, 0, 0) : (g_checkWarn[i] ? RGB(160, 110, 0) : RGB(0, 120, 0));
                SetTextColor(dc, colour);
                SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
        }
        SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }

    case WM_MP_STATUS: {
        EnterCriticalSection(&g_cs);
        const std::wstring s = g_status;
        LeaveCriticalSection(&g_cs);
        if (g_statusHwnd) SetWindowTextW(g_statusHwnd, s.c_str());
        return 0;
    }

    case WM_MP_DONE:
        g_running = false;
        if (g_playHwnd) EnableWindow(g_playHwnd, TRUE);
        if (g_buildHwnd) EnableWindow(g_buildHwnd, TRUE);
        RefreshChecks(); // the map check may have just gone green
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_PLAY: {
            if (g_running) break;
            bool ok = true;
            for (int i = 0; i < kCheckCount; ++i)
                if (!g_checkOk[i]) ok = false;
            if (!ok) {
                // 8.42: name what is wrong instead of just "some checks failed", so a player
                // with the wrong reTHAWed build reads it in one go (DESIGN 11).
                std::wstring msg = L"Fix these first:";
                for (const SetupCheck& c : RunChecks()) {
                    if (c.ok) continue;
                    msg += L"\n - " + c.label;
                    if (!c.note.empty()) msg += L" - " + c.note;
                }
                SetStatus(msg.c_str());
                break;
            }
            g_startHidden = (IsDlgButtonChecked(hwnd, IDC_HIDDEN) == BST_CHECKED);
            g_running = true;
            if (g_playHwnd) EnableWindow(g_playHwnd, FALSE);
            SetStatus(L"Starting...");
            CloseHandle(CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr));
            break;
        }
        case IDC_BUILDMAP: {
            if (g_running) break;
            g_running = true;
            if (g_playHwnd) EnableWindow(g_playHwnd, FALSE);
            if (g_buildHwnd) EnableWindow(g_buildHwnd, FALSE);
            SetStatus(L"Looking for Python 3...");
            CloseHandle(CreateThread(nullptr, 0, BuildMapThread, nullptr, 0, nullptr));
            break;
        }
        case IDC_LOGS: {
            const std::wstring dir = mp::LogFolderW();
            ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case IDC_BROWSE_GTA: {
            std::wstring pick = g_gtaDir;
            if (BrowseForFolder(hwnd, pick)) {
                g_gtaDir = pick;
                SaveSettings();
                ShowPaths();
                Log("launcher: GTA folder set to %ls", g_gtaDir.c_str());
            }
            RefreshChecks();
            break;
        }
        case IDC_BROWSE_THAW: {
            std::wstring pick = g_rethawedDir;
            if (BrowseForFolder(hwnd, pick)) {
                g_rethawedDir = pick;
                SaveSettings();
                ShowPaths();
                Log("launcher: reTHAWed folder set to %ls", g_rethawedDir.c_str());
            }
            RefreshChecks();
            break;
        }
        default:
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int RunGui(HINSTANCE inst)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // for the "pick a folder" window
    g_font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontBold = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontTitle = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"SkateAndreasLauncher";
    if (!RegisterClassExW(&wc)) {
        Log("ERROR: could not register the window class (Windows error %lu)", GetLastError());
        return 1;
    }

    RECT rect = {0, 0, 560, 480};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rect, style, FALSE);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Tony Hawk's SkateAndreas", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        Log("ERROR: could not create the window (Windows error %lu)", GetLastError());
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) { // Tab between the controls
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}

bool HasArg(const wchar_t* arg)
{
    const wchar_t* line = GetCommandLineW();
    return line && wcsstr(line, arg) != nullptr;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
{
    g_startTick = GetTickCount64();
    InitializeCriticalSection(&g_cs);

    // 8.36: the folders start at the ones the build was made with, then whatever the
    // player last chose (SkateAndreas.ini next to this launcher).
    g_gtaDir = Widen(MP_GTA_DIR);
    g_rethawedDir = Widen(MP_RETHAWED_DIR);
    LoadSettings();

    // 8.38: tell the games (and so our plugins) where the mod lives, so a copy that was
    // downloaded works wherever it was unzipped. Both games are started from here and
    // inherit it. See src/common/paths.h.
    g_modHome = ModHome();
    SetEnvironmentVariableW(L"SKATEANDREAS_DIR", g_modHome.c_str());

    const std::wstring logDir = mp::LogFolderW();
    CreateDirectoryW(logDir.c_str(), nullptr); // fine if it already exists
    const std::wstring logPath = logDir + L"\\launcher.log";
    // Append, with a run header, so a user can send us one file with the history.
    // Open it shareable (deny none) so we can still read it while the window is open.
    g_log = _wfsopen(logPath.c_str(), L"a", _SH_DENYNO);
    if (g_log) {
        SYSTEMTIME st = {};
        GetLocalTime(&st);
        fprintf(g_log, "\n===== launcher run %04u-%02u-%02u %02u:%02u:%02u =====\n", st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond);
        fflush(g_log);
    } else {
        g_log = nullptr;
        printf("warning: could not open %ls\n", logPath.c_str());
    }

    const int result = HasArg(L"--where") ? RunWhere() : (HasArg(L"--selftest") ? RunSelfTest() : RunGui(inst));
    if (g_log) fclose(g_log);
    return result;
}
