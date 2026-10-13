// SkateAndreas: even frame pacing for GTA. See pacer.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CMenuManager.h"
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

#include "pacer.h"

namespace mp::gta {
namespace {

constexpr int kFps = 60;               // frames a second
constexpr double kSpinMs = 1.5;        // last part of the wait: spin, don't sleep (Sleep isn't exact)

bool g_started = false;
long long g_freq = 0, g_next = 0;
int g_state = -1;                      // -1 unknown, 0 standing aside, 1 pacing
unsigned g_frames = 0, g_late = 0;
long long g_reportAt = 0;

long long Now()
{
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

} // namespace

void PacerFrame(LogFn log)
{
    if (!g_started) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_freq = f.QuadPart;
        timeBeginPeriod(1); // Sleep(1) = about 1 ms in this program
        g_started = true;
    }
    // plugin-sdk calls the byte at +0x4C "m_bPrefsVsync"; in GTA SA 1.0 it is
    // the Frame Limiter option of Display Setup.
    const int want = FrontEndMenuManager.m_bPrefsVsync ? 0 : 1;
    if (want != g_state) {
        log(want ? "pacer: GTA's frame limiter is off - pacing GTA to an even %d frames a second"
                 : "pacer: GTA's frame limiter is on - pacer standing aside (turn it off in Display Setup to use ours, %d fps)",
            kFps);
        g_state = want;
        g_next = 0;
    }
    if (!g_state) return;

    const long long period = g_freq / kFps;
    long long now = Now();
    if (g_next == 0 || now > g_next + 2 * period) {
        g_next = now + period; // first frame, or after a long hitch (loading): start fresh
        return;
    }
    if (now > g_next) ++g_late;
    const long long spin = static_cast<long long>(g_freq * kSpinMs / 1000.0);
    while (now < g_next) {
        if (g_next - now > spin) Sleep(1);
        else YieldProcessor();
        now = Now();
    }
    g_next += period;
    ++g_frames;
    if (now >= g_reportAt) {
        if (g_reportAt) log("pacer: %u frames in the last 10 s, %u late", g_frames, g_late);
        g_frames = g_late = 0;
        g_reportAt = now + g_freq * 10;
    }
}

} // namespace mp::gta
