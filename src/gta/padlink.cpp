// my-passthrough: GTA's controller state into the link (phase 3-2). See padlink.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CPad.h"
#include <windows.h>

#include "padlink.h"
#include "radio.h"
#include "spawner.h"
#include "skatemode.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace mp::gta {
namespace {

constexpr uintptr_t kCallSite = 0x53BEE6;     // in CGame::Process: call CPad::UpdatePads
constexpr uintptr_t kUpdatePads = 0x541DD0;   // GTA's own CPad::UpdatePads

using UpdatePadsFn = void(__cdecl*)();
UpdatePadsFn g_next = nullptr;                // what the call went to before us

LinkBlock* volatile g_link = nullptr;
bool g_tried = false;                         // install attempted (only once)
std::atomic<bool> g_installed{false};
std::atomic<LONG> g_published{0};
std::atomic<LONG> g_frames{0};                // our function's calls
std::atomic<LONG> g_lastButtons{0};
std::atomic<LONG> g_lastAxes[kAxisCount];
std::atomic<LONG> g_blanked{0};              // frames blanked (skate mode)
bool g_wasBlanking = false;
PadLogFn g_log = nullptr;

uintptr_t CallTarget(uintptr_t site)
{
    int32_t offset;
    std::memcpy(&offset, reinterpret_cast<const void*>(site + 1), sizeof(offset));
    return site + 5 + static_cast<uintptr_t>(offset);
}

void LogBytes(PadLogFn log, const char* label)
{
    const unsigned char* b = reinterpret_cast<const unsigned char*>(kCallSite);
    if (b[0] == 0xE8)
        log("pad hook %s 0x%06X: %02X %02X %02X %02X %02X = call to 0x%08X", label,
            static_cast<unsigned>(kCallSite), b[0], b[1], b[2], b[3], b[4],
            static_cast<unsigned>(CallTarget(kCallSite)));
    else
        log("pad hook %s 0x%06X: %02X %02X %02X %02X %02X = NOT a call", label,
            static_cast<unsigned>(kCallSite), b[0], b[1], b[2], b[3], b[4]);
}

short Clamp16(int v)
{
    if (v < -32768) return -32768;
    if (v > 32767) return 32767;
    return static_cast<short>(v);
}

// Copy pad 0, as GTA just read it, into the link.
void Publish()
{
    LinkBlock* link = g_link;
    CPad* pad = CPad::GetPad(0);
    if (!pad) return;
    const CControllerState& s = pad->NewState;

    InputShared in;
    LONG b = 0;
    if (s.ButtonCross) b |= kBtnCross;
    if (s.ButtonCircle) b |= kBtnCircle;
    if (s.ButtonSquare) b |= kBtnSquare;
    if (s.ButtonTriangle) b |= kBtnTriangle;
    if (s.LeftShoulder1) b |= kBtnL1;
    if (s.RightShoulder1) b |= kBtnR1;
    if (s.ShockButtonL) b |= kBtnL3;
    if (s.ShockButtonR) b |= kBtnR3;
    if (s.Start) b |= kBtnStart;
    if (s.Select) b |= kBtnSelect;
    if (s.DPadUp) b |= kBtnUp;
    if (s.DPadDown) b |= kBtnDown;
    if (s.DPadLeft) b |= kBtnLeft;
    if (s.DPadRight) b |= kBtnRight;
    in.buttons = b;
    in.axes[kAxisLX] = Clamp16(s.LeftStickX);
    in.axes[kAxisLY] = Clamp16(s.LeftStickY);
    in.axes[kAxisRX] = Clamp16(s.RightStickX);
    in.axes[kAxisRY] = Clamp16(s.RightStickY);
    in.axes[kAxisL2] = Clamp16(s.LeftShoulder2);
    in.axes[kAxisR2] = Clamp16(s.RightShoulder2);
    // 8.27: Back hold = radio wheel (right stick picks), tap = passed on to reTHAWed
    RadioFilterPad(in.buttons, in.axes);

    g_lastButtons.store(b, std::memory_order_relaxed);
    for (int i = 0; i < kAxisCount; ++i) g_lastAxes[i].store(in.axes[i], std::memory_order_relaxed);

    if (!link) return;
    in.tick = static_cast<LONG>(GetTickCount64() & 0xFFFFFFFFu);
    in.count = g_published.fetch_add(1, std::memory_order_relaxed) + 1;
    PublishInput(link, in);
}

void PublishSafe()
{
    __try {
        Publish();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never let a copying problem take GTA down; the count shows it stopped.
    }
}

// Phase 3-4: all buttons up, sticks centred, for the rest of this frame.
void BlankSafe()
{
    __try {
        if (CPad* pad = CPad::GetPad(0)) std::memset(&pad->NewState, 0, sizeof(pad->NewState));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __cdecl OurUpdatePads()
{
    g_next(); // GTA reads the controller (through any mod chained before us)
    g_frames.fetch_add(1, std::memory_order_relaxed);
    PublishSafe();
    // 8.28 d: the vehicle list (when open, GTA doesn't get the controller)
    const bool spawnerHolds = SpawnerPadFilter();
    const bool blank = SkateModeIsOn();
    if (blank || spawnerHolds) BlankSafe();
    if (blank) g_blanked.fetch_add(1, std::memory_order_relaxed);
    if (blank != g_wasBlanking) {
        g_wasBlanking = blank;
        if (g_log)
            g_log(blank ? "pad: skate mode on: GTA's pad is now blanked every frame (controller goes to reTHAWed only)"
                        : "pad: skate mode off: GTA gets the controller again");
    }
}

void Install(PadLogFn log)
{
    LogBytes(log, "before");
    const unsigned char* b = reinterpret_cast<const unsigned char*>(kCallSite);
    if (b[0] != 0xE8) {
        log("pad hook: NOT installed (no call instruction at 0x%06X); controller not sent to reTHAWed",
            static_cast<unsigned>(kCallSite));
        return;
    }
    const uintptr_t target = CallTarget(kCallSite);
    HMODULE owner = nullptr;
    wchar_t ownerName[MAX_PATH] = L"(no loaded file)";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(target), &owner) && owner)
        GetModuleFileNameW(owner, ownerName, MAX_PATH);
    log("pad hook: the call goes to 0x%08X (%s), inside %ls", static_cast<unsigned>(target),
        target == kUpdatePads ? "GTA's own CPad::UpdatePads" : "NOT GTA's own; another mod, we chain after it",
        ownerName);
    g_next = reinterpret_cast<UpdatePadsFn>(target);

    // Point the call at ours: E8 + offset (address of ours - end of the call).
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(kCallSite), 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        log("pad hook: NOT installed (could not unprotect, Windows error %lu)", GetLastError());
        return;
    }
    const int32_t offset = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&OurUpdatePads) - (kCallSite + 5));
    std::memcpy(reinterpret_cast<void*>(kCallSite + 1), &offset, sizeof(offset));
    DWORD unused = 0;
    VirtualProtect(reinterpret_cast<void*>(kCallSite), 5, oldProtect, &unused);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(kCallSite), 5);
    g_installed.store(true);
    LogBytes(log, "after ");
}

