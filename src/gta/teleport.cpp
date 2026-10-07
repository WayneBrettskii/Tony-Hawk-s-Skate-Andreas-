// my-passthrough: F8 = move CJ to the map waypoint (test helper). See teleport.h.
//
// How (all GTA's own functions, via plugin-sdk):
//   - the waypoint: FrontEndMenuManager.m_nTargetBlipIndex (0 = none), turned
//     into a radar slot with CRadar::GetActualBlipArrayIndex, position in
//     CRadar::ms_RadarTrace[slot].m_vecPos (only x and y are meaningful)
//   - CStreaming::LoadScene loads the area (may freeze the game for a moment)
//   - CWorld::FindGroundZForCoord gives the ground height there
//   - CEntity::Teleport moves CJ to 1 m above that ground

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CCamera.h"
#include "CCutsceneMgr.h"
#include "CMenuManager.h"
#include "CPad.h"
#include "CPlayerPed.h"
#include "CRadar.h"
#include "CStreaming.h"
#include "CTimer.h"
#include "CWorld.h"
#include "common.h"
#include <windows.h>

#include "teleport.h"
#include "skatemode.h"

namespace mp::gta {
namespace {

constexpr int kTeleportKey = VK_F8;
constexpr float kAboveGround = 1.0f; // CJ's position is about 1 m above his feet

bool g_keyWasDown = false;

bool GtaIsForeground()
{
    HWND w = GetForegroundWindow();
    if (!w) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

// Why CJ can't be teleported now (nullptr = he can).
const char* Blocker(CPlayerPed* ped)
{
    if (!ped) return "no player";
    if (ped->bInVehicle) return "CJ is in a vehicle (get out first)";
    if (ped->m_fHealth <= 0.0f) return "CJ is dead";
    if (CCutsceneMgr::ms_running) return "cutscene running";
    if (TheCamera.m_bWideScreenOn) return "scripted scene (black bars)";
    CPad* pad = CPad::GetPad(0);
    if (pad && pad->DisablePlayerControls) return "player controls are disabled (mission / script / skate mode)";
    if (CTimer::m_UserPause || CTimer::m_CodePause) return "GTA is paused";
    if (SkateModeIsOn()) return "skate mode is on (turn it off first)";
    return nullptr;
}

void TeleportFrame(LogFn log)
{
    const bool keyDown = (GetAsyncKeyState(kTeleportKey) & 0x8000) != 0;
    const bool pressed = keyDown && !g_keyWasDown && GtaIsForeground();
    g_keyWasDown = keyDown;
    if (!pressed) return;

    CPlayerPed* ped = FindPlayerPed();
    if (const char* b = Blocker(ped)) {
        log("teleport (F8): not done: %s", b);
        return;
    }
    const int handle = FrontEndMenuManager.m_nTargetBlipIndex;
    const int slot = handle ? CRadar::GetActualBlipArrayIndex(handle) : -1;
    if (slot < 0) {
        log("teleport (F8): not done: no waypoint on the map (place one in the pause menu map first)");
        return;
    }
    const CVector marker = CRadar::ms_RadarTrace[slot].m_vecPos;
    const CVector from = ped->GetPosition();

    CVector loadAt(marker.x, marker.y, from.z);
    CStreaming::LoadScene(&loadAt);
    const float groundZ = CWorld::FindGroundZForCoord(marker.x, marker.y);
    const CVector to(marker.x, marker.y, groundZ + kAboveGround);
    ped->Teleport(to, false);

    const CVector now = ped->GetPosition();
    log("teleport (F8): waypoint %.2f %.2f | ground %.2f | CJ from %.2f %.2f %.2f to %.2f %.2f %.2f "
        "(now reads %.2f %.2f %.2f)",
        marker.x, marker.y, groundZ, from.x, from.y, from.z, to.x, to.y, to.z, now.x, now.y, now.z);
}

} // namespace

void TeleportFrameSafe(LogFn log)
{
    __try {
        TeleportFrame(log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("teleport (F8): ERROR (exception 0x%08lX), nothing more done", GetExceptionCode());
    }
}

} // namespace mp::gta
