// my-passthrough: BMX mode, GTA side. See bmxmode.h and docs/DESIGN.md 8.29.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CCamera.h"
#include "CCutsceneMgr.h"
#include "CMenuManager.h"
#include "CPad.h"
#include "CPlayerInfo.h"
#include "CPedIntelligence.h"
#include "CPlayerPed.h"
#include "CTaskComplexLeaveCar.h"
#include "CTaskManager.h"
#include "CReplay.h"
#include "CStuntJumpManager.h"
#include "CTimer.h"
#include "CVehicle.h"
#include "CWorld.h"
#include "common.h"
#include <windows.h>

#include "bmxmode.h"
#include "padlink.h"
#include "../common/convert.h"

#include <cmath>

namespace mp::gta {
namespace {

constexpr int kBmxModel = 481;
constexpr int kToggleKey = VK_F7;                 // same keys as skate mode
constexpr unsigned short kSkateControlBit = 1u << 4; // skate mode's own control flag (not a script's)
constexpr LONG kMaxExtrapolateMs = 50;
constexpr LONG kStaleMs = 1000;
constexpr LONG kRideModeBmx = 2;                  // link gtaSkateMode value while BMX mode is on

// ---- B1: rider on / off the bike with CJ ----
bool g_weAsked = false;          // we asked reTHAWed's rider onto the bike
bool g_wasRiding = false;
LONG g_lastThaw = -1;
DWORD g_askedAt = 0;

// ---- B2: rider placed at the GTA bike before BMX mode starts ----
LONG g_placeSeqWanted = 0;       // 0 = not asked for this start yet
DWORD g_checkAt = 0;             // when to compare where the rider ended up
float g_bikeFeet[3] = {};        // GTA spot we asked for

// ---- B3: BMX mode ----
bool g_active = false;
bool g_userOff = false;          // player switched it off (F7 / L3+R3 or got off in reTHAWed) for this ride
CVehicle* g_bike = nullptr;
float g_baseDist = 0.5f;         // bike centre above its wheels' bottom
unsigned char g_savedKnock = 0;
bool g_savedCollision = true;
bool g_savedStunts = true;       // GTA's unique stunt jumps on before BMX mode
bool g_stuntsOff = false;        // we switched them off
bool g_keyWas = false, g_comboWas = false;
DWORD g_riderOffSince = 0;       // reTHAWed's rider "off the bike" since (0 = on); a bail drops it briefly
constexpr DWORD kRiderOffMs = 1000; // L1 + R1 in reTHAWed (test 01:14); a short drop doesn't count
bool g_cinemaLogged = false;
LONG g_replaySeen = 0;
const char* g_lastWhyNot = nullptr;
// once-per-second report
DWORD g_nextReport = 0;
unsigned g_frames = 0, g_held = 0, g_skipped = 0;
LONG g_maxAge = 0;

struct V3 {
    float x, y, z;
};
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 Norm(V3 a)
{
    const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return l > 1e-6f ? V3{a.x / l, a.y / l, a.z / l} : V3{0, 0, 0};
}
// a THAW direction in GTA terms (no scale)
V3 Dir(const float* d, float ySign) { return {d[0], d[2] * ySign, d[1]}; }

bool CjRidesBmxUnsafe()
{
    CPlayerPed* ped = FindPlayerPed();
    if (!ped || !ped->bInVehicle || !ped->m_pVehicle) return false;
    CVehicle* v = ped->m_pVehicle;
    return v->m_nModelIndex == kBmxModel && v->m_pDriver == ped;
}

bool CjRidesBmx()
{
    __try {
        return CjRidesBmxUnsafe();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GtaInFront()
{
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

// Why GTA itself is in charge right now (nullptr = the player is).
const char* GtaInCharge()
{
    CPlayerPed* ped = FindPlayerPed();
    CPad* pad = CPad::GetPad(0);
    if (!ped) return "no player";
    if (ped->m_fHealth <= 0.0f) return "CJ is dead";
    if (CWorld::Players[0].m_nPlayerState != PLAYERSTATE_PLAYING) return "player not in normal play";
    if (CCutsceneMgr::ms_running) return "cutscene running";
    if (TheCamera.m_bWideScreenOn) return "scripted scene (black bars)";
    if (pad && (pad->DisablePlayerControls & ~kSkateControlBit)) return "a script disabled player controls";
    return nullptr;
}

// ---- B2 ----
struct BikeSpot {
    bool ok = false;
    float feet[3] = {};
    float fwd[2] = {};
};

BikeSpot BikeSpotUnsafe()
{
    BikeSpot s;
    CPlayerPed* ped = FindPlayerPed();
    if (!ped || !ped->m_pVehicle) return s;
    CVehicle* v = ped->m_pVehicle;
    const CVector& p = v->GetPosition();
    const CVector& f = v->GetForward();
    s.feet[0] = p.x;
    s.feet[1] = p.y;
    s.feet[2] = p.z - v->GetDistanceFromCentreOfMassToBaseOfModel();
    s.fwd[0] = f.x;
    s.fwd[1] = f.y;
    s.ok = true;
    return s;
}

BikeSpot BikeSpotSafe()
{
    __try {
        return BikeSpotUnsafe();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return BikeSpot{};
    }
}

// Ask the THAW side to put the rider at the GTA bike's spot. Returns false if it can't.
bool PlaceRider(LinkBlock* link, const LevelAlignment& a, LogFn log)
{
    const BikeSpot b = BikeSpotSafe();
    if (!b.ok) return false;
    // inverse of MapAbsolute: GTA (x, y, z) = offset + scale * (tx, ySign * tz, ty)
    const float tx = (b.feet[0] - a.offset.x) / a.scale;
    const float tz = (b.feet[1] - a.offset.y) / (a.scale * a.ySign);
    const float ty = (b.feet[2] - a.offset.z) / a.scale;
    // GTA direction (fx, fy, 0) = (dx, ySign * dz, dy) for a THAW direction (dx, dy, dz)
    link->gtaPlacePos[0] = tx;
    link->gtaPlacePos[1] = ty;
    link->gtaPlacePos[2] = tz;
    link->gtaPlaceAt[0] = b.fwd[0];
    link->gtaPlaceAt[1] = 0.0f;
    link->gtaPlaceAt[2] = b.fwd[1] * a.ySign;
    MemoryBarrier();
    g_placeSeqWanted = InterlockedIncrement(&link->gtaPlaceSeq);
    for (int i = 0; i < 3; ++i) g_bikeFeet[i] = b.feet[i];
    g_checkAt = GetTickCount() + 1000;
    log("bmx: asked reTHAWed to put its rider at the GTA bike (%.2f %.2f %.2f) = %s (%.0f %.0f %.0f)", b.feet[0],
        b.feet[1], b.feet[2], a.name, tx, ty, tz);
    return true;
}

void CheckPlacement(LinkBlock* link, LogFn log)
{
    const LevelAlignment* a = FindLevelAlignment(static_cast<unsigned long>(link->thawLevelId));
    SkaterShared s;
    int retries = 0;
    if (!a || !ReadSkaterShared(link, s, retries)) return;
    const Vec3 g = MapAbsolute(Vec3{s.pos[0], s.pos[1], s.pos[2]}, *a);
    const float dx = g.x - g_bikeFeet[0], dy = g.y - g_bikeFeet[1], dz = g.z - g_bikeFeet[2];
    log("bmx: rider at GTA (%.2f %.2f %.2f): %.2f m from where the GTA bike was (height difference %.2f m)", g.x, g.y,
        g.z, std::sqrt(dx * dx + dy * dy), dz);
}

// ---- B3 ----
void LogWhyOffUnsafe(LogFn log)
{
    CPlayerPed* ped = FindPlayerPed();
    if (!ped) { log("bmx: CJ off the bike: no player"); return; }
    CVehicle* v = ped->m_pVehicle;
    log("bmx: CJ off the bike: in vehicle %s, vehicle %s (model %d, driver %s), ped state %d, knocked-off flag %d, "
        "bike health %.0f, replay mode %d, controls 0x%04X",
        ped->bInVehicle ? "yes" : "no", v ? (v == g_bike ? "our bike" : "another") : "none", v ? v->m_nModelIndex : -1,
        v ? (v->m_pDriver == ped ? "CJ" : "not CJ") : "-", static_cast<int>(ped->m_ePedState),
        static_cast<int>(ped->bKnockedOffBike), g_bike ? g_bike->m_fHealth : -1.0f, static_cast<int>(CReplay::Mode),
        CPad::GetPad(0) ? CPad::GetPad(0)->DisablePlayerControls : 0);
}

// GTA's own "get off the bike" for CJ.
void CjOffBikeUnsafe(CVehicle* bike, LogFn log)
{
    CPlayerPed* ped = FindPlayerPed();
    if (!ped || !bike || ped->m_pVehicle != bike || !ped->m_pIntelligence) return;
    ped->m_pIntelligence->m_TaskMgr.SetTask(new CTaskComplexLeaveCar(bike, 0, 0, true, false), TASK_PRIMARY_PRIMARY,
                                            false);
    log("bmx: the rider got off in reTHAWed: CJ gets off the GTA bike too");
}

void CjOffBike(CVehicle* bike, LogFn log)
{
    __try {
        CjOffBikeUnsafe(bike, log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("bmx: couldn't make CJ get off the bike (Triangle does it)");
    }
}

void LogWhyOff(LogFn log)
{
    __try {
        LogWhyOffUnsafe(log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("bmx: CJ off the bike (details unreadable)");
    }
}

void StartUnsafe(LinkBlock* link, LogFn log)
{
    CPlayerPed* ped = FindPlayerPed();
    g_bike = ped->m_pVehicle;
    g_baseDist = g_bike->GetDistanceFromCentreOfMassToBaseOfModel();
    g_savedKnock = ped->CantBeKnockedOffBike;
    ped->CantBeKnockedOffBike = 1; // never: bumps in GTA mustn't throw CJ off while reTHAWed rides
    g_savedCollision = g_bike->bUsesCollision;
    g_bike->bUsesCollision = false; // reTHAWed's world decides where the bike can go
    // GTA's unique stunt jumps play a slow-motion replay with their own camera when
    // the bike flies through one (test 00:51/00:52: "weird camera angles",
    // "kept doing a replay"); off while reTHAWed rides, back on afterwards.
    if (!g_stuntsOff) {
        g_savedStunts = CStuntJumpManager::m_bActive;
        g_stuntsOff = true;
    }
    if (!CStuntJumpManager::mp_Active) CStuntJumpManager::m_bActive = false; // not in the middle of one
    InterlockedExchange(&link->gtaSkateMode, kRideModeBmx); // the THAW side takes GTA's controller now
    g_active = true;
    g_frames = g_held = g_skipped = 0;
    g_maxAge = 0;
    g_nextReport = GetTickCount() + 1000;
    log("bmx: BMX MODE ON: your controller rides reTHAWed's bike, the GTA bike and CJ follow it (F7 / L3 + R3 or "
        "getting off in reTHAWed = GTA's normal BMX)");
}

void EndUnsafe(LinkBlock* link, LogFn log, const char* why)
{
    g_active = false;
    if (link && link->gtaSkateMode == kRideModeBmx) InterlockedExchange(&link->gtaSkateMode, 0);
    if (g_stuntsOff) {
        CStuntJumpManager::m_bActive = g_savedStunts;
        g_stuntsOff = false;
    }
    CPlayerPed* ped = FindPlayerPed();
    if (ped) ped->CantBeKnockedOffBike = g_savedKnock;
    if (g_bike && ped && ped->m_pVehicle == g_bike) {
        g_bike->bUsesCollision = g_savedCollision;
        // Put the bike on GTA's ground under it: reTHAWed's ramps / bowl aren't
        // in GTA, so it was left floating (tests 00:51, 01:01).
        CVector p = g_bike->GetPosition();
        bool found = false;
        const float ground = CWorld::FindGroundZFor3DCoord(p.x, p.y, p.z + 1.0f, &found, nullptr);
        if (found && ground < p.z + 1.0f) {
            CMatrix m;
            m.right = g_bike->m_matrix->right;
            m.up = g_bike->m_matrix->up;
            m.at = g_bike->m_matrix->at;
            m.pos = CVector(p.x, p.y, ground + g_baseDist + 0.05f);
            g_bike->SetMatrix(m);
            if (g_bike->m_matrix) g_bike->m_matrix->UpdateRW();
            g_bike->UpdateRwFrame();
            log("bmx: bike put on GTA's ground (%.2f m down)", p.z - (ground + g_baseDist + 0.05f));
        }
        g_bike->m_vecMoveSpeed = CVector(0.0f, 0.0f, -0.01f);
        g_bike->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
        g_bike->SetIsStatic(false);
    }
    g_bike = nullptr;
    g_placeSeqWanted = 0; // next start places the rider again
    log("bmx: BMX mode off (%s): GTA's normal BMX", why);
}

void End(LinkBlock* link, LogFn log, const char* why)
{
    __try {
        EndUnsafe(link, log, why);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_active = false;
        g_bike = nullptr;
        g_placeSeqWanted = 0;
        if (g_stuntsOff) {
            CStuntJumpManager::m_bActive = g_savedStunts;
            g_stuntsOff = false;
        }
        if (link && link->gtaSkateMode == kRideModeBmx) InterlockedExchange(&link->gtaSkateMode, 0);
        log("bmx: BMX mode off (%s; error while restoring GTA's bike)", why);
    }
}

// One frame: the GTA bike (and so CJ) where reTHAWed's rider is.
void FollowUnsafe(LinkBlock* link, const LevelAlignment& a)
{
    SkaterShared s;
    int retries = 0;
    if (!ReadSkaterShared(link, s, retries)) {
        ++g_skipped;
        return;
    }
    const LONG age = static_cast<LONG>(GetTickCount64() & 0xFFFFFFFFu) - s.sampleTick;
    if (age > g_maxAge) g_maxAge = age;
    const LONG ex = age < 0 ? 0 : (age > kMaxExtrapolateMs ? kMaxExtrapolateMs : age);
    const Vec3 p{s.pos[0] + s.vel[0] * ex / 1000.0f, s.pos[1] + s.vel[1] * ex / 1000.0f,
                 s.pos[2] + s.vel[2] * ex / 1000.0f};
    const Vec3 feet = MapAbsolute(p, a);

    // The GTA bike stays upright and only turns (test 00:51): tilting it with the
    // rider's flips swung GTA's bike camera around and GTA threw CJ off a bike
    // that looked tipped over. Tricks are shown on CJ's body later (B4).
    const V3 up{0, 0, 1};
    V3 fwd{0, 0, 0};
    if (s.flags & kPoseValid) {
        const V3 at = Dir(s.orient + 6, a.ySign);
        fwd = Norm(V3{at.x, at.y, 0.0f});
        if (std::fabs(at.z) > 0.9f) fwd = {0, 0, 0}; // facing straight up / down (vert): no heading from it
    }
    if (fwd.x == 0.0f && fwd.y == 0.0f) { // moving: its direction; else keep the bike's
        const V3 vel = Norm(V3{s.vel[0], s.vel[2] * a.ySign, 0.0f});
        const float speed2 = s.vel[0] * s.vel[0] + s.vel[2] * s.vel[2];
        if (speed2 > 50.0f * 50.0f) {
            fwd = vel;
        } else {
            const CVector& f = g_bike->GetForward();
            fwd = Norm(V3{f.x, f.y, 0.0f});
        }
    }
    if (fwd.x == 0.0f && fwd.y == 0.0f) fwd = {0, 1, 0};
    const V3 right = Norm(Cross(fwd, up));

    CMatrix m;
    m.right = CVector(right.x, right.y, right.z);
    m.up = CVector(fwd.x, fwd.y, fwd.z);
    m.at = CVector(up.x, up.y, up.z);
    m.pos = CVector(feet.x + up.x * g_baseDist, feet.y + up.y * g_baseDist, feet.z + up.z * g_baseDist);
    g_bike->SetMatrix(m);
    if (g_bike->m_matrix) g_bike->m_matrix->UpdateRW();
    g_bike->UpdateRwFrame();
    // GTA speed is metres per 1/50 s; this keeps the wheels and CJ's pedalling going
    const float k = a.scale * 0.02f;
    g_bike->m_vecMoveSpeed = CVector(s.vel[0] * k, s.vel[2] * a.ySign * k, s.vel[1] * k);
    g_bike->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
    ++g_frames;
}

bool FollowSafe(LinkBlock* link, const LevelAlignment& a)
{
    __try {
        FollowUnsafe(link, a);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MenuOpen() { return FrontEndMenuManager.m_bMenuActive || CTimer::m_UserPause || CTimer::m_CodePause; }

// F7 or L3 + R3 pressed this frame (GTA in front)?
bool TogglePressed()
{
    const bool key = (GetAsyncKeyState(kToggleKey) & 0x8000) != 0;
    LONG raw = 0;
    bool combo = false;
    if (PadLinkRawButtons(raw)) {
        combo = (raw & kBtnL3) != 0 && (raw & kBtnR3) != 0;
    } else if (CPad* pad = CPad::GetPad(0)) {
        combo = pad->NewState.ShockButtonL != 0 && pad->NewState.ShockButtonR != 0;
    }
    const bool pressed = GtaInFront() && ((key && !g_keyWas) || (combo && !g_comboWas));
    g_keyWas = key;
    g_comboWas = combo;
    return pressed;
}

void Report(LogFn log, const char* whyNot)
{
    if (whyNot != g_lastWhyNot) {
        g_lastWhyNot = whyNot;
        if (whyNot) log("bmx: BMX mode waits: %s", whyNot);
    }
}

} // namespace

bool BmxModeIsOn() { return g_active; }

void BmxFrame(LinkBlock* link, LogFn log)
{
    if (!link) return;
    const bool riding = !SkateModeIsOn() && CjRidesBmx();
    const DWORD now = GetTickCount();
    const bool toggle = TogglePressed();

    // ---- B1: rider on / off with CJ ----
    if (riding != g_wasRiding) {
        g_wasRiding = riding;
        if (riding) {
            g_weAsked = true;
            g_userOff = false;
            g_askedAt = now;
            log("bmx: CJ got on a BMX: asking reTHAWed's rider to get on the bike");
        } else {
            if (g_active) {
                LogWhyOff(log);
                End(link, log, "CJ got off the bike");
            }
            g_placeSeqWanted = 0;
            g_lastWhyNot = nullptr;
            if (g_weAsked) {
                g_askedAt = now;
                log("bmx: CJ got off the BMX: asking reTHAWed's rider to get off the bike");
            }
        }
    }
    if (riding && toggle) {
        if (g_active) {
            g_userOff = true;
            End(link, log, "F7 / L3 + R3");
        } else if (g_userOff) {
            g_userOff = false;
            log("bmx: F7 / L3 + R3: BMX mode back on for this ride");
        }
    }
    LONG wish = kBikeLeave;
    if (riding) {
        // While BMX mode is on, the rider is the player's: we don't put him back
        // on the bike (test 01:14: L1 + R1 got off, we put him straight back on).
        wish = (g_userOff || g_active) ? kBikeLeave : kBikeOn;
    } else if (g_weAsked) {
        wish = kBikeOff;
        if (link->thawBikeState == kBikeStateOff) g_weAsked = false; // done
    }
    InterlockedExchange(&link->gtaBikeWish, wish);

    const LONG thaw = link->thawBikeState;
    if (thaw != g_lastThaw) {
        static const char* const kState[] = {"unknown", "off the bike", "ON the bike"};
        if (g_lastThaw != -1 || thaw != kBikeStateUnknown)
            log("bmx: reTHAWed's rider is now %s (%.1f s after our last ask)",
                (thaw >= 0 && thaw <= 2) ? kState[thaw] : "?", (now - g_askedAt) / 1000.0f);
        g_lastThaw = thaw;
    }
    if (g_checkAt && static_cast<LONG>(now - g_checkAt) >= 0) {
        g_checkAt = 0;
        CheckPlacement(link, log);
    }
    if (!riding) return;

    // ---- B3: BMX mode ----
    const LevelAlignment* align = FindLevelAlignment(static_cast<unsigned long>(link->thawLevelId));
    const char* gta = GtaInCharge();
    if (g_active) {
        CPlayerPed* ped = FindPlayerPed();
        if (!ped || ped->m_pVehicle != g_bike) { End(link, log, "not the same bike any more"); return; }
        if (thaw == kBikeStateOff) {
            if (!g_riderOffSince) g_riderOffSince = now ? now : 1;
            if (now - g_riderOffSince >= kRiderOffMs) {
                g_userOff = true;
                g_riderOffSince = 0;
                CVehicle* bike = g_bike;
                End(link, log, "the rider got off the bike in reTHAWed");
                CjOffBike(bike, log); // CJ gets off too (user 01:14)
                return;
            }
        } else {
            g_riderOffSince = 0;
        }
        // GTA's cinematic car camera (dramatic angles): not while reTHAWed rides
        if (TheCamera.m_bObbeCinematicCarCamOn) {
            TheCamera.m_bObbeCinematicCarCamOn = false;
            if (!g_cinemaLogged) {
                g_cinemaLogged = true;
                log("bmx: GTA had started its cinematic car camera; switched it off");
            }
        }
        if (CReplay::Mode != 0 && !g_replaySeen) {
            g_replaySeen = 1;
            log("bmx: GTA's replay is running (mode %d)", static_cast<int>(CReplay::Mode));
        }
        if (gta) { End(link, log, gta); return; }
        if (!align) { End(link, log, "reTHAWed's level isn't lined up with GTA's world"); return; }
        if (link->thawPid == 0) { End(link, log, "reTHAWed not connected"); return; }
        // stunt jumps stay off (one that was running at the start has ended by now)
        if (g_stuntsOff && !CStuntJumpManager::mp_Active) CStuntJumpManager::m_bActive = false;
        if (MenuOpen()) {
            ++g_held;
        } else if (!FollowSafe(link, *align)) {
            End(link, log, "error while moving the bike");
            return;
        }
        if (static_cast<LONG>(now - g_nextReport) >= 0) {
            g_nextReport = now + 1000;
            log("bmx: %u frames followed, %u held (menu), %u skipped (sample busy), newest sample up to %ld ms old",
                g_frames, g_held, g_skipped, g_maxAge);
            g_frames = g_held = g_skipped = 0;
            g_maxAge = 0;
        }
        return;
    }

    // not on yet: everything in place?
    const char* whyNot = nullptr;
    if (g_userOff) whyNot = "switched off for this ride (F7 / L3 + R3 to switch it back on)";
    else if (thaw != kBikeStateOn) whyNot = "reTHAWed's rider isn't on the bike yet";
    else if (!align) whyNot = "reTHAWed's level isn't lined up with GTA's world (normal GTA BMX here)";
    else if (gta) whyNot = gta;
    else if (MenuOpen()) whyNot = "GTA menu open";
    Report(log, whyNot);
    if (whyNot) {
        if (gta) g_placeSeqWanted = 0; // GTA / a mission may move CJ: place the rider again afterwards
        return;
    }
    if (g_placeSeqWanted == 0) {
        if (!PlaceRider(link, *align, log)) return;
    }
    if (link->thawPlaceDone != g_placeSeqWanted) return; // reTHAWed hasn't moved the rider yet
    __try {
        StartUnsafe(link, log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_active = false;
        log("bmx: error while starting BMX mode");
    }
}

} // namespace mp::gta
