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
#include "CTaskSimpleCarSetPedOut.h"
#include "CCarEnterExit.h"
#include "CPools.h"
#include "CCheat.h"
#include "CTaskManager.h"
#include "CReplay.h"
#include "CStuntJumpManager.h"
#include "CTimer.h"
#include "CVehicle.h"
#include "CBmx.h"
#include "NodeName.h"
#include "CWorld.h"
#include "common.h"
#include <windows.h>

#include "bmxmode.h"
#include "body.h"
#include "padlink.h"
#include "blend.h"
#include "../common/convert.h"

#include <cmath>
#include <cstddef>

namespace mp::gta {
namespace {

constexpr int kBmxModel = 481;
constexpr int kToggleKey = VK_F7;                 // same keys as skate mode
constexpr unsigned short kSkateControlBit = 1u << 4; // skate mode's own control flag (not a script's)
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
unsigned g_blended = 0;          // B5: frames shown blended between two reTHAWed frames
RiderBlend g_blend;              // B5: skate mode's smoothing (blend.cpp)

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
V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 Mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// A rotation as its three axes in world space (columns): right, forward, up.
struct Basis {
    V3 r, f, u;
};
V3 Apply(const Basis& b, V3 v) { return Add(Add(Mul(b.r, v.x), Mul(b.f, v.y)), Mul(b.u, v.z)); } // local -> world
V3 Local(const Basis& b, V3 w) { return {Dot(b.r, w), Dot(b.f, w), Dot(b.u, w)}; }                 // world -> local

// ---- B4b: the GTA bike drawn where reTHAWed's bike is ----
// When BMX mode starts (rider standing on the bike) we note how reTHAWed's bike
// ("board" bone) sits relative to the GTA bike; then each frame the GTA bike's
// picture gets the same turn and shift as reTHAWed's bike since then. Only the
// picture: the real GTA bike stays upright (camera, physics; test 00:51).
constexpr int kCalibrateAfter = 10;   // frames into BMX mode (rider settled)
int g_calFrames = 0;
bool g_calibrated = false;
Basis g_board0, g_bike0;              // at calibration
V3 g_offLocal{0, 0, 0};               // GTA bike centre in reTHAWed bike's own space
bool g_haveVisual = false;            // a picture placement for this frame
Basis g_vis;
V3 g_visPos{0, 0, 0};
ULONGLONG g_visTick = 0;
bool g_swapped = false;               // the bike's matrix is the picture's right now
float g_saved[12];                    // what it really was (restored after drawing)

bool g_calLogPending = false;
unsigned g_drawnTurned = 0;
// STATUS 01:55: turning the bike's picture is off until we know how reTHAWed's
// bike sits (both lined-up attempts were off); the research log below decides.
constexpr bool kTurnBikePicture = true;   // fix 2: from the research log
constexpr bool kTurnParts = true;         // B7: fork / bars / cranks / pedals like reTHAWed's
constexpr float kBoardRestHeight = 17.8f; // board bone above the rider's feet when riding normally (THAW units, log 01:59)
float g_lastBoard[12] = {};           // research log: board bone rows in the rider's space
// ---- B7: the GTA bike's parts follow reTHAWed's (fork, cranks), CJ's hands and feet on them ----
RwFrame* g_node[BMX_NUM_NODES] = {};  // the GTA BMX's parts (plugin-sdk eBmxNodes)
V3 g_nodePos[BMX_NUM_NODES] = {};     // their positions in the parent's space, at the start
bool g_haveNodes = false;
V3 g_grip[2] = {};                    // grips (bike's right, left) in chassis_dummy's space, fork straight
bool g_gripMeasured = false;
bool g_haveParts = false;             // fork / crank for this frame
float g_forkG[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // the fork's turn in GTA bike space (rows)
V3 g_crankG{0, 0, -1};                // axle -> right pedal, GTA bike space
RwMatrix g_savedNode[BMX_NUM_NODES];  // restored after drawing
bool g_nodesSwapped = false;
unsigned g_partsDrawn = 0, g_limbFrames = 0;
float g_barsDrawnMax = 0.0f;          // log: biggest bars turn GTA really drew this second (degrees, from its world matrices)
unsigned g_barsChecked = 0;
float g_onMin[4] = {1, 1, 1, 1};      // log: lowest "on" this second (hands R, L, feet R, L)
constexpr float kGripGuessX = 0.27f, kGripGuessZ = 0.10f; // if the bars' mesh can't be read
constexpr float kLimbEaseMs = 100.0f;  // B10: hands / feet "on" amounts ease with this time constant
// B11: BMX <-> skate the quick way
int g_lastBikeRef = -1;          // the GTA BMX CJ last left for skate mode (GTA's vehicle handle)
DWORD g_l1r1At = 0;              // when L1 + R1 were last held in BMX mode (the get-off buttons)
bool g_quickStart = false;       // CJ was put on the bike because reTHAWed's rider already is: no waiting
LONG g_skateThawPrev = kBikeStateUnknown;
constexpr DWORD kL1R1RecentMs = 2000;
// B12: the GTA bike CJ left lies where reTHAWed's bike lies (skate mode)
bool g_mirroring = false;
CVehicle* g_mirrorBike = nullptr;
bool g_mirrorSavedCollision = true;
unsigned g_mirrorFrames = 0;
// B10 fix 1: where the GTA bike stays once reTHAWed's rider is off it
bool g_holdHave = false;
CMatrix g_holdMatrix;
unsigned g_heldOff = 0;
float g_lastSpeed = 0.0f;
bool g_haveBoardLog = false;
Basis g_boardStart;                   // for the log only (fix 1)
float g_boardTurnMax = 0.0f, g_bodyTurnMax = 0.0f;

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

// B11 (user 14:25: in reTHAWed the board rolls to the rider and he hops on):
// CJ off the bike at once, no GTA get-off animation, so skate mode can copy
// reTHAWed's hop-on right away. Next to the bike; skate mode then puts him at
// the rider's spot. False if GTA kept him on (then the old way).
bool CjWarpOffBikeUnsafe(CVehicle* bike, LogFn log)
{
    CPlayerPed* ped = FindPlayerPed();
    if (!ped || !bike || ped->m_pVehicle != bike) return false;
    CTaskSimpleCarSetPedOut out(bike, 0, false);
    out.m_bWarpingOutOfCar = true;
    out.ProcessPed(ped);
    CCarEnterExit::RemoveCarSitAnim(ped);
    const CVector p = bike->GetPosition();
    ped->Teleport(CVector(p.x, p.y, p.z + 0.6f), false);
    const bool off = !ped->bInVehicle;
    log("bmx: B11: CJ %s the GTA bike at once", off ? "taken off" : "NOT taken off (GTA kept him on)");
    return off;
}

bool CjWarpOffBike(CVehicle* bike, LogFn log)
{
    __try {
        return CjWarpOffBikeUnsafe(bike, log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("bmx: B11: error while taking CJ off the bike");
        return false;
    }
}

// B11: the GTA BMX to put CJ on when reTHAWed's rider gets on his bike in
// skate mode: the one he left (within 30 m), else the nearest free BMX within
// 15 m, else a new one (GTA's own vehicle cheat).
CVehicle* FindOrSpawnBmx(CPlayerPed* ped, const char*& from)
{
    const CVector c = ped->GetPosition();
    auto dist = [&](CVehicle* v) { const CVector p = v->GetPosition(); return (p - c).Magnitude(); };
    if (g_lastBikeRef >= 0) {
        CVehicle* v = CPools::GetVehicle(g_lastBikeRef);
        if (v && v->m_nModelIndex == kBmxModel && !v->m_pDriver && dist(v) < 30.0f) {
            from = "the BMX CJ left";
            return v;
        }
    }
    CVehicle* best = nullptr;
    float bestD = 15.0f;
    for (int i = 0; i < CPools::ms_pVehiclePool->m_nSize; ++i) {
        CVehicle* v = CPools::ms_pVehiclePool->GetAt(i);
        if (!v || v->m_nModelIndex != kBmxModel || v->m_pDriver) continue;
        const float d = dist(v);
        if (d < bestD) { bestD = d; best = v; }
    }
    if (best) {
        from = "the nearest BMX";
        return best;
    }
    from = "a new BMX";
    return CCheat::VehicleCheat(kBmxModel);
}

// B12: stop putting the bike where reTHAWed's is (its collision back on)
void StopMirror(LogFn log)
{
    if (!g_mirroring) return;
    g_mirroring = false;
    __try {
        if (g_mirrorBike) {
            g_mirrorBike->bUsesCollision = g_mirrorSavedCollision;
            g_mirrorBike->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (log) log("bmx: B12: the GTA bike no longer follows reTHAWed's dropped bike (%u frames)", g_mirrorFrames);
    g_mirrorBike = nullptr;
    g_mirrorFrames = 0;
}

// B12 (user 14:37: CJ bent down for a bike that wasn't there): in skate mode,
// the GTA bike CJ left is put where reTHAWed's bike lies, and moves with it
// when the rider picks it up (Square).
void DroppedBikeFrameUnsafe(LinkBlock* link, LogFn log)
{
    const LevelAlignment* a = FindLevelAlignment(static_cast<unsigned long>(link->thawLevelId));
    SkaterShared s;
    int retries = 0;
    const bool want = a && ReadSkaterShared(link, s, retries) && (s.flags & kDroppedBikeValid);
    CVehicle* bike = (want && g_lastBikeRef >= 0) ? CPools::GetVehicle(g_lastBikeRef) : nullptr;
    if (bike && (bike->m_nModelIndex != kBmxModel || bike->m_pDriver)) bike = nullptr;
    if (!bike) {
        StopMirror(log);
        return;
    }
    if (!g_mirroring || g_mirrorBike != bike) {
        StopMirror(log);
        g_mirrorBike = bike;
        g_mirrorSavedCollision = bike->bUsesCollision;
        bike->bUsesCollision = false; // it may lie on reTHAWed's ramps; GTA mustn't knock it about
        g_mirroring = true;
        log("bmx: B12: the GTA bike now lies where reTHAWed's bike lies");
    }
    const float* d = s.droppedBike; // board convention, THAW world
    const V3 r = Norm(Dir(d, a->ySign));
    const V3 f = Mul(Norm(Dir(d + 3, a->ySign)), -1.0f); // the "up" row points to the back wheel
    const V3 u = Norm(Dir(d + 6, a->ySign));
    const Vec3 p = MapAbsolute(Vec3{d[9], d[10], d[11]}, *a);
    const float base = bike->GetDistanceFromCentreOfMassToBaseOfModel();
    const V3 pos = Add(V3{p.x, p.y, p.z}, Mul(u, base - kBoardRestHeight * a->scale)); // as BMX mode's picture
    CMatrix m;
    m.right = CVector(r.x, r.y, r.z);
    m.up = CVector(f.x, f.y, f.z);
    m.at = CVector(u.x, u.y, u.z);
    m.pos = CVector(pos.x, pos.y, pos.z);
    bike->SetMatrix(m);
    bike->UpdateRwMatrix();
    bike->UpdateRwFrame();
    bike->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    bike->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
    ++g_mirrorFrames;
}

void DroppedBikeFrame(LinkBlock* link, LogFn log)
{
    __try {
        DroppedBikeFrameUnsafe(link, log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_mirroring = false;
        g_mirrorBike = nullptr;
    }
}

void MountFromSkateUnsafe(LinkBlock* link, LogFn log)
{
    CPlayerPed* ped = FindPlayerPed();
    if (!ped || ped->bInVehicle) return;
    const char* from = "?";
    CVehicle* bike = FindOrSpawnBmx(ped, from);
    if (!bike) {
        log("bmx: B11: reTHAWed's rider got on his bike, but no GTA BMX could be found or made");
        return;
    }
    const CVector c = ped->GetPosition();
    const float heading = ped->GetHeading();
    const bool mirrored = g_mirroring && g_mirrorBike == bike; // B12: already where reTHAWed's bike is
    StopMirror(log);
    SkateModeTurnOffNow(link, log, "reTHAWed's rider got on his bike (Square): BMX mode");
    if (!mirrored) {
        // the bike where CJ is, facing his way, on the ground
        bool found = false;
        float ground = CWorld::FindGroundZFor3DCoord(c.x, c.y, c.z + 1.0f, &found, nullptr);
        if (!found) ground = c.z - 1.0f;
        bike->SetPosn(c.x, c.y, ground + bike->GetDistanceFromCentreOfMassToBaseOfModel());
        bike->SetHeading(heading);
    }
    bike->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    bike->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
    bike->UpdateRwMatrix();
    bike->UpdateRwFrame();
    CCarEnterExit::SetPedInCarDirect(ped, bike, 0, true);
    g_lastBikeRef = CPools::GetVehicleRef(bike);
    g_quickStart = ped->bInVehicle && ped->m_pVehicle == bike;
    log("bmx: B11: reTHAWed's rider got on his bike: CJ put on %s at once (%s)%s", from,
        g_quickStart ? "on" : "NOT on: GTA refused", mirrored ? ", where reTHAWed's bike lay (B12)" : "");
}

void MountFromSkate(LinkBlock* link, LogFn log)
{
    __try {
        MountFromSkateUnsafe(link, log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("bmx: B11: error while putting CJ on a bike");
    }
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

// B7: the bike's parts and where its grips are (outermost points of the bars'
// mesh, in chassis_dummy's space with the fork straight). Once per BMX start.
struct GripScan {
    float maxX = -1e9f, minX = 1e9f;
    V3 sumR{0, 0, 0}, sumL{0, 0, 0};
    int nR = 0, nL = 0;
};

// a point in `f`'s space -> chassis_dummy's space; forks_front counts without
// its turn (GTA's steering), so the grips are measured with the fork straight
bool ToChassis(RwFrame* f, V3& p)
{
    for (int depth = 0; f && depth < 16; ++depth) {
        if (f == g_node[BMX_CHASSIS]) return true;
        const RwMatrix& m = f->modelling;
        if (f == g_node[BMX_FORKS_FRONT]) {
            p = Add(p, V3{m.pos.x, m.pos.y, m.pos.z});
        } else {
            p = V3{p.x * m.right.x + p.y * m.up.x + p.z * m.at.x + m.pos.x,
                   p.x * m.right.y + p.y * m.up.y + p.z * m.at.y + m.pos.y,
                   p.x * m.right.z + p.y * m.up.z + p.z * m.at.z + m.pos.z};
        }
        f = static_cast<RwFrame*>(f->object.parent);
    }
    return false;
}

bool Under(RwFrame* f, RwFrame* top)
{
    for (int depth = 0; f && depth < 16; ++depth, f = static_cast<RwFrame*>(f->object.parent))
        if (f == top) return true;
    return false;
}

// pass 0: outermost x; pass 1: average the points near each end
void ScanAtomics(RpClump* clump, RwFrame* top, GripScan& g, int pass)
{
    RwLLLink* end = &clump->atomicList.link;
    int guard = 0;
    for (RwLLLink* l = end->next; l && l != end && guard < 256; l = l->next, ++guard) {
        RpAtomic* at = reinterpret_cast<RpAtomic*>(reinterpret_cast<char*>(l) - offsetof(RpAtomic, inClumpLink));
        RwFrame* f = static_cast<RwFrame*>(at->object.object.parent);
        if (!f || !Under(f, top) || !at->geometry || !at->geometry->morphTarget) continue;
        const RwV3d* v = at->geometry->morphTarget[0].verts;
        const int n = at->geometry->numVertices;
        if (!v || n <= 0 || n > 20000) continue;
        for (int i = 0; i < n; ++i) {
            V3 p{v[i].x, v[i].y, v[i].z};
            if (!ToChassis(f, p)) break;
            if (pass == 0) {
                if (p.x > g.maxX) g.maxX = p.x;
                if (p.x < g.minX) g.minX = p.x;
            } else if (p.x > g.maxX - 0.06f) {
                g.sumR = Add(g.sumR, p);
                ++g.nR;
            } else if (p.x < g.minX + 0.06f) {
                g.sumL = Add(g.sumL, p);
                ++g.nL;
            }
        }
    }
}

void LearnParts(LogFn log)
{
    g_haveNodes = false;
    g_gripMeasured = false;
    if (!g_bike) return;
    CBike* bike = static_cast<CBike*>(g_bike);
    for (int i = 1; i < BMX_NUM_NODES; ++i) {
        g_node[i] = bike->m_aBikeNodes[i];
        if (g_node[i]) {
            const RwV3d& p = g_node[i]->modelling.pos;
            g_nodePos[i] = V3{p.x, p.y, p.z};
        }
    }
    g_haveNodes = g_node[BMX_CHASSIS] && g_node[BMX_FORKS_FRONT] && g_node[BMX_CHAINSET] && g_node[BMX_PEDAL_R] &&
                  g_node[BMX_PEDAL_L];
    if (!g_haveNodes) {
        log("bmx parts: B7 off: the GTA bike's parts weren't found");
        return;
    }
    RpClump* clump = g_bike->m_pRwClump;
    GripScan g;
    const char* from = "guess";
    for (RwFrame* top : {g_node[BMX_HANDLEBARS], g_node[BMX_FORKS_FRONT]}) {
        if (!top || !clump) continue;
        g = GripScan{};
        ScanAtomics(clump, top, g, 0);
        if (g.maxX - g.minX < 0.2f) continue; // nothing wide enough there
        ScanAtomics(clump, top, g, 1);
        if (g.nR > 0 && g.nL > 0) {
            g_grip[0] = Mul(g.sumR, 1.0f / g.nR);
            g_grip[1] = Mul(g.sumL, 1.0f / g.nL);
            g_gripMeasured = true;
            from = top == g_node[BMX_HANDLEBARS] ? "the handlebars' mesh" : "the fork's mesh";
            break;
        }
    }
    if (!g_gripMeasured) {
        const V3 f = g_nodePos[BMX_FORKS_FRONT];
        g_grip[0] = V3{kGripGuessX, f.y, f.z + kGripGuessZ};
        g_grip[1] = V3{-kGripGuessX, f.y, f.z + kGripGuessZ};
    }
    log("bmx parts: B7 grips (from %s): right %.3f %.3f %.3f, left %.3f %.3f %.3f (chassis space, m); fork pivot "
        "%.3f %.3f %.3f; crank axle %.3f %.3f %.3f", from, g_grip[0].x, g_grip[0].y, g_grip[0].z, g_grip[1].x,
        g_grip[1].y, g_grip[1].z, g_nodePos[BMX_FORKS_FRONT].x, g_nodePos[BMX_FORKS_FRONT].y,
        g_nodePos[BMX_FORKS_FRONT].z, g_nodePos[BMX_CHAINSET].x, g_nodePos[BMX_CHAINSET].y, g_nodePos[BMX_CHAINSET].z);
}

// B7: reTHAWed's bike parts -> GTA bike space. reTHAWed's bike frame space is
// (right, towards the back, up); GTA's bike is (right, forward, up): flip y.
void PartsFromSample(const SkaterShared& s)
{
    g_haveParts = g_haveNodes && (s.flags & kBikeValid);
    if (!g_haveParts) return;
    static const float sgn[3] = {1.0f, -1.0f, 1.0f};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) g_forkG[i * 3 + j] = s.bike[kBikeFork + i * 3 + j] * sgn[i] * sgn[j];
    const float* c = s.bike + kBikeCrank;
    V3 u{0.0f, -c[1], c[2]}; // the crank turns about the bike's x: keep it in the y-z plane
    u = Norm(u);
    g_crankG = (u.y == 0.0f && u.z == 0.0f) ? V3{0, 0, -1} : u;
}

// a point in chassis_dummy's space (as we draw it: unturned) -> world, on the drawn bike
V3 ChassisToWorld(V3 p)
{
    const V3 local = Add(p, g_nodePos[BMX_CHASSIS]);
    return Add(g_visPos, Apply(g_vis, local));
}

// B7: grips and pedals in the world on the drawn bike; CJ's hands / feet go there
void LimbTargets(const SkaterShared& s)
{
    if (!g_haveParts || !g_haveVisual) {
        BodyClearLimbTargets();
        return;
    }
    const V3 pivot = g_nodePos[BMX_FORKS_FRONT];
    float t[4][3], w[4];
    for (int k = 0; k < 2; ++k) {
        const V3 d = Sub(g_grip[k], pivot);
        const float* m = g_forkG;
        const V3 turned{d.x * m[0] + d.y * m[3] + d.z * m[6], d.x * m[1] + d.y * m[4] + d.z * m[7],
                        d.x * m[2] + d.y * m[5] + d.z * m[8]};
        const V3 g = ChassisToWorld(Add(pivot, turned));
        t[k][0] = g.x; t[k][1] = g.y; t[k][2] = g.z;
    }
    const V3 axle = g_nodePos[BMX_CHAINSET];
    const V3 pr = g_nodePos[BMX_PEDAL_R], pl = g_nodePos[BMX_PEDAL_L]; // in the chainset's space: (x, 0, along the crank)
    const V3 right{1, 0, 0};
    const V3 pedR = Add(axle, Add(Mul(right, pr.x), Mul(g_crankG, pr.z)));
    const V3 pedL = Add(axle, Add(Mul(right, pl.x), Mul(g_crankG, pl.z)));
    const V3 wr = ChassisToWorld(pedR), wl = ChassisToWorld(pedL);
    t[2][0] = wr.x; t[2][1] = wr.y; t[2][2] = wr.z;
    t[3][0] = wl.x; t[3][1] = wl.y; t[3][2] = wl.z;
    // B10 (user 14:11: awkward when CJ starts pedalling again): the "on" amounts
    // ease in / out over ~0.1 s instead of switching in a few frames
    static float smooth[4] = {};
    static ULONGLONG lastMs = 0;
    const ULONGLONG nowMs = GetTickCount64();
    const float dt = lastMs ? static_cast<float>(nowMs - lastMs) : 0.0f;
    const bool fresh = !lastMs || dt > 500.0f; // first frame of a ride: no easing from old values
    lastMs = nowMs;
    const float a = fresh ? 1.0f : 1.0f - std::exp(-dt / kLimbEaseMs);
    for (int k = 0; k < 4; ++k) {
        float want = s.bike[kBikeHandOn + k]; // hands R, L, feet R, L (kBikeFootOn follows kBikeHandOn)
        if (!(want >= 0.0f)) want = 0.0f;
        if (want > 1.0f) want = 1.0f;
        smooth[k] += (want - smooth[k]) * a;
        w[k] = smooth[k];
        if (w[k] < g_onMin[k]) g_onMin[k] = w[k];
    }
    BodySetLimbTargets(t, w);
    ++g_limbFrames;
}

// B6 research (read only): the GTA BMX's moving parts, once per game session:
// name, parent, position and axes in its parent's space.
void LogBikeParts(LogFn log)
{
    static bool done = false;
    if (done || !g_bike) return;
    done = true;
    static const char* const kWhat[BMX_NUM_NODES] = {"-", "chassis", "forks_front", "forks_rear", "wheel_front",
                                                     "wheel_rear", "handlebars", "chainset", "pedal_r", "pedal_l"};
    CBike* bike = static_cast<CBike*>(g_bike);
    log("bmx parts: the GTA BMX's moving parts (B6 research)");
    for (int i = 1; i < BMX_NUM_NODES; ++i) {
        RwFrame* f = bike->m_aBikeNodes[i];
        if (!f) {
            log("bmx parts:   %d %s: none", i, kWhat[i]);
            continue;
        }
        RwFrame* parent = static_cast<RwFrame*>(f->object.parent);
        const char* name = GetFrameNodeName(f);
        const char* pname = parent ? GetFrameNodeName(parent) : nullptr;
        const RwMatrix& m = f->modelling;
        log("bmx parts:   %d %s = '%s' (parent '%s'): pos %.3f %.3f %.3f | right %.2f %.2f %.2f | up %.2f %.2f %.2f | "
            "at %.2f %.2f %.2f", i, kWhat[i], name ? name : "?", pname ? pname : "-", m.pos.x, m.pos.y, m.pos.z,
            m.right.x, m.right.y, m.right.z, m.up.x, m.up.y, m.up.z, m.at.x, m.at.y, m.at.z);
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
    g_calibrated = false;
    g_calFrames = 0;
    g_haveVisual = false;
    g_frames = g_held = g_skipped = g_blended = 0;
    g_maxAge = 0;
    g_blend.Clear();
    g_holdHave = false;
    g_quickStart = false;
    g_nextReport = GetTickCount() + 1000;
    LogBikeParts(log);
    LearnParts(log);
    g_haveParts = false;
    log("bmx: BMX MODE ON: your controller rides reTHAWed's bike, the GTA bike and CJ follow it (F7 / L3 + R3 or "
        "getting off in reTHAWed = GTA's normal BMX)");
}

void EndUnsafe(LinkBlock* link, LogFn log, const char* why)
{
    g_active = false;
    g_haveVisual = false;
    BodyClear(); // B4: CJ's own riding animation again
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
            g_bike->UpdateRwMatrix(); // GTA's CEntity::UpdateRW
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
        BodyClear();
        if (g_stuntsOff) {
            CStuntJumpManager::m_bActive = g_savedStunts;
            g_stuntsOff = false;
        }
        if (link && link->gtaSkateMode == kRideModeBmx) InterlockedExchange(&link->gtaSkateMode, 0);
        log("bmx: BMX mode off (%s; error while restoring GTA's bike)", why);
    }
}

// reTHAWed's bike ("board" bone, rows right, up, at, position in the rider's
// own space) in GTA's world: its axes and position. As skate mode's board.
void BoardFrame(const SkaterShared& s, const LevelAlignment& a, const Vec3& feet, Basis& out, V3& pos)
{
    const float* o = s.orient;
    const float* b = s.board;
    auto toWorld = [o](const float* v, float* w) {
        w[0] = v[0] * o[0] + v[1] * o[3] + v[2] * o[6];
        w[1] = v[0] * o[1] + v[1] * o[4] + v[2] * o[7];
        w[2] = v[0] * o[2] + v[1] * o[5] + v[2] * o[8];
    };
    float w[3];
    toWorld(b + 0, w);
    out.r = Norm(Dir(w, a.ySign));
    toWorld(b + 3, w);
    out.f = Mul(Norm(Dir(w, a.ySign)), -1.0f); // the bone's "up" points to the tail
    toWorld(b + 6, w);
    out.u = Norm(Dir(w, a.ySign));
    toWorld(b + 9, w);
    const V3 off = Mul(Dir(w, a.ySign), a.scale);
    pos = {feet.x + off.x, feet.y + off.y, feet.z + off.z};
}

// The rider's hips and torso in GTA's world (fix 1, test 01:40: the board bone
// doesn't move with reTHAWed's bike): origin = hips, up = hips to neck,
// right = left thigh to right thigh, forward = up x right... made square.
bool BodyFrame(const SkaterShared& s, const LevelAlignment& a, const Vec3& feet, Basis& out, V3& pos)
{
    const float* o = s.orient;
    auto world = [&](int bone) {
        const float* v = s.body + bone * 3;
        float w[3] = {v[0] * o[0] + v[1] * o[3] + v[2] * o[6], v[0] * o[1] + v[1] * o[4] + v[2] * o[7],
                      v[0] * o[2] + v[1] * o[5] + v[2] * o[8]};
        const V3 d = Mul(Dir(w, a.ySign), a.scale);
        return V3{feet.x + d.x, feet.y + d.y, feet.z + d.z};
    };
    constexpr int kPelvis = 0, kNeck = 2, kThighL = 10, kThighR = 14; // order: src/thaw/pose.cpp kBodyBones
    pos = world(kPelvis);
    const V3 up = Norm(Sub(world(kNeck), pos));
    V3 side = Sub(world(kThighR), world(kThighL));
    side = Norm(Sub(side, Mul(up, Dot(side, up))));
    if ((up.x == 0 && up.y == 0 && up.z == 0) || (side.x == 0 && side.y == 0 && side.z == 0)) return false;
    out.u = up;
    out.r = side;
    out.f = Norm(Cross(up, side));
    return true;
}

// One frame: the GTA bike (and so CJ) where reTHAWed's rider is.
// B10 fix 1 (test 14:17: the GTA bike "rolled up like a board" while getting
// off): from the moment reTHAWed's rider is off the bike, the GTA bike stops
// following (what we'd copy is the rider's skateboard by then) and stays
// where it last was, upright; CJ goes back to GTA's riding pose until he gets off.

void HoldBike()
{
    g_haveVisual = false;
    g_haveParts = false;
    BodyClear();
    if (g_holdHave) {
        g_bike->SetMatrix(g_holdMatrix);
        g_bike->UpdateRwMatrix();
        g_bike->UpdateRwFrame();
    }
    g_bike->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    g_bike->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
    ++g_heldOff;
}

void FollowUnsafe(LinkBlock* link, const LevelAlignment& a)
{
    if (link->thawBikeState != kBikeStateOn) { // B10 fix 1
        HoldBike();
        return;
    }
    SkaterShared s;
    int retries = 0;
    if (!ReadSkaterShared(link, s, retries)) {
        ++g_skipped;
        return;
    }
    const LONG age = static_cast<LONG>(GetTickCount64() & 0xFFFFFFFFu) - s.sampleTick;
    if (age > g_maxAge) g_maxAge = age;
    // B5: no guess ahead any more (speed x sample age: ages are only known in
    // 16 ms steps, so the guess jumped; the skate stutter, 8.18). Instead skate
    // mode's cure: reTHAWed shown 40 ms late, blended, at even moments.
    if (g_blend.Show(s, s) >= 0.0f) ++g_blended;
    const Vec3 p{s.pos[0], s.pos[1], s.pos[2]};
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
    g_holdMatrix = m; // B10 fix 1: where it stays if the rider gets off
    g_holdHave = true;
    g_bike->UpdateRwMatrix(); // GTA's CEntity::UpdateRW
    g_bike->UpdateRwFrame();
    // GTA speed is metres per 1/50 s; this keeps the wheels and CJ's pedalling going
    const float k = a.scale * 0.02f;
    g_bike->m_vecMoveSpeed = CVector(s.vel[0] * k, s.vel[2] * a.ySign * k, s.vel[1] * k);
    g_bike->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
    // B4b: where the GTA bike's picture goes (reTHAWed's bike's turn and shift
    // since BMX mode started, applied to the GTA bike as it was then)
    // B4b fix 2 (research log 01:59): reTHAWed's "board" bone IS its bike, set up
    // like the skateboard: right = bike's right, its "up" points to the back
    // wheel, its "at" is the bike's up; riding normally it sits kBoardRestHeight
    // units above the rider's feet, facing the rider's way. So the GTA bike's
    // picture = that bone, its centre lifted so the wheels sit where they do at
    // rest. Fixed numbers, no lining up at run time.
    if (s.flags & kPoseValid) {
        Basis bb;
        V3 bp;
        BoardFrame(s, a, feet, bb, bp);
        for (int i = 0; i < 12; ++i) g_lastBoard[i] = s.board[i];
        g_lastSpeed = std::sqrt(s.vel[0] * s.vel[0] + s.vel[1] * s.vel[1] + s.vel[2] * s.vel[2]) * a.scale;
        g_haveBoardLog = true;
        g_vis = bb;
        g_visPos = Add(bp, Mul(bb.u, g_baseDist - kBoardRestHeight * a.scale));
        g_visTick = GetTickCount64();
        g_haveVisual = kTurnBikePicture;
    } else {
        g_haveVisual = false;
    }
    // B4: CJ's body copies the rider's (tricks, bails), applied when GTA draws
    // him; the same code as skate mode (body.cpp). Rider's origin = feet.
    if ((s.flags & kBodyValid) && (s.flags & kPoseValid)) {
        BodySetTarget(s.body, s.orient, a.scale, a.ySign, feet.x, feet.y, feet.z);
        // B7: bars and cranks like reTHAWed's; CJ's hands and feet on them
        PartsFromSample(s);
        LimbTargets(s);
    } else {
        BodyClear();
        g_haveParts = false;
    }
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

void SetMatrixRows(CVehicle* v, const float* m12)
{
    CMatrixLink& m = *v->m_matrix;
    m.right = CVector(m12[0], m12[1], m12[2]);
    m.up = CVector(m12[3], m12[4], m12[5]);
    m.at = CVector(m12[6], m12[7], m12[8]);
    m.pos = CVector(m12[9], m12[10], m12[11]);
    // GTA's own CEntity::UpdateRW (plugin-sdk UpdateRwMatrix): copies the matrix
    // into the RenderWare frame the bike is drawn from. (m.UpdateRW() did nothing:
    // the matrix isn't attached to that frame; F5 test 12:30 showed no change.)
    v->UpdateRwMatrix();
    v->UpdateRwFrame();
}

// B7: set one part's turn (rows), keep its position; RenderWare recomputes it
// when the bike's frame is updated (SetMatrixRows).
void SetNodeRows(RwFrame* f, V3 r, V3 u, V3 a)
{
    RwMatrix& m = f->modelling;
    m.right = RwV3d{r.x, r.y, r.z};
    m.up = RwV3d{u.x, u.y, u.z};
    m.at = RwV3d{a.x, a.y, a.z};
    RwMatrixUpdate(&m);       // "changed"
    RwFrameUpdateObjects(f);  // its world matrix gets recomputed before drawing
}

// B7: right before GTA draws the bike: chassis unturned (reTHAWed's bike
// already leans), fork and bars turned like reTHAWed's fork, cranks pointed
// like reTHAWed's, pedals level. Put back after drawing.
void TurnParts()
{
    for (int i = 1; i < BMX_NUM_NODES; ++i)
        if (g_node[i]) g_savedNode[i] = g_node[i]->modelling;
    g_nodesSwapped = true;
    SetNodeRows(g_node[BMX_CHASSIS], V3{1, 0, 0}, V3{0, 1, 0}, V3{0, 0, 1});
    const float* m = g_forkG;
    const V3 fr{m[0], m[1], m[2]}, fu{m[3], m[4], m[5]}, fa{m[6], m[7], m[8]};
    SetNodeRows(g_node[BMX_FORKS_FRONT], fr, fu, fa);
    if (g_node[BMX_HANDLEBARS]) SetNodeRows(g_node[BMX_HANDLEBARS], fr, fu, fa);
    // chainset: its "at" (z) points from the axle to the right pedal (pedal_r sits at +z in it)
    const V3 a = g_crankG;
    const V3 r{1, 0, 0};
    const V3 u{0, a.z, -a.y}; // at x right
    SetNodeRows(g_node[BMX_CHAINSET], r, u, a);
    // pedals: the chainset's turn undone, so they stay level
    const V3 pr{1, 0, 0}, pu{0, u.y, a.y}, pa{0, u.z, a.z};
    SetNodeRows(g_node[BMX_PEDAL_R], pr, pu, pa);
    SetNodeRows(g_node[BMX_PEDAL_L], pr, pu, pa);
    ++g_partsDrawn;
}

void BeforeDrawUnsafe(CVehicle* v)
{
    if (!g_active || !g_haveVisual || v != g_bike || !v->m_matrix) return;
    if (GetTickCount64() - g_visTick > 200) return;
    const CMatrixLink& m = *v->m_matrix;
    const float now[12] = {m.right.x, m.right.y, m.right.z, m.up.x, m.up.y, m.up.z,
                           m.at.x,    m.at.y,    m.at.z,    m.pos.x, m.pos.y, m.pos.z};
    for (int i = 0; i < 12; ++i) g_saved[i] = now[i];
    float vis[12] = {g_vis.r.x, g_vis.r.y, g_vis.r.z, g_vis.f.x, g_vis.f.y, g_vis.f.z,
                     g_vis.u.x, g_vis.u.y, g_vis.u.z, g_visPos.x, g_visPos.y, g_visPos.z};

    if (g_haveParts && kTurnParts) TurnParts(); // B7, before the bike's frame update below
    SetMatrixRows(v, vis);
    // B7 fix 1 check: what GTA will draw: the fork's world matrix vs the bike's
    if (g_nodesSwapped && g_node[BMX_FORKS_FRONT]) {
        const RwMatrix* lt = RwFrameGetLTM(g_node[BMX_FORKS_FRONT]);
        if (lt) {
            const float c = lt->right.x * g_vis.r.x + lt->right.y * g_vis.r.y + lt->right.z * g_vis.r.z;
            const float l = std::sqrt(lt->right.x * lt->right.x + lt->right.y * lt->right.y + lt->right.z * lt->right.z);
            if (l > 1e-4f) {
                float cc = c / l;
                if (cc > 1.0f) cc = 1.0f;
                if (cc < -1.0f) cc = -1.0f;
                const float deg = std::acos(cc) * 57.29578f;
                if (deg > g_barsDrawnMax) g_barsDrawnMax = deg;
                ++g_barsChecked;
            }
        }
    }
    g_swapped = true;
    ++g_drawnTurned;
}

void AfterDrawUnsafe(CVehicle* v)
{
    if (!g_swapped || v != g_bike || !v->m_matrix) return;
    g_swapped = false;
    if (g_nodesSwapped) { // B7: GTA's own part positions back
        g_nodesSwapped = false;
        for (int i = 1; i < BMX_NUM_NODES; ++i)
            if (g_node[i]) {
                g_node[i]->modelling = g_savedNode[i];
                RwFrameUpdateObjects(g_node[i]);
            }
    }
    SetMatrixRows(v, g_saved);
}

void BeforeDraw(CVehicle* v)
{
    __try {
        BeforeDrawUnsafe(v);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_haveVisual = false;
    }
}

void AfterDraw(CVehicle* v)
{
    __try {
        AfterDrawUnsafe(v);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_swapped = false;
        g_nodesSwapped = false;
    }
}

} // namespace

bool BmxModeIsOn() { return g_active; }

void BmxAddHook()
{
    // B4b: right before / after GTA draws a vehicle
    plugin::Events::vehicleRenderEvent.before += [](CVehicle* v) { BeforeDraw(v); };
    plugin::Events::vehicleRenderEvent.after += [](CVehicle* v) { AfterDraw(v); };
}

void BmxFrame(LinkBlock* link, LogFn log)
{
    if (!link) return;
    // B11 (user 14:25): in skate mode, reTHAWed's rider getting on his bike
    // (its own Square prompt) puts CJ on a GTA BMX and BMX mode takes over
    {
        const LONG t = link->thawBikeState;
        if (SkateModeIsOn() && t == kBikeStateOn && g_skateThawPrev == kBikeStateOff) MountFromSkate(link, log);
        g_skateThawPrev = t;
        // B12: in skate mode, the GTA bike CJ left lies where reTHAWed's bike lies
        if (SkateModeIsOn()) DroppedBikeFrame(link, log);
        else if (g_mirroring) StopMirror(log);
    }
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
        {
            LONG raw = 0;
            if (PadLinkRawButtons(raw) && (raw & kBtnL1) && (raw & kBtnR1)) g_l1r1At = now ? now : 1;
        }
        if (thaw == kBikeStateOff) {
            if (!g_riderOffSince) g_riderOffSince = now ? now : 1;
            // B11: L1 + R1 (the get-off buttons) held lately = really getting
            // off: no waiting (the wait is only for bails)
            const bool asked = g_l1r1At && now - g_l1r1At < kL1R1RecentMs;
            if (asked || now - g_riderOffSince >= kRiderOffMs) {
                g_userOff = true;
                g_riderOffSince = 0;
                g_l1r1At = 0;
                CVehicle* bike = g_bike;
                End(link, log, asked ? "the rider got off the bike in reTHAWed (L1 + R1)"
                                     : "the rider got off the bike in reTHAWed");
                g_lastBikeRef = CPools::GetVehicleRef(bike);
                // B11: CJ off at once (GTA's get-off animation only if that fails)
                if (!CjWarpOffBike(bike, log)) CjOffBike(bike, log);
                // B10 (user 14:11): L1 + R1 goes from BMX to skate (L3 + R3 = GTA's normal BMX)
                SkateModeRequestOn("BMX -> skate (L1 + R1 in reTHAWed)", 6000);
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
            if (g_calLogPending) {
                g_calLogPending = false;
                log("bmx: the rider's hips lined up with the GTA bike (GTA bike centre at %.2f %.2f %.2f in its own space; "
                    "its forward there %.2f %.2f %.2f)", g_offLocal.x, g_offLocal.y, g_offLocal.z,
                    Local(g_board0, g_bike0.f).x, Local(g_board0, g_bike0.f).y, Local(g_board0, g_bike0.f).z);
            }
            if (g_haveBoardLog) {
                const float* b = g_lastBoard;
                log("bmx: bike (board bone) in the rider's space: right %.2f %.2f %.2f | up %.2f %.2f %.2f | at %.2f "
                    "%.2f %.2f | at %.1f %.1f %.1f | speed %.1f m/s | drawn there in %u frames",
                    b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], g_lastSpeed, g_drawnTurned);
            }
            g_drawnTurned = 0;
            g_bodyTurnMax = g_boardTurnMax = 0.0f;
            unsigned bodySkipped = 0;
            const unsigned body = BodyFramesAndReset(&bodySkipped);
            log("bmx: %u frames followed (%u blended), %u held (menu), %u skipped (sample busy), newest sample up "
                "to %ld ms old | CJ's body copied in %u drawn frames (%u half-updated samples skipped)",
                g_frames, g_blended, g_held, g_skipped, g_maxAge, body, bodySkipped);
            log("bmx: B7 bike parts turned in %u drawn frames, CJ's hands / feet put on the bike %u times; lowest "
                "'on' this second: hands %.1f %.1f, feet %.1f %.1f (bike's right, left); fork %.2f %.2f %.2f, crank "
                "%.2f %.2f %.2f | bars as drawn: up to %.0f deg turned (%u checks)", g_partsDrawn, BodyReachedAndReset(),
                g_onMin[0], g_onMin[1], g_onMin[2], g_onMin[3], g_forkG[0], g_forkG[1], g_forkG[2], g_crankG.x,
                g_crankG.y, g_crankG.z, g_barsDrawnMax, g_barsChecked);
            g_barsDrawnMax = 0.0f;
            g_barsChecked = 0;
            if (g_heldOff) log("bmx: B10: bike held still for %u frames (reTHAWed's rider is off it)", g_heldOff);
            g_heldOff = 0;
            g_partsDrawn = g_limbFrames = 0;
            for (float& m : g_onMin) m = 1.0f;
            g_frames = g_held = g_skipped = g_blended = 0;
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
    // Not while CJ is still getting on (GTA's ped state "driving"), nor while
    // reTHAWed may still be switching the rider (test 01:48, 151-160 s: BMX mode
    // started mid-switch and CJ was thrown off again).
    {
        CPlayerPed* ped = FindPlayerPed();
        if (g_quickStart) {
            // B11: reTHAWed's rider is already on, CJ was put on directly
            if (!ped || !ped->bInVehicle) return;
        } else {
            if (!ped || ped->m_ePedState != PEDSTATE_DRIVING) return;
            if (now - g_askedAt < 3000) return;
        }
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
