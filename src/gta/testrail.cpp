// SkateAndreas: 8.32 R3b test: show the F5 test rail. See testrail.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CCoronas.h"
#include "common.h"
#include <windows.h>

#include "../common/link.h"
#include "testrail.h"

namespace mp::gta {
namespace {

constexpr int kDots = 13;                  // one light about every 2 m along the 25 m rail
constexpr unsigned kIdBase = 0x4D503200u;  // our own corona ids (any number GTA doesn't use)

} // namespace

void TestRailFrame(LinkBlock* link, LogFn log)
{
    static bool shown = false;
    if (!link || !link->thawTestRailOn || !SkateModeIsOn()) {
        if (shown) log("test rail: hidden (no test rail, or skate mode off)");
        shown = false;
        return;
    }
    float a[3], b[3], d[3];
    for (int k = 0; k < 3; ++k) {
        a[k] = link->thawTestRail[k];
        b[k] = link->thawTestRail[3 + k];
        d[k] = b[k] - a[k];
    }
    float ga[3], gb[3], gd[3];
    if (!SkateModeThawToGta(a, d, ga, gd) || !SkateModeThawToGta(b, d, gb, gd)) return;
    for (int i = 0; i < kDots; ++i) {
        const float f = static_cast<float>(i) / (kDots - 1);
        const CVector p(ga[0] + (gb[0] - ga[0]) * f, ga[1] + (gb[1] - ga[1]) * f, ga[2] + (gb[2] - ga[2]) * f);
        CCoronas::RegisterCorona(kIdBase + i, nullptr, 255, 120, 0, 255, p, 0.35f, 200.0f, CORONATYPE_SHINYSTAR,
                                 FLARETYPE_NONE, false, false, 0, 0.0f, false, 0.5f, 0, 15.0f, false, false);
    }
    static float lastA[3] = {};
    if (!shown || lastA[0] != a[0] || lastA[1] != a[1] || lastA[2] != a[2]) {
        log("test rail: showing it in GTA from %.2f %.2f %.2f to %.2f %.2f %.2f", ga[0], ga[1], ga[2], gb[0], gb[1],
            gb[2]);
        for (int k = 0; k < 3; ++k) lastA[k] = a[k];
    }
    shown = true;
}

} // namespace mp::gta
