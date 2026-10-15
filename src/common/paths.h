// SkateAndreas: where our files live on this PC (docs/DESIGN.md 8.38).
//
// The folder the project was compiled in used to be baked into the plugins (MP_LOG_DIR),
// which is fine on the machine that built it but breaks a copy someone downloaded: the
// logs would land in a folder that does not exist, and, worse, the rail pool could not
// find the rail list, so nothing would be grindable.
//
// The launcher now sets SKATEANDREAS_DIR to the mod's home folder and starts both games,
// so they inherit it. The plugins keep their logs in <home>\logs and look for the map
// tool's output in <home>\extracted. With no launcher (a build started by hand) they fall
// back to the folder the project was compiled in, which is what they always did.

#pragma once

#include <windows.h>
#include <string>

#ifndef MP_LOG_DIR
#error "MP_LOG_DIR must be set by CMakeLists.txt (it may be empty)"
#endif

// 8.44 F1b (audit, 2026-10-15): a release build is made with MP_LOG_DIR empty, so it carries
// no folder of the PC it was built on. Without the launcher such a build logs to
// %LOCALAPPDATA%\SkateAndreas\logs and has no "extracted" folder (the rail pool then
// stays off and says so). Development builds keep the project folder as before.

namespace mp {

// The mod's home folder, from the launcher. Empty when our plugins are loaded without it.
inline std::wstring HomeFolderW()
{
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"SKATEANDREAS_DIR", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring s = buf;
    while (!s.empty() && s.back() == L'\\') s.pop_back();
    return s;
}

// Where the logs go: <home>\logs, or the folder this project was compiled with when there
// is no home (a build started by hand, or one of our own tools).
inline std::wstring LogFolderW()
{
    const std::wstring home = HomeFolderW();
    if (!home.empty()) return home + L"\\logs";
    const std::wstring built = L"" MP_LOG_DIR;
    if (!built.empty()) return built;
    wchar_t local[MAX_PATH] = {}; // 8.44 F1b: release build started without the launcher
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    const std::wstring dir = std::wstring(local) + L"\\SkateAndreas";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\logs";
}

// The "extracted" folder the map tool writes (it holds the rail pool's csv files):
// <home>\extracted, or the compiled project's when there is no home.
inline std::string ExtractedFolderA()
{
    const std::wstring home = HomeFolderW();
    if (!home.empty()) {
        char buf[MAX_PATH] = {};
        if (WideCharToMultiByte(CP_ACP, 0, home.c_str(), -1, buf, MAX_PATH, nullptr, nullptr) > 0)
            return std::string(buf) + "\\extracted";
    }
    const std::string built = MP_LOG_DIR;
    if (built.empty()) return ""; // 8.44 F1b: release build without the launcher: no rail list
    return built + "\\..\\extracted";
}

// 8.44: reTHAWed's folder as the player set it in the launcher (SKATEANDREAS_RETHAWED_DIR,
// set before the games start), else the one this build was configured with. The GTA
// plugin reads reTHAWed's logo from it for the radio wheel; before this it only ever
// looked in the build PC's folder, so a player's copy never found the logo.
inline std::wstring RethawedFolderW(const wchar_t* builtIn)
{
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"SKATEANDREAS_RETHAWED_DIR", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring s = buf;
        while (!s.empty() && s.back() == L'\\') s.pop_back();
        if (!s.empty()) return s;
    }
    return builtIn ? builtIn : L"";
}

} // namespace mp
