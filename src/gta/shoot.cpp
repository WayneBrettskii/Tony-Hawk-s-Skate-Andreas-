// SkateAndreas: shooting while skating (DESIGN 8.53). See shoot.h.
//
// 8.53 step 1 (user 2026-10-17: "commit the plan and start step 1 (probe + one test shot)"):
// GTA's controller is blanked in skate mode, so GTA never fires by itself. Our plugin asks GTA's
// own weapon code (CWeapon::Fire, 0x742300) to fire CJ's held gun; GTA does the bullet, damage,
// sound, ammo and wanted level (test 39). F12 = one test shot along the camera.
//
// 8.53 step 2 (user 2026-10-17: "go ahead with the step 2 code as planned in DESIGN 8.53"):
// shoot mode. D-pad Left + X toggles it (X held back from reTHAWed, so the toggle does no trick);
// while it is on, L1 and B never reach reTHAWed (test 40: was R1, which is also the bert slide):
// L1 locks on to a ped or a vehicle in front of the
// camera (again = the next one, held 0.5 s = no lock), B fires at the gun's own rate (held for
// automatic guns), toward the target or along the camera. Reloading is GTA's own weapon code.
// Buttons: the user's PS5 pad. "X" = Cross (the bottom button, GTA's ButtonCross), "B" = Circle.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CPlayerPed.h"
#include "CWeapon.h"
#include "CWeaponInfo.h"
#include "CCamera.h"
#include "CWorld.h"
#include "CColPoint.h"
#include "CPools.h"
#include "CPad.h"
#include "CHud.h"
#include "CSprite.h"
#include "CSprite2d.h"
#include "CRect.h"
#include "CRGBA.h"
#include "CFont.h"
#include "RenderWare.h"
#include "common.h"
#include <windows.h>

#include "shoot.h"
#include "arsenal.h"
#include "skatemode.h"
#include "body.h" // 8.53 step 3: the aim pose
#include "../common/link.h"

#include <cmath>

