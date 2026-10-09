// my-passthrough: F9 = log CJ's position as a "mark" (phase 4-0). See mark.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CPlayerPed.h"
#include "CWorld.h"
#include "common.h"
#include <windows.h>

#include "mark.h"

namespace mp::gta {
namespace {

constexpr int kMarkKey = VK_F9;
bool g_keyWasDown = false;
int g_count = 0;

bool GtaIsForeground()
{
    HWND w = GetForegroundWindow();
    if (!w) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

void MarkFrame(LogFn log)
{
    const bool down = (GetAsyncKeyState(kMarkKey) & 0x8000) != 0;
    const bool pressed = down && !g_keyWasDown;
    g_keyWasDown = down;
    if (!pressed || !GtaIsForeground()) return;

    CPlayerPed* ped = FindPlayerPed();
    if (!ped) {
        log("mark (F9): no player");
        return;
    }
    const CVector p = ped->GetPosition();
    bool found = false;
    const float ground = CWorld::FindGroundZFor3DCoord(p.x, p.y, p.z + 1.0f, &found, nullptr);
    ++g_count;
    if (found)
        log("mark #%d (F9): CJ at %.3f %.3f %.3f | ground below %.3f (feet about %.2f m below CJ's position) | heading %.1f deg | %s",
            g_count, p.x, p.y, p.z, ground, p.z - ground, ped->m_fHeadingCurrent * 57.29578f,
            ped->bInVehicle ? "IN A VEHICLE" : "on foot");
    else
        log("mark #%d (F9): CJ at %.3f %.3f %.3f | no ground found below | heading %.1f deg", g_count, p.x,
            p.y, p.z, ped->m_fHeadingCurrent * 57.29578f);
}

} // namespace

void MarkFrameSafe(LogFn log)
{
    __try {
        MarkFrame(log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("mark (F9): ERROR (exception 0x%08lX)", GetExceptionCode());
    }
}

} // namespace mp::gta
