// SkateAndreas: F8 = move CJ to the map waypoint (test helper). See teleport.h.
//
// How (all GTA's own functions, via plugin-sdk):
//   - the waypoint: FrontEndMenuManager.m_nTargetBlipIndex (0 = none), turned
//     into a radar slot with CRadar::GetActualBlipArrayIndex, position in
//     CRadar::ms_RadarTrace[slot].m_vecPos (only x and y are meaningful)
//   - CStreaming::LoadScene loads the area's models, LoadSceneCollision its
//     collision (may freeze the game for a moment)
//   - CWorld::FindGroundZFor3DCoord from 1000 m down gives the ground height
//     there AND says whether it found any
//   - CEntity::Teleport moves CJ to 1 m above that ground
//
// 8.31 D3 (2026-10-10): the first version asked for the ground right after
// LoadScene only. With the area's collision not loaded yet GTA answers a
// default height, so in San Fierro's hills CJ was put under the ground, fell
// through the map, and the game crashed (user's test). Now: collision loaded
// too, and CJ is only moved once real ground was found (tried again every
// frame for up to 3 s).

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
constexpr float kAboveGround = 1.0f;   // CJ's position is about 1 m above his feet
constexpr float kSearchFrom = 1000.0f; // look for the ground from this high down
constexpr ULONGLONG kTryMs = 3000;     // keep trying this long for the ground

bool g_keyWasDown = false;

// a teleport waiting for the target's collision
bool g_pending = false;
float g_targetX = 0.0f, g_targetY = 0.0f;
ULONGLONG g_until = 0;
unsigned g_tries = 0;

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

// Load the target area and look for ground; move CJ if found. True = finished
// (moved), false = try again next frame.
bool TryTeleport(CPlayerPed* ped, LogFn log)
{
    ++g_tries;
    CVector at(g_targetX, g_targetY, 50.0f);
    CStreaming::LoadScene(&at);
    CStreaming::LoadSceneCollision(&at);
    bool found = false;
    const float groundZ = CWorld::FindGroundZFor3DCoord(g_targetX, g_targetY, kSearchFrom, &found, nullptr);
    if (!found) return false;
    const CVector from = ped->GetPosition();
    const CVector to(g_targetX, g_targetY, groundZ + kAboveGround);
    ped->Teleport(to, false);
    ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    const CVector now = ped->GetPosition();
    log("teleport (F8): waypoint %.2f %.2f | ground %.2f (found on try %u) | CJ from %.2f %.2f %.2f to %.2f %.2f %.2f "
        "(now reads %.2f %.2f %.2f)",
        g_targetX, g_targetY, groundZ, g_tries, from.x, from.y, from.z, to.x, to.y, to.z, now.x, now.y, now.z);
    return true;
}

void TeleportFrame(LogFn log)
{
    const bool keyDown = (GetAsyncKeyState(kTeleportKey) & 0x8000) != 0;
    const bool pressed = keyDown && !g_keyWasDown && GtaIsForeground();
    g_keyWasDown = keyDown;

    CPlayerPed* ped = FindPlayerPed();
    if (g_pending) {
        if (const char* b = Blocker(ped)) {
            g_pending = false;
            log("teleport (F8): stopped: %s", b);
            return;
        }
        if (TryTeleport(ped, log)) {
            g_pending = false;
        } else if (GetTickCount64() > g_until) {
            g_pending = false;
            log("teleport (F8): not done: no ground found at %.2f %.2f after %u tries (%.1f s); CJ stays where he is",
                g_targetX, g_targetY, g_tries, kTryMs / 1000.0);
        }
        return;
    }
    if (!pressed) return;

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
    g_targetX = marker.x;
    g_targetY = marker.y;
    g_tries = 0;
    g_until = GetTickCount64() + kTryMs;
    if (!TryTeleport(ped, log)) {
        g_pending = true; // collision not there yet: try again next frames
        log("teleport (F8): waypoint %.2f %.2f: ground not loaded yet, trying for up to %.1f s", g_targetX,
            g_targetY, kTryMs / 1000.0);
    }
}

} // namespace

void TeleportFrameSafe(LogFn log)
{
    __try {
        TeleportFrame(log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_pending = false;
        log("teleport (F8): ERROR (exception 0x%08lX), nothing more done", GetExceptionCode());
    }
}

} // namespace mp::gta