namespace mp::gta {
namespace {

constexpr int kShotKey = VK_F12;        // F1-F11 are taken by our two plugins
constexpr int kRightWrist = 24;         // GTA ped bone (plugin-sdk ePedBones BONE_RIGHTWRIST)
constexpr int kChest = 3;               // BONE_SPINE1
constexpr float kAimRange = 60.0f;      // metres along the camera when the gun's own range is unknown
constexpr DWORD kComboMs = 250;         // Left pressed this soon after X (Cross) = the toggle too
constexpr float kConeDeg = 35.0f;       // a target must be within this of where the camera looks (25 until test 41)
constexpr DWORD kOutOfSightMs = 1000;   // a locked target out of sight this long is dropped
constexpr float kMinInterval = 0.08f;   // seconds between shots, at least
constexpr unsigned short kOurControlBit = 1u << 4; // skate mode's own bit in DisablePlayerControls

LogFn g_log = nullptr;
bool g_keyWasDown = false;
DWORD g_nextInfo = 0;
int g_shots = 0;

// ---- pad side (ShootFilterPad, in GTA's pad update) ----
bool g_leftWas = false, g_xWas = false;
DWORD g_leftAt = 0, g_xAt = 0;
bool g_comboLatched = false;  // the toggle fired; wait until Left or X is released
bool g_holdX = false;         // X is being kept from reTHAWed (pressed with Left)
bool g_toggleWanted = false;  // for ShootFrame
bool g_r1 = false, g_b = false; // raw aim (L1 since test 40; the name stays) / B, for ShootFrame
LONG g_heldBack = 0;          // frames a button was kept from reTHAWed (log)
// test 48: L1 + R1 together pass to reTHAWed (caveman / off the board) instead of aiming
bool g_pairPass = false;      // pad thread only
LONG g_pairPasses = 0;        // times the pair was passed (log)

// ---- game side (ShootFrame) ----
bool g_on = false;
int g_targetHandle = -1;      // CPools ref of the locked ped / vehicle, -1 = none
bool g_targetIsPed = false;
DWORD g_targetOutOfSightSince = 0;
bool g_r1Was = false, g_bWas = false;
DWORD g_r1DownAt = 0;
DWORD g_nextShotAt = 0;
int g_modeShots = 0, g_secondShots = 0;
DWORD g_reloadSeenAt = 0;     // the clip ran empty
bool g_reloadCalled = false;
bool g_reloading = false;
// test 41: the gun-mode messages are drawn by us in the middle of the screen (GTA's help box sat
// over reTHAWed's HUD); the marker draws are counted for the log
char g_msg[64] = "";
DWORD g_msgUntil = 0;
volatile LONG g_markerDraws = 0, g_markerMissed = 0;
const char* g_aimWhat = "";   // what the last unlocked shot was aimed at (log)
// 8.53 step 3: the aim pose (body.cpp) while L1 is held or just after a shot, eased in and out
constexpr DWORD kAimAfterShotMs = 600;
constexpr float kAimEaseS = 0.15f;
DWORD g_lastShotAt = 0, g_aimTick = 0;
float g_aimW = 0.0f;
bool g_skipImpactLogged = false;
float g_aimDist = -1.0f;

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

CWeaponInfo* InfoOf(CPlayerPed* ped, CWeapon* w)
{
    return w ? CWeaponInfo::GetWeaponInfo(w->m_eWeaponType, static_cast<unsigned char>(ped->GetWeaponSkill())) : nullptr;
}

bool IsGun(CWeaponInfo* info) { return info && info->m_nWeaponFire == WEAPON_FIRE_INSTANT_HIT; }

float FireInterval(CWeaponInfo* info)
{
    float t = info ? info->m_fAnimLoopEnd - info->m_fAnimLoopStart : 0.25f;
    if (!(t > kMinInterval)) t = kMinInterval;
    if (t > 1.5f) t = 1.5f;
    return t;
}

float TargetRange(CWeaponInfo* info)
{
    const float r = info ? info->m_fTargetRange : 0.0f;
    return (r > 1.0f && r < 300.0f) ? r : 40.0f;
}

// test 41 ("it didn't look like it was facing the camera"): the active CCam's m_vecFront is not what
// is on screen while skating; the camera's own matrix is (GTA's "up" row is the look direction).
CVector CameraFront()
{
    CVector f = TheCamera.GetForward();
    float l = f.Magnitude();
    if (l < 0.01f) {
        f = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront;
        l = f.Magnitude();
    }
    return l > 0.01f ? f * (1.0f / l) : CVector(0.0f, 1.0f, 0.0f);
}

void LogCameraOnce()
{
    static int n = 0;
    if (n >= 3 || !g_log) return;
    ++n;
    const CVector m = TheCamera.GetForward(), c = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront;
    const CVector p = TheCamera.GetPosition();
    g_log("gun mode: camera at %.1f %.1f %.1f | matrix forward %.2f %.2f %.2f (used) | active cam %d front %.2f %.2f %.2f", p.x, p.y,
          p.z, m.x, m.y, m.z, static_cast<int>(TheCamera.m_nActiveCam), c.x, c.y, c.z);
}

// test 41: the MP5 fired one shot a press (GTA's "continuous fire" flag is not set for it):
// automatic guns by type (Uzi, MP5, AK-47, M4, Tec-9, minigun), or the flag
bool IsAutomatic(CWeapon* w, CWeaponInfo* info)
{
    switch (static_cast<int>(w->m_eWeaponType)) {
    case 28: case 29: case 30: case 31: case 32: case 38: return true;
    default: return info && info->m_nFlags.bContinuosFire;
    }
}

CVector BonePos(CPed* p, int bone)
{
    RwV3d v = {};
    p->GetBonePosition(v, static_cast<unsigned int>(bone), true);
    return CVector(v.x, v.y, v.z);
}

// The locked target, or nullptr if it is gone (handles are safe: a freed pool slot gives nullptr).
CEntity* Target()
{
    if (g_targetHandle < 0) return nullptr;
    if (g_targetIsPed) return CPools::GetPed(g_targetHandle);
    return CPools::GetVehicle(g_targetHandle);
}

// test 43 ("the lock-on was there but the peds took no damage"): the locked ped's chest bone gave points
// 1.6-2.4 m from CJ for peds locked 6-10 m away (GTA does not keep other peds' bones up to date when it
// does not need them), so the shots went beside CJ. A ped's own position (the middle of its body) instead.
CVector AimPoint(CEntity* e, bool isPed)
{
    if (isPed) return e->GetPosition() + CVector(0.0f, 0.0f, 0.2f);
    return e->GetPosition();
}

bool Alive(CEntity* e, bool isPed)
{
    if (isPed) return static_cast<CPed*>(e)->m_fHealth > 0.0f;
    CVehicle* v = static_cast<CVehicle*>(e);
    return v->m_fHealth > 0.0f && v->m_nStatus != STATUS_WRECKED;
}

bool ClearSight(const CVector& from, const CVector& to)
{
    CColPoint cp;
    CEntity* hit = nullptr;
    return !CWorld::ProcessLineOfSight(from, to, cp, hit, true, false, false, false, false, false, false, true);
}

void SetMessage(const char* text)
{
    snprintf(g_msg, sizeof(g_msg), "%s", text);
    g_msgUntil = GetTickCount() + 2000;
}

void ClearLock(const char* why)
{
    if (g_targetHandle >= 0 && g_log) g_log("shoot lock: cleared (%s)", why);
    g_targetHandle = -1;
    g_targetOutOfSightSince = 0;
}

// test 42 (user: "can't we use GTA's own lock-on?"): GTA's CPlayerPed::FindWeaponLockOnTarget picks
// the target, as with a controller on foot; its choice is read and kept as a pool handle, and GTA's own
// target pointer is cleared again at once (GTA's aiming code is not running for CJ). False if none.
bool GtaLockOn(CPlayerPed* ped)
{
    const bool found = ped->FindWeaponLockOnTarget();
    CEntity* t = reinterpret_cast<CEntity*>(ped->m_pTargetedObject);
    int handle = -1;
    bool isPed = false;
    if (found && t && t->m_nType == ENTITY_TYPE_PED && t != ped) {
        handle = CPools::GetPedRef(static_cast<CPed*>(t));
        isPed = true;
    } else if (found && t && t->m_nType == ENTITY_TYPE_VEHICLE) {
        handle = CPools::GetVehicleRef(static_cast<CVehicle*>(t));
    }
    if (handle >= 0 && !Alive(t, isPed)) handle = -1;
    const CVector chest = BonePos(ped, kChest);
    const float dist = t ? (t->GetPosition() - chest).Magnitude() : -1.0f;
    const int model = t ? static_cast<int>(t->m_nModelIndex) : -1;
    ped->ClearWeaponTarget();
    if (handle < 0) return false;
    g_targetHandle = handle;
    g_targetIsPed = isPed;
    g_targetOutOfSightSince = 0;
    g_log("shoot lock: GTA's own lock-on: %s (model %d) at %.1f m", isPed ? "ped" : "vehicle", model, dist);
    return true;
}

// Our own picker (when GTA's finds nothing): the next target in the cone in front of the camera, nearest first (peds a little before
// vehicles at the same distance), after the one locked now; wraps round.
void PickNext(CPlayerPed* ped, CWeaponInfo* info)
{
    struct Cand { int handle; bool isPed; float score, dist, deg; int model; };
    Cand c[48];
    int n = 0;
    const CVector cam = TheCamera.GetPosition();
    const CVector front = CameraFront();
    const CVector chest = BonePos(ped, kChest);
    const float range = TargetRange(info);
    const float cosMax = std::cos(kConeDeg * 3.14159265f / 180.0f);
    auto consider = [&](CEntity* e, bool isPed, int handle) {
        if (n >= 48 || !e || !Alive(e, isPed)) return;
        const CVector p = AimPoint(e, isPed);
        const float dist = (p - chest).Magnitude();
        if (dist > range || dist < 0.5f) return;
        CVector d = p - cam;
        const float dl = d.Magnitude();
        if (dl < 0.01f) return;
        const float cosA = (d.x * front.x + d.y * front.y + d.z * front.z) / dl;
        if (cosA < cosMax) return;
        if (!ClearSight(chest, p)) return;
        c[n++] = Cand{handle, isPed, dist - (isPed ? 2.0f : 0.0f), dist, std::acos(cosA > 1.0f ? 1.0f : cosA) * 57.2958f,
                      static_cast<int>(e->m_nModelIndex)};
    };
    if (CPools::ms_pPedPool)
        for (int i = 0; i < CPools::ms_pPedPool->m_nSize; ++i) {
            CPed* p = CPools::ms_pPedPool->GetAt(i);
            if (!p || p == ped) continue;
            consider(p, true, CPools::GetPedRef(p));
        }
    if (CPools::ms_pVehiclePool)
        for (int i = 0; i < CPools::ms_pVehiclePool->m_nSize; ++i) {
            CVehicle* v = CPools::ms_pVehiclePool->GetAt(i);
            if (!v || (ped->bInVehicle && ped->m_pVehicle == v)) continue;
            consider(v, false, CPools::GetVehicleRef(v));
        }
    if (n == 0) {
        ClearLock("L1: nothing to lock on to in front of the camera");
        g_log("shoot lock: L1: none (GTA's own lock-on found none, and no ped or vehicle within %.0f m and %.0f deg of the "
              "camera in clear sight)", range, kConeDeg);
        return;
    }
    for (int a = 1; a < n; ++a) { // nearest first
        const Cand x = c[a];
        int b = a - 1;
        while (b >= 0 && c[b].score > x.score) { c[b + 1] = c[b]; --b; }
        c[b + 1] = x;
    }
    int pick = 0;
    for (int i = 0; i < n; ++i)
        if (c[i].handle == g_targetHandle && c[i].isPed == g_targetIsPed) { pick = (i + 1) % n; break; }
    g_targetHandle = c[pick].handle;
    g_targetIsPed = c[pick].isPed;
    g_targetOutOfSightSince = 0;
    g_log("shoot lock: target %s (model %d) at %.1f m, %.0f deg off the camera (%d of %d in the cone)", c[pick].isPed ? "ped" : "vehicle",
          c[pick].model, c[pick].dist, c[pick].deg, pick + 1, n);
}

// What the camera's own line meets (CJ ignored), as far as the gun's range: the middle of the screen.
CVector CentrePoint(CPlayerPed* ped, CWeaponInfo* info, CEntity** hitOut, bool* anyOut)
{
    const float range = (info->m_fWeaponRange > 1.0f && info->m_fWeaponRange < 500.0f) ? info->m_fWeaponRange : kAimRange;
    const CVector cam = TheCamera.GetPosition();
    const CVector farPt = cam + CameraFront() * (range + 5.0f);
    CColPoint cp;
    CEntity* hit = nullptr;
    CWorld::pIgnoreEntity = ped;
    const bool any = CWorld::ProcessLineOfSight(cam, farPt, cp, hit, true, true, true, true, false, false, false, true);
    CWorld::pIgnoreEntity = nullptr;
    if (hitOut) *hitOut = any ? hit : nullptr;
    if (anyOut) *anyOut = any;
    return any ? cp.m_vecPoint : farPt;
}

void Shot(CPlayerPed* ped, CWeapon* w, CWeaponInfo* info, DWORD now)
{
    CVector origin = BonePos(ped, kRightWrist);
    CEntity* t = Target();
    CVector target;
    bool aimed = false;
    if (t) {
        target = AimPoint(t, g_targetIsPed);
        aimed = true;
        // what the shot's line meets first (CJ ignored), for the log
        CColPoint cp;
        CEntity* hit = nullptr;
        CWorld::pIgnoreEntity = ped;
        const bool any = CWorld::ProcessLineOfSight(origin, target + (target - origin) * 0.1f, cp, hit, true, true, true, true, false, false,
                                                    false, true);
        CWorld::pIgnoreEntity = nullptr;
        g_aimWhat = !any ? "nothing on the line" : hit == t ? "the target" : HitKind(hit);
        g_aimDist = any ? (cp.m_vecPoint - origin).Magnitude() : -1.0f;
    } else {
        // test 42 ("bullets weren't hitting peds or anything"): a line from CJ's wrist parallel to the camera misses
        // what is in the middle of the screen; the shot now goes to what the camera's own line meets (CJ ignored)
        CEntity* hit = nullptr;
        bool any = false;
        target = CentrePoint(ped, info, &hit, &any);
        if (any && hit && (hit->m_nType == ENTITY_TYPE_PED || hit->m_nType == ENTITY_TYPE_VEHICLE)) t = hit;
        g_aimWhat = any ? HitKind(hit) : "nothing (in range)";
        g_aimDist = any ? (target - origin).Magnitude() : -1.0f;
    }
    // test 44 (the line met the locked ped 81 times, its health stayed 100: GTA's Fire does not shoot along
    // the line we give it while CJ's arm is not aiming): Fire still gives the sound, flash, ammo and wanted
    // level, but the hit is ours: GTA's own weapon line test from CJ's wrist through the aim point, then
    // GTA's own CWeapon::DoBulletImpact on what it meets (damage, blood, the ped's reaction).
    const float range = (info->m_fWeaponRange > 1.0f && info->m_fWeaponRange < 500.0f) ? info->m_fWeaponRange : kAimRange;
    CVector dir = target - origin;
    const float dl = dir.Magnitude();
    dir = dl > 0.01f ? dir * (1.0f / dl) : CameraFront();
    CVector end = origin + dir * range;
    CColPoint hitCp;
    CEntity* hitEnt = nullptr;
    const bool hitAny = CWeapon::ProcessLineOfSight(origin, end, hitCp, hitEnt, w->m_eWeaponType, ped, true, true, true, true, false, false,
                                                    false);
    auto health = [](CEntity* e) -> float {
        if (e && e->m_nType == ENTITY_TYPE_PED) return static_cast<CPed*>(e)->m_fHealth;
        if (e && e->m_nType == ENTITY_TYPE_VEHICLE) return static_cast<CVehicle*>(e)->m_fHealth;
        return -1.0f;
    };
    const float hp0 = health(hitEnt);
    const unsigned int clip0 = w->m_nAmmoInClip, total0 = w->m_nAmmoTotal;
    const bool fired = w->Fire(ped, &origin, &origin, t, &target, nullptr);
    const float hp1 = health(hitEnt);
    bool impact = false;
    // step 3: with the arm aimed, GTA's own Fire may start hitting; then ours is left out (no double damage)
    const bool gtaHurt = hp0 >= 0.0f && hp1 < hp0;
    if (gtaHurt && !g_skipImpactLogged) {
        g_skipImpactLogged = true;
        g_log("shoot: GTA's own Fire damaged the hit (%.0f -> %.0f): our impact is left out when that happens", hp0, hp1);
    }
    if (fired && hitAny && hitEnt && hitEnt != ped && !gtaHurt) {
        CVector hitPoint = hitCp.m_vecPoint;
        w->DoBulletImpact(ped, hitEnt, &origin, &hitPoint, &hitCp, 0);
        impact = true;
    }
    const float hp2 = health(hitEnt);
    ++g_modeShots;
    ++g_secondShots;
    g_lastShotAt = now;
    g_nextShotAt = now + static_cast<DWORD>(FireInterval(info) * 1000.0f);
    g_log("shoot: shot %d %s, %.1f m away | our hit line: %s%s at %.1f m (model %d) | its health %.0f, after GTA's Fire %.0f, after "
          "our impact %.0f%s | Fire %s | ammo %u/%u -> %u/%u",
          g_modeShots, aimed ? "at the locked target" : "at the middle of the screen", dl, hitAny ? HitKind(hitEnt) : "nothing",
          hitAny && hitEnt == t && t ? " (the target)" : "", hitAny ? (hitCp.m_vecPoint - origin).Magnitude() : -1.0f,
          hitEnt ? static_cast<int>(hitEnt->m_nModelIndex) : -1, hp0, hp1, hp2, impact ? "" : " (no impact applied)",
          fired ? "TRUE" : "false", clip0, total0, w->m_nAmmoInClip, w->m_nAmmoTotal);
}

void TurnOn(CPlayerPed* ped, CWeapon* w, CWeaponInfo* info)
{
    g_on = true;
    g_modeShots = 0;
    g_nextShotAt = 0;
    g_reloadSeenAt = 0;
    g_reloadCalled = false;
    g_reloading = false;
    ClearLock("shoot mode on");
    SetMessage("Gun mode ON");
    LogCameraOnce();
    g_log("shoot mode ON (Left + X): weapon %d, ammo %u/%u, fire interval %.2f s (GTA's firing loop %.2f-%.2f), %s, target range %.0f m, "
          "clip %u, skill %d",
          static_cast<int>(w->m_eWeaponType), w->m_nAmmoInClip, w->m_nAmmoTotal, FireInterval(info), info->m_fAnimLoopStart,
          info->m_fAnimLoopEnd, IsAutomatic(w, info) ? "holds fire (automatic)" : "one shot a press", TargetRange(info),
          static_cast<unsigned>(info->m_nAmmoClip), static_cast<int>(ped->GetWeaponSkill()));
}

void TurnOff(const char* why, bool tell)
{
    if (!g_on) return;
    g_on = false;
    ClearLock("shoot mode off");
    if (tell) SetMessage("Gun mode OFF");
    if (g_log) g_log("shoot mode OFF (%s) after %d shot(s)", why, g_modeShots);
}

// What CJ holds, once a second while skating.
void LogHeld(CPlayerPed* ped)
{
    CWeapon* w = ped->GetWeapon();
    if (!w) return;
    CWeaponInfo* info = InfoOf(ped, w);
    g_log("shoot probe: CJ holds weapon %d in slot %d (%s), state %d, ammo %u in the clip / %u in all, gun model in hand: %s "
          "(model %d) | shoot mode %s%s | F12 = one test shot",
          static_cast<int>(w->m_eWeaponType), static_cast<int>(ped->m_nSelectedWepSlot), info ? FireKind(info->m_nWeaponFire) : "no info",
          static_cast<int>(w->m_nState), w->m_nAmmoInClip, w->m_nAmmoTotal, ped->m_pWeaponObject ? "yes" : "no", ped->m_nWeaponModelId,
          g_on ? "ON" : "off", g_on && g_targetHandle >= 0 ? ", locked" : "");
    unsigned gunSamples = 0;
    if (const char* gun = BodyTakeGunInfo(&gunSamples))
        g_log("gun mode: %s | %u sampled frame(s)", gun, gunSamples);
    if (g_on) {
        unsigned beyond = 0;
        float chest = 0.0f;
        unsigned armClamped = 0;
        const unsigned af = BodyAimFramesAndReset(&beyond, &chest, &armClamped);
        if (const char* info = BodyTakeAimInfo()) g_log("gun mode: %s", info);
        if (af)
            g_log("gun mode: aim pose drawn in %u frame(s) (last second), chest turned %.0f deg, target further round than 60 deg "
                  "in %u, arm kept in front of the chest in %u", af, chest, beyond, armClamped);
    }
    if (g_on && (g_markerDraws || g_markerMissed))
        g_log("gun mode: lock marker drawn in %ld frame(s), not on screen in %ld (last second)",
              static_cast<long>(InterlockedExchange(&g_markerDraws, 0)), static_cast<long>(InterlockedExchange(&g_markerMissed, 0)));
    if (g_on) {
        g_log("shoot mode: last second: %d shot(s), %ld frame(s) with buttons kept from reTHAWed", g_secondShots,
              static_cast<long>(InterlockedExchange(&g_heldBack, 0)));
        g_secondShots = 0;
        if (const LONG pp = InterlockedExchange(&g_pairPasses, 0))
            g_log("gun mode: L1 + R1 passed to reTHAWed %ld time(s) (caveman / on or off the board), not used to aim", static_cast<long>(pp));
    }
}

// F12: one shot of the held gun, from CJ's right wrist along the camera (step 1).
void TestShot(CPlayerPed* ped)
{
    ++g_shots;
    CWeapon* w = ped->GetWeapon();
    CWeaponInfo* info = InfoOf(ped, w);
    if (!w || !IsGun(info)) {
        g_log("shoot test %d (F12): not a gun that fires bullets: not fired", g_shots);
        return;
    }
    CVector origin = BonePos(ped, kRightWrist);
    const float range = (info->m_fWeaponRange > 1.0f && info->m_fWeaponRange < 500.0f) ? info->m_fWeaponRange : kAimRange;
    CVector target = origin + CameraFront() * range;
    CColPoint cp;
    CEntity* hit = nullptr;
    const bool any = CWorld::ProcessLineOfSight(origin, target, cp, hit, true, true, true, true, false, false, false, true);
    const unsigned int clip0 = w->m_nAmmoInClip, total0 = w->m_nAmmoTotal;
    const bool fired = w->Fire(ped, &origin, &origin, nullptr, &target, nullptr);
    g_log("shoot test %d (F12): CWeapon::Fire returned %s | ammo %u/%u -> %u/%u | on that line: %s at %.1f m", g_shots,
          fired ? "TRUE" : "false", clip0, total0, w->m_nAmmoInClip, w->m_nAmmoTotal, HitKind(hit),
          any ? (cp.m_vecPoint - origin).Magnitude() : -1.0f);
}

void ShootFrame(LogFn log)
{
    g_log = log;
    const bool down = (GetAsyncKeyState(kShotKey) & 0x8000) != 0;
    const bool pressed = down && !g_keyWasDown;
    g_keyWasDown = down;
    if (!SkateModeIsOn()) {
        TurnOff("skate mode off", false);
        g_toggleWanted = false;
        return;
    }
    CPlayerPed* ped = FindPlayerPed();
    if (!ped) return;
    const DWORD now = GetTickCount();
    CWeapon* w = ped->GetWeapon();
    CWeaponInfo* info = InfoOf(ped, w);
    CPad* pad = CPad::GetPad(0);
    const bool scriptHolds = pad && (pad->DisablePlayerControls & ~kOurControlBit) != 0;

    if (g_toggleWanted) {
        g_toggleWanted = false;
        if (g_on) {
            TurnOff("Left + X", true);
        } else if (!w || !IsGun(info)) {
            SetMessage("Gun mode needs a gun");
            log("shoot mode: not turned on: CJ holds weapon %d (%s), not a gun that fires bullets", w ? static_cast<int>(w->m_eWeaponType) : -1,
                info ? FireKind(info->m_nWeaponFire) : "no info");
        } else if (w->m_nAmmoInClip == 0 && w->m_nAmmoTotal == 0) {
            SetMessage("Gun mode: no ammo");
            log("shoot mode: not turned on: no ammo");
        } else if (scriptHolds) {
            log("shoot mode: not turned on: a script has taken the controls");
        } else {
            TurnOn(ped, w, info);
        }
    }
    if (g_on) {
        if (scriptHolds) TurnOff("a script took the controls", false);
        // Keep gun mode's wheel available with a non-bullet or empty slot selected.
    }

    if (g_on && w && IsGun(info) && !ArsenalBlocksInput()) {
        // test 42: the user holds L1 to aim, as in GTA (the old "tap = lock, held 0.5 s = clear" never locked):
        // L1 pressed = lock on (GTA's own first, ours if it finds none); while held, a lost target is looked for
        // again every 0.3 s; L1 released = no lock
        const bool r1 = g_r1;
        if (r1 && !g_r1Was) {
            g_r1DownAt = now;
            if (!GtaLockOn(ped)) PickNext(ped, info);
        } else if (r1 && g_targetHandle < 0 && now - g_r1DownAt >= 300) {
            g_r1DownAt = now;
            if (!GtaLockOn(ped)) PickNext(ped, info);
        } else if (!r1 && g_r1Was) {
            ClearLock("L1 released");
        }
        g_r1Was = r1;
        // the lock drops by itself
        if (CEntity* t = Target()) {
            const CVector chest = BonePos(ped, kChest), p = AimPoint(t, g_targetIsPed);
            if (!Alive(t, g_targetIsPed)) {
                ClearLock("the target is dead / wrecked");
            } else if ((p - chest).Magnitude() > TargetRange(info) * 1.2f) {
                ClearLock("the target is out of range");
            } else if (!ClearSight(chest, p)) {
                if (!g_targetOutOfSightSince) g_targetOutOfSightSince = now;
                else if (now - g_targetOutOfSightSince >= kOutOfSightMs) ClearLock("the target was out of sight for 1 s");
            } else {
                g_targetOutOfSightSince = 0;
            }
        } else if (g_targetHandle >= 0) {
            ClearLock("the target is gone");
        }
        // reload: GTA's own weapon code; Reload() once if the state does not change by itself
        const bool reloading = w->m_nState == WEAPONSTATE_RELOADING;
        if (reloading && !g_reloading) {
            g_reloading = true;
            g_reloadSeenAt = now;
            SetMessage("Reloading");
            log("shoot: reload: GTA's weapon code is reloading (ammo %u/%u)", w->m_nAmmoInClip, w->m_nAmmoTotal);
        } else if (!reloading && g_reloading) {
            g_reloading = false;
            g_reloadCalled = false;
            log("shoot: reload: done after %lu ms (ammo %u/%u)", now - g_reloadSeenAt, w->m_nAmmoInClip, w->m_nAmmoTotal);
        } else if (!reloading && w->m_nAmmoInClip == 0 && w->m_nAmmoTotal > 0) {
            if (!g_reloadSeenAt) g_reloadSeenAt = now;
            if (!g_reloadCalled && now - g_reloadSeenAt >= 300) {
                g_reloadCalled = true;
                w->Reload(ped);
                log("shoot: reload: the clip was empty for 0.3 s and GTA did not reload: CWeapon::Reload called (state now %d, ammo %u/%u)",
                    static_cast<int>(w->m_nState), w->m_nAmmoInClip, w->m_nAmmoTotal);
            }
        } else if (!reloading && w->m_nAmmoInClip > 0) {
            g_reloadSeenAt = 0;
        }
        // B: fire at the gun's own rate; held for automatic guns
        const bool b = g_b;
        const bool wantShot = IsAutomatic(w, info) ? b : (b && !g_bWas);
        if (wantShot && !g_reloading && w->m_nAmmoInClip > 0 && now >= g_nextShotAt) Shot(ped, w, info, now);
        g_bWas = b;
        // step 3: CJ points the gun while L1 is held or just after a shot, at the target or the screen's middle
        const bool aiming = g_r1 || (g_lastShotAt && now - g_lastShotAt < kAimAfterShotMs);
        float dt = g_aimTick ? (now - g_aimTick) / 1000.0f : 0.0f;
        if (dt > 0.1f) dt = 0.1f;
        g_aimTick = now;
        g_aimW += (aiming ? dt : -dt) / kAimEaseS;
        if (g_aimW > 1.0f) g_aimW = 1.0f;
        if (g_aimW < 0.0f) g_aimW = 0.0f;
        if (g_aimW > 0.0f) {
            CEntity* t = Target();
            const CVector p = t ? AimPoint(t, g_targetIsPed) : CentrePoint(ped, info, nullptr, nullptr);
            const float pt[3] = {p.x, p.y, p.z};
            const bool minigun = w->m_eWeaponType == WEAPONTYPE_MINIGUN;
            BodySetAim(pt, g_aimW, minigun || !info->m_nFlags.bAimWithArm, minigun);
        } else {
            BodyClearAim();
        }
    } else {
        g_aimW = 0.0f;
        g_aimTick = 0;
        BodyClearAim();
        if (g_targetHandle >= 0) ClearLock("menu or non-bullet weapon");
    }

    if (now - g_nextInfo < 0x80000000ul) {
        g_nextInfo = now + 1000;
        LogHeld(ped);
    }
    if (pressed && GtaIsForeground() && !ArsenalBlocksInput()) TestShot(ped);
}

// The gun-mode message in the middle of the screen, and a small red square over the locked target.
void DrawMarker()
{
    if (!SkateModeIsOn()) return;
    if (g_msg[0] && GetTickCount() < g_msgUntil) {
        const float W = static_cast<float>(RsGlobal.maximumWidth), H = static_cast<float>(RsGlobal.maximumHeight);
        // test 42 (user: "make the standard black and white help message and centre it"): white text on a
        // black box, GTA's help-box look, in the middle of the screen
        CFont::SetBackground(false, false);
        CFont::SetProportional(true);
        CFont::SetJustify(false);
        CFont::SetOrientation(ALIGN_CENTER);
        CFont::SetCentreSize(W * 0.6f);
        CFont::SetFontStyle(FONT_SUBTITLES);
        CFont::SetScale(W / 640.0f * 0.5f, H / 448.0f * 1.0f);
        CFont::SetColor(CRGBA(255, 255, 255, 255));
        CFont::SetEdge(0);
        CFont::SetDropShadowPosition(0);
        const float tw = CFont::GetStringWidth(g_msg, true);
        const float th = H / 448.0f * 1.0f * 18.0f;
        const float cx = W * 0.5f, ty = H * 0.06f, padX = W * 0.012f, padY = th * 0.35f; // test 43: top centre
        CSprite2d::DrawRect(CRect(cx - tw * 0.5f - padX, ty - padY, cx + tw * 0.5f + padX, ty + th + padY), CRGBA(0, 0, 0, 190));
        CFont::PrintString(cx, ty, g_msg);
    }
    if (!g_on) return;
    CEntity* t = Target();
    if (!t) return;
    CVector p = AimPoint(t, g_targetIsPed);
    RwV3d world = {p.x, p.y, p.z}, screen = {};
    float sw = 0.0f, sh = 0.0f;
    // test 41: no marker was seen; the checks for "too far / too near" are off now, only in front counts
    if (!CSprite::CalcScreenCoors(world, &screen, &sw, &sh, false, false) || screen.z < 0.1f) {
        InterlockedIncrement(&g_markerMissed);
        return;
    }
    InterlockedIncrement(&g_markerDraws);
    const float s = 14.0f, k = 3.0f;
    const CRGBA red(230, 30, 30, 230);
    CSprite2d::DrawRect(CRect(screen.x - s, screen.y - s, screen.x + s, screen.y - s + k), red);
    CSprite2d::DrawRect(CRect(screen.x - s, screen.y + s - k, screen.x + s, screen.y + s), red);
    CSprite2d::DrawRect(CRect(screen.x - s, screen.y - s, screen.x - s + k, screen.y + s), red);
    CSprite2d::DrawRect(CRect(screen.x + s - k, screen.y - s, screen.x + s, screen.y + s), red);
}

} // namespace

void ShootFilterPad(LONG& buttons, short* axes)
{
    (void)axes;
    if (!SkateModeIsOn()) {
        g_leftWas = g_xWas = false;
        g_comboLatched = g_holdX = false;
        g_r1 = g_b = false;
        return;
    }
    const DWORD now = GetTickCount();
    // test 40: the user's "X" is the PS5 pad's X (Cross, the bottom button), not the Xbox X
    // (Square here): they pressed Left + Cross, 47-156 ms apart. So: Cross pressed while Left is
    // held (any time), or Left pressed up to kComboMs after Cross went down.
    const bool left = (buttons & kBtnLeft) != 0, x = (buttons & kBtnCross) != 0;
    if (left && !g_leftWas) {
        g_leftAt = now;
        if (x && now - g_xAt <= kComboMs && !g_comboLatched) { // Cross first, then Left
            g_comboLatched = true;
            g_toggleWanted = true;
        }
    }
    if (x && !g_xWas) {
        g_xAt = now;
        if (left && !g_comboLatched) { // Left held, then Cross: the toggle, not an ollie
            g_holdX = true;
            g_comboLatched = true;
            g_toggleWanted = true;
        }
    }
    if (!x) g_holdX = false;
    if (!left || !x) g_comboLatched = false;
    g_leftWas = left;
    g_xWas = x;
    if (g_holdX) {
        buttons &= ~kBtnCross;
        InterlockedIncrement(&g_heldBack);
    }
    // while shoot mode is on, L1 (spin left; test 40: R1 is also the bert slide, so aim moved to L1)
    // and B / Circle (grab) are ours
    // test 48 ("make it so cj can still caveman back to his board if he's on foot"): reTHAWed gets on and
    // off the board (caveman) with L1 + R1 together, the same pair that gets the rider off the BMX (B10).
    // With L1 held back that pair never reached it. So while R1 is down, L1 goes to reTHAWed and does not
    // aim; it stays passed until L1 is let go, so a press that starts as the pair is never cut in half.
    const bool l1 = (buttons & kBtnL1) != 0, r1 = (buttons & kBtnR1) != 0;
    if (g_on && l1 && r1 && !g_pairPass) {
        g_pairPass = true;
        InterlockedIncrement(&g_pairPasses);
    }
    if (!l1) g_pairPass = false;
    g_r1 = l1 && !g_pairPass;
    g_b = (buttons & kBtnCircle) != 0;
    const LONG ours = (g_pairPass ? 0 : kBtnL1) | kBtnCircle;
    if (g_on && (buttons & ours)) {
        buttons &= ~ours;
        InterlockedIncrement(&g_heldBack);
    }
}

bool ShootModeIsOn() { return g_on && SkateModeIsOn(); }

void ShootFrameSafe(LogFn log)
{
    __try {
        ShootFrame(log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("shoot: ERROR (exception 0x%08lX)", GetExceptionCode());
    }
}

namespace {
void DrawMarkerSafe()
{
    __try {
        DrawMarker();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
} // namespace

void ShootAddHook()
{
    plugin::Events::drawHudEvent.Add([] { DrawMarkerSafe(); });
}

} // namespace mp::gta
