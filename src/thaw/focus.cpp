// my-passthrough: keep reTHAWed running when its window isn't in front (phase 3-1).
// See focus.h.

#include <windows.h>
#include "focus.h"

#include <cstring>

namespace mp::thaw {
namespace {

// What we swallow. Index into g_swallowed.
enum Swallowed { kActivate = 0, kActivateApp, kKillFocus, kNcActivate, kSwallowKinds };
const char* const kSwallowedNames[kSwallowKinds] = {
    "WM_ACTIVATE (inactive)", "WM_ACTIVATEAPP (false)", "WM_KILLFOCUS", "WM_NCACTIVATE (false)"};

volatile LONG g_swallowed[kSwallowKinds] = {};
volatile LONG g_passedActivate = 0; // "you got focus" messages passed on

HWND g_window = nullptr;
WNDPROC volatile g_original = nullptr;
volatile bool g_unicode = true;
bool g_warnedReplaced = false;
ULONGLONG g_nextSearch = 0;
unsigned g_searches = 0;

LRESULT CALLBACK KeepFocusProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) {
            InterlockedIncrement(&g_swallowed[kActivate]);
            return 0;
        }
        InterlockedIncrement(&g_passedActivate);
        break;
    case WM_ACTIVATEAPP:
        if (!w) {
            InterlockedIncrement(&g_swallowed[kActivateApp]);
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        InterlockedIncrement(&g_swallowed[kKillFocus]);
        return 0;
    case WM_NCACTIVATE:
        if (!w) {
            InterlockedIncrement(&g_swallowed[kNcActivate]);
            return TRUE; // "go ahead", without drawing the title bar as inactive
        }
        break;
    default:
        break;
    }
    return g_unicode ? CallWindowProcW(g_original, h, msg, w, l)
                     : CallWindowProcA(g_original, h, msg, w, l);
}

// ---- finding the window ----

struct Candidate {
    HWND hwnd;
    long area;
};

struct SearchState {
    DWORD pid;
    Candidate best;
    FocusLogFn log;
    bool logEach;
};

BOOL CALLBACK EnumProc(HWND h, LPARAM p)
{
    SearchState& s = *reinterpret_cast<SearchState*>(p);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != s.pid) return TRUE;

    char cls[128] = {};
    GetClassNameA(h, cls, sizeof(cls));
    char title[128] = {};
    GetWindowTextA(h, title, sizeof(title));
    RECT r = {};
    GetWindowRect(h, &r);
    const long area = (r.right - r.left) * (r.bottom - r.top);
    const bool visible = IsWindowVisible(h) != FALSE;
    const bool owned = GetWindow(h, GW_OWNER) != nullptr;
    if (s.logEach)
        s.log("focus: window 0x%08lX class \"%s\" title \"%s\" %ldx%ld %s%s",
              static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(h)), cls, title,
              r.right - r.left, r.bottom - r.top, visible ? "visible" : "hidden",
              owned ? ", owned" : "");
    if (visible && !owned && area > s.best.area) s.best = Candidate{h, area};
    return TRUE;
}

// ---- reading THAW's focus flags (read only) ----

struct Flags {
    bool ok = false;
    int focused = -1;  // 0x008A35D5
    int other = -1;    // 0x0072E850
};

// The WM_ACTIVATE handler that writes the flags (reTHAWed.exe 0x00543D59):
//   setne al; test al,al; mov [0x008A35D5],al
// and 0x00543D6F: mov [0x0072E850],al
const unsigned char kHandlerBytes[] = {0x0F, 0x95, 0xC0, 0x84, 0xC0, 0xA2, 0xD5, 0x35, 0x8A, 0x00};
const unsigned char kHandlerBytes2[] = {0xA2, 0x50, 0xE8, 0x72, 0x00};

