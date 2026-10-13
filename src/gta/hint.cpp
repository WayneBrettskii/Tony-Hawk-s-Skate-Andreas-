// SkateAndreas: GTA's black message box hints. See hint.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CPlayerPed.h"
#include "CHud.h"
#include <windows.h>

#include "hint.h"
#include "bmxmode.h"

namespace mp::gta {
namespace {

// How often the BMX hint may come back while the player has not used the combo.
// (The skate hint does not repeat at all: the player has to load free skate by hand
// anyway, so they only need to be told the combo once per session.)
constexpr DWORD kHintRepeatMs = 60000;

DWORD g_lastHint = 0;
DWORD g_firstTick = 0;
bool g_skateHintShown = false; // once per run of the game (a plugin load is a session)

void Show(const char* text, LogFn log)
{
    // quick = false, permanent = false, addToBrief = false: the ordinary box, the
    // same one GTA uses for its own tips.
    CHud::SetHelpMessage(text, false, false, false);
    g_lastHint = GetTickCount();
    log("hint: showed \"%s\"", text);
}

} // namespace

void HintFrame(LogFn log)
{
    const DWORD now = GetTickCount();
    if (g_firstTick == 0) g_firstTick = now;
    if (now - g_firstTick < 10000) return;    // let the game finish coming up first
    if (CHud::HelpMessageDisplayed()) return; // never overwrite GTA's own message
    if (RideModeIsOn()) return;               // the pad belongs to reTHAWed; no hint needed

    CPlayerPed* ped = FindPlayerPed(0);
    if (!ped) return;

    if (BmxCjRidesBmx()) {
        if (now - g_lastHint < kHintRepeatMs) return; // on a bike: repeat slowly
        Show("Press L3+R3 to toggle BMX mode", log);
        return;
    }
    if (!ped->bInVehicle && !g_skateHintShown) { // on foot: once per session
        g_skateHintShown = true;
        Show("Press L3+R3 to Skate", log);
    }
}

} // namespace mp::gta