void AppendButtons(char* out, size_t size, LONG buttons)
{
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < kButtonCount; ++i) {
        if (!(buttons & (1 << i))) continue;
        const char* name = ButtonName(i);
        const size_t len = std::strlen(name);
        if (used + len + 2 >= size) break;
        if (used) out[used++] = ' ';
        std::memcpy(out + used, name, len);
        used += len;
        out[used] = '\0';
    }
    if (!used && size > 4) std::memcpy(out, "none", 5);
}

} // namespace

void PadLinkFrame(LinkBlock* link, PadLogFn log)
{
    g_link = link;
    g_log = log;
    if (g_tried) return;
    g_tried = true; // first world update: every mod has loaded by now
    Install(log);
}

bool PadLinkRawButtons(LONG& buttons)
{
    if (!g_installed.load()) return false;
    buttons = g_lastButtons.load(std::memory_order_relaxed);
    return true;
}

void PadLinkSecond(PadLogFn log)
{
    static LONG lastPublished = 0, lastFrames = 0, lastBlanked = 0;
    static bool warned = false;
    if (!g_installed.load()) return;

    // Still ours? (Another mod could re-point the call later.)
    const uintptr_t now = CallTarget(kCallSite);
    if (now != reinterpret_cast<uintptr_t>(&OurUpdatePads) && !warned) {
        log("pad hook: the call at 0x%06X now goes to 0x%08X, not ours; controller no longer sent",
            static_cast<unsigned>(kCallSite), static_cast<unsigned>(now));
        warned = true;
    }

    const LONG published = g_published.load();
    const LONG frames = g_frames.load();
    char names[160];
    AppendButtons(names, sizeof(names), g_lastButtons.load());
    const LONG blanked = g_blanked.load();
    log("pad: %ld frames, %ld sent, %ld blanked in the last second | now: buttons %s | LX %ld LY %ld RX %ld RY %ld L2 %ld R2 %ld",
        frames - lastFrames, published - lastPublished, blanked - lastBlanked, names, g_lastAxes[kAxisLX].load(),
        g_lastAxes[kAxisLY].load(), g_lastAxes[kAxisRX].load(), g_lastAxes[kAxisRY].load(),
        g_lastAxes[kAxisL2].load(), g_lastAxes[kAxisR2].load());
    lastPublished = published;
    lastFrames = frames;
    lastBlanked = blanked;
}

} // namespace mp::gta
