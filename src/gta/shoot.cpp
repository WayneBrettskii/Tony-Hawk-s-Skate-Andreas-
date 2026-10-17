// SkateAndreas: shooting while skating, step 1 (DESIGN 8.53). See shoot.h.
//
// 8.53 step 1 (user 2026-10-17: "commit the plan and start step 1 (probe + one test shot)"):
// GTA's controller is blanked in skate mode, so GTA never fires by itself. Here our plugin asks
// GTA's own weapon code (CWeapon::Fire, 0x742300) to fire CJ's held gun once; GTA should do the
// bullet, damage, sound, flash, ammo and wanted level. This step only finds out how much of that
// happens without GTA's own firing task; nothing else changes.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CPlayerPed.h"
#include "CWeapon.h"
#include "CWeaponInfo.h"
#include "CCamera.h"
#include "CWorld.h"
#include "CColPoint.h"
#include "common.h"
#include <windows.h>

#include "shoot.h"
#include "skatemode.h"

namespace mp::gta {
namespace {

constexpr int kShotKey = VK_F12;      // F1-F11 are taken by our two plugins
constexpr int kRightWrist = 24;       // GTA ped bone (plugin-sdk ePedBones BONE_RIGHTWRIST)
constexpr float kAimRange = 60.0f;    // metres along the camera when the gun's own range is unknown
bool g_keyWasDown = false;
DWORD g_nextInfo = 0;
int g_shots = 0;

bool GtaIsForeground()
{
    HWND w = GetForegroundWindow();
    if (!w) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

const char* FireKind(unsigned int f)
{
    switch (f) {
    case WEAPON_FIRE_MELEE: return "melee";
    case WEAPON_FIRE_INSTANT_HIT: return "instant hit";
    case WEAPON_FIRE_PROJECTILE: return "projectile";
    case WEAPON_FIRE_AREA_EFFECT: return "area effect";
    case WEAPON_FIRE_CAMERA: return "camera";
    case WEAPON_FIRE_USE: return "use";
    default: return "?";
    }
}

const char* HitKind(CEntity* e)
{
    if (!e) return "nothing";
    switch (e->m_nType) {
    case ENTITY_TYPE_BUILDING: return "a building";
    case ENTITY_TYPE_VEHICLE: return "a vehicle";
    case ENTITY_TYPE_PED: return "a ped";
    case ENTITY_TYPE_OBJECT: return "an object";
    case ENTITY_TYPE_DUMMY: return "a dummy";
    default: return "something";
    }
}

// What CJ holds, once a second while skating (the gun model in his hand, the weapon, its ammo).
void LogHeld(CPlayerPed* ped, LogFn log)
{
    CWeapon* w = ped->GetWeapon();
    if (!w) return;
    CWeaponInfo* info = CWeaponInfo::GetWeaponInfo(w->m_eWeaponType, 1);
    log("shoot probe: CJ holds weapon %d in slot %d (%s), state %d, ammo %u in the clip / %u in all, gun model in hand: %s "
        "(model %d) | F12 = one test shot",
        static_cast<int>(w->m_eWeaponType), static_cast<int>(ped->m_nSelectedWepSlot), info ? FireKind(info->m_nWeaponFire) : "no info",
        static_cast<int>(w->m_nState), w->m_nAmmoInClip, w->m_nAmmoTotal, ped->m_pWeaponObject ? "yes" : "no", ped->m_nWeaponModelId);
}

// One shot of the held gun, from CJ's right wrist along the camera.
void TestShot(CPlayerPed* ped, LogFn log)
{
    ++g_shots;
    CWeapon* w = ped->GetWeapon();
    if (!w) {
        log("shoot test %d (F12): no weapon", g_shots);
        return;
    }
    CWeaponInfo* info = CWeaponInfo::GetWeaponInfo(w->m_eWeaponType, 1);
    if (!info || info->m_nWeaponFire != WEAPON_FIRE_INSTANT_HIT) {
        log("shoot test %d (F12): weapon %d is %s, not a gun that fires bullets: not fired (step 1 is guns only)", g_shots,
            static_cast<int>(w->m_eWeaponType), info ? FireKind(info->m_nWeaponFire) : "unknown");
        return;
    }
    RwV3d hand = {};
    ped->GetBonePosition(hand, kRightWrist, true);
    CVector origin(hand.x, hand.y, hand.z);
    const CVector front = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront;
    const float fl = front.Magnitude();
    if (fl < 0.1f) {
        log("shoot test %d (F12): the camera has no direction: not fired", g_shots);
        return;
    }
    const float range = (info->m_fWeaponRange > 1.0f && info->m_fWeaponRange < 500.0f) ? info->m_fWeaponRange : kAimRange;
    CVector target = origin + front * (range / fl);
    // what lies on that line, for the log (GTA's own shot does its own test)
    CColPoint cp;
    CEntity* hit = nullptr;
    const bool any = CWorld::ProcessLineOfSight(origin, target, cp, hit, true, true, true, true, false, false, false, true);
    const float hitDist = any ? (cp.m_vecPoint - origin).Magnitude() : -1.0f;
    const unsigned int clip0 = w->m_nAmmoInClip, total0 = w->m_nAmmoTotal;
    const int state0 = static_cast<int>(w->m_nState);
    const bool fired = w->Fire(ped, &origin, &origin, nullptr, &target, nullptr);
    log("shoot test %d (F12): CWeapon::Fire returned %s | weapon %d, state %d -> %d, ammo %u/%u -> %u/%u | from %.2f %.2f %.2f "
        "toward %.2f %.2f %.2f (%.0f m) | on that line: %s%s (model %d) at %.1f m",
        g_shots, fired ? "TRUE" : "false", static_cast<int>(w->m_eWeaponType), state0, static_cast<int>(w->m_nState), clip0, total0,
        w->m_nAmmoInClip, w->m_nAmmoTotal, origin.x, origin.y, origin.z, target.x, target.y, target.z, range, HitKind(hit),
        any ? "" : " (nothing within range)", hit ? static_cast<int>(hit->m_nModelIndex) : -1, hitDist);
}

void ShootFrame(LogFn log)
{
    const bool down = (GetAsyncKeyState(kShotKey) & 0x8000) != 0;
    const bool pressed = down && !g_keyWasDown;
    g_keyWasDown = down;
    if (!SkateModeIsOn()) return;
    CPlayerPed* ped = FindPlayerPed();
    if (!ped) return;
    const DWORD now = GetTickCount();
    if (now - g_nextInfo < 0x80000000ul) {
        g_nextInfo = now + 1000;
        LogHeld(ped, log);
    }
    if (pressed && GtaIsForeground()) TestShot(ped, log);
}

} // namespace

void ShootFrameSafe(LogFn log)
{
    __try {
        ShootFrame(log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("shoot: ERROR (exception 0x%08lX)", GetExceptionCode());
    }
}

} // namespace mp::gta