// No C++ objects with destructors in here: __try/__except needs that.
bool SameBytes(unsigned long address, const unsigned char* bytes, unsigned length)
{
    __try {
        return std::memcmp(reinterpret_cast<const void*>(address), bytes, length) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadFlagBytes(int& focused, int& other)
{
    __try {
        focused = *reinterpret_cast<volatile unsigned char*>(0x008A35D5);
        other = *reinterpret_cast<volatile unsigned char*>(0x0072E850);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int g_flagsChecked = 0; // 0 = not yet, 1 = code matches, -1 = different (don't read)
Flags g_lastFlags;
LONG g_lastSwallowed[kSwallowKinds] = {};
LONG g_lastPassed = 0;

void TickFlags(FocusLogFn log)
{
    if (g_flagsChecked == 0) {
        const bool a = SameBytes(0x00543D59, kHandlerBytes, sizeof(kHandlerBytes));
        const bool b = SameBytes(0x00543D6F, kHandlerBytes2, sizeof(kHandlerBytes2));
        g_flagsChecked = (a && b) ? 1 : -1;
        log("code check 0x00543D59 / 0x00543D6F (WM_ACTIVATE handler writing the focus flags): %s / %s%s",
            a ? "MATCH" : "DIFFERENT", b ? "MATCH" : "DIFFERENT",
            g_flagsChecked == 1 ? "; logging the focus flags" : "; not reading the focus flags");
    }
    if (g_flagsChecked != 1) return;
    Flags f;
    f.ok = ReadFlagBytes(f.focused, f.other);
    if (!f.ok) return;
    if (!g_lastFlags.ok || f.focused != g_lastFlags.focused || f.other != g_lastFlags.other) {
        log("focus: THAW focus flags: 0x008A35D5 = %d, 0x0072E850 = %d", f.focused, f.other);
        g_lastFlags = f;
    }
}

void Install(HWND h, FocusLogFn log)
{
    // Remember the original procedure BEFORE switching: the game's thread may call
    // ours the moment it is installed, and ours must already know where to pass on.
    g_unicode = IsWindowUnicode(h) != FALSE;
    const LONG_PTR before = g_unicode ? GetWindowLongPtrW(h, GWLP_WNDPROC) : GetWindowLongPtrA(h, GWLP_WNDPROC);
    if (before == 0) {
        log("focus: could not read the window procedure (Windows error %lu); reTHAWed will still pause behind GTA",
            GetLastError());
        return;
    }
    g_original = reinterpret_cast<WNDPROC>(before);
    SetLastError(0);
    const LONG_PTR previous =
        g_unicode ? SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&KeepFocusProc))
                  : SetWindowLongPtrA(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&KeepFocusProc));
    const DWORD error = GetLastError();
    if (previous == 0 && error != 0) {
        log("focus: could not wrap the window procedure (Windows error %lu); reTHAWed will still pause behind GTA",
            error);
        return;
    }
    if (previous != before) {
        // Changed in the tiny gap between reading and switching; pass on to the newest one.
        g_original = reinterpret_cast<WNDPROC>(previous);
        log("focus: window procedure changed while wrapping (0x%08lX -> 0x%08lX); using the newer one",
            static_cast<unsigned long>(before), static_cast<unsigned long>(previous));
    }
    g_window = h;
    g_warnedReplaced = false;
    log("focus: wrapped window 0x%08lX (%s); original procedure 0x%08lX. \"Lost focus\" messages are now swallowed",
        static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(h)), g_unicode ? "Unicode" : "ANSI",
        static_cast<unsigned long>(previous));
}

void TickWindow(FocusLogFn log)
{
    if (g_window) {
        if (!IsWindow(g_window)) {
            log("focus: wrapped window is gone; looking for reTHAWed's window again");
            g_window = nullptr;
            g_original = nullptr;
        } else {
            const LONG_PTR now = g_unicode ? GetWindowLongPtrW(g_window, GWLP_WNDPROC)
                                           : GetWindowLongPtrA(g_window, GWLP_WNDPROC);
            if (now != reinterpret_cast<LONG_PTR>(&KeepFocusProc) && !g_warnedReplaced) {
                // Someone wrapped it after us; their procedure still calls ours. Don't
                // re-wrap (that could make the two call each other forever).
                log("focus: window procedure changed after ours (now 0x%08lX); leaving it",
                    static_cast<unsigned long>(now));
                g_warnedReplaced = true;
            }
            return;
        }
    }

    const ULONGLONG tick = GetTickCount64();
    if (tick < g_nextSearch) return;
    g_nextSearch = tick + 500;
    ++g_searches;

    SearchState s{GetCurrentProcessId(), Candidate{nullptr, 0}, log, false};
    EnumWindows(&EnumProc, reinterpret_cast<LPARAM>(&s));
    if (!s.best.hwnd) {
        if (g_searches == 1 || g_searches % 20 == 0)
            log("focus: no visible reTHAWed window yet (%u searches)", g_searches);
        return;
    }
    // Found one: list every window of ours once, for the record, then wrap the biggest.
    s.logEach = true;
    s.best = Candidate{nullptr, 0};
    EnumWindows(&EnumProc, reinterpret_cast<LPARAM>(&s));
    if (s.best.hwnd) Install(s.best.hwnd, log);
}

} // namespace

void FocusKeeperTick(FocusLogFn log)
{
    TickWindow(log);
    TickFlags(log);
}

void FocusKeeperSecond(FocusLogFn log)
{
    bool changed = InterlockedCompareExchange(&g_passedActivate, 0, 0) != g_lastPassed;
    LONG now[kSwallowKinds];
    for (int i = 0; i < kSwallowKinds; ++i) {
        now[i] = InterlockedCompareExchange(&g_swallowed[i], 0, 0);
        if (now[i] != g_lastSwallowed[i]) changed = true;
    }
    if (!changed) return;
    g_lastPassed = InterlockedCompareExchange(&g_passedActivate, 0, 0);
    log("focus: swallowed so far: %s %ld, %s %ld, %s %ld, %s %ld | \"got focus\" passed on: %ld",
        kSwallowedNames[0], now[0], kSwallowedNames[1], now[1], kSwallowedNames[2], now[2],
        kSwallowedNames[3], now[3], g_lastPassed);
    for (int i = 0; i < kSwallowKinds; ++i) g_lastSwallowed[i] = now[i];
}

} // namespace mp::thaw
