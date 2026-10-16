// SkateAndreas: reTHAWed's rider on / off the BMX. See bmx.h.

#include <windows.h>

#include <cmath>
#include <cstdio>

#include "bmx.h"
#include "vehscan.h" // 8.51: VehFollowOn - reTHAWed's own skitch on a following vehicle

namespace mp::thaw {
namespace {

constexpr unsigned long kSkateManagerGlobal = 0x0074FB44; // as skater.cpp
constexpr unsigned long kGetLocalSkater = 0x004A0160;     // thiscall (manager) -> skater
constexpr unsigned long kGetComponent = 0x0042E8C0;       // thiscall (object, checksum) -> component
constexpr unsigned long kRunScript = 0x004135E0;          // cdecl (script, params, object, 0, 0, 0)
constexpr unsigned long kBikeComponent = 0x5BD63B29;      // the component holding "on bike"
constexpr unsigned long kOnBikeByte = 0x524;
constexpr unsigned long kToggleBikeScript = 0x27065DA5;   // reTHAWed's own bike on/off script
constexpr DWORD kStaleMs = 5000;                          // GTA's tick older than this = GTA gone
constexpr DWORD kRetryMs = 8000; // a switch takes ~3 s (animations load); a second toggle meanwhile restarts it (test 00:25)

struct Check {
    unsigned long address;
    unsigned char bytes[12];
    unsigned int length;
    const char* what;
};
const Check kChecks[] = {
    {0x0042E8C0, {0x53, 0x8B, 0x59, 0x60, 0x85, 0xDB, 0x74, 0x3B}, 8, "GetComponent"},
    {0x005C74D0, {0x8A, 0x91, 0x24, 0x05, 0x00, 0x00, 0x33, 0xC0, 0x84, 0xD2}, 10, "IsOnBike reads +0x524"},
    {0x005F925E, {0x68, 0xD4, 0xE3, 0x98, 0x93, 0xE8, 0x78, 0xA3, 0xE1, 0xFF}, 10, "a call to RunScript 0x4135E0"},
    {0x004135E0, {0x55, 0x8B, 0x6C, 0x24, 0x08, 0x56, 0x55, 0xE8}, 8, "RunScript start"},
    {0x004A0160, {0x53, 0x56, 0x57, 0x33, 0xFF, 0x83, 0xCE, 0xFF}, 8, "GetLocalSkater start"},
    // B2: reTHAWed's Obj_SetPosition (0x0042E9E0) writes the position at +0x68,
    // then calls every component's vtable +0x24 (list at +0x11C, next at +0x10)
    {0x0042E9E9, {0x8D, 0x46, 0x68, 0x50, 0x68, 0x0A, 0x1B, 0xD3, 0xB9}, 9, "Obj_SetPosition: position at +0x68"},
    {0x0042E9F7, {0x8B, 0xB6, 0x1C, 0x01, 0x00, 0x00, 0x85, 0xF6, 0x74, 0x0E, 0x8B, 0x16}, 12,
     "Obj_SetPosition: component list at +0x11C"},
    {0x0042EA03, {0x8B, 0xCE, 0xFF, 0x52, 0x24, 0x8B, 0x76, 0x10}, 8, "Obj_SetPosition: each component's +0x24, next +0x10"},
};

using GetSkaterFn = unsigned long(__thiscall*)(void*);
using GetComponentFn = unsigned char*(__thiscall*)(void*, unsigned long);
using RunScriptFn = int(__cdecl*)(unsigned long, void*, void*, DWORD, DWORD, DWORD);

volatile LONG g_ok = 0;
DWORD g_next = 0, g_lastToggle = 0;         // game thread only
LONG g_lastWishSeen = kBikeLeave;           // game thread only: a new wish may toggle at once
volatile LONG g_state = kBikeStateUnknown;  // last seen
volatile LONG g_wish = kBikeLeave;
volatile LONG g_toggles = 0, g_errors = 0, g_changes = 0;
volatile LONG g_lastError = 0;
volatile LONG g_placed = 0, g_placeErrors = 0;
float g_placePos[3] = {}, g_placeAt[3] = {}; // last placement (for the log)
float g_posBefore[3] = {}, g_oldBefore[3] = {}; // the skater's position / previous position just before it

using TeleportedFn = void(__thiscall*)(void*);

// Like reTHAWed's Obj_SetPosition, plus the facing and no speed: put the rider
// at pos (feet, THAW units), facing 'at' (level). Game thread only.
bool PlaceRider(unsigned long skater, const float pos[3], const float at[3])
{
    __try {
        unsigned char* s = reinterpret_cast<unsigned char*>(skater);
        float* p = reinterpret_cast<float*>(s + 0x68);
        // 8.31 fix: the object's previous position (THUG engine object layout:
        // position +0x68, previous position +0x78, matrix +0x88, velocity +0xC8).
        // The skater's collision sweeps from the previous position to the new
        // one, so a far move without it stopped at the first wall on the
        // straight line between (user test 18:19: skate mode on in Vinewood put
        // the skater on a roof 150 m from the skatepark, not at CJ).
        float* old = reinterpret_cast<float*>(s + 0x78);
        float* v = reinterpret_cast<float*>(s + 0xC8);
        float* m = reinterpret_cast<float*>(s + 0x88); // rows right, up, at (16 bytes each)
        // keep the matrix's own handedness: is right = up x at, or the other way?
        const float cx = m[5] * m[10] - m[6] * m[9], cy = m[6] * m[8] - m[4] * m[10], cz = m[4] * m[9] - m[5] * m[8];
        const float sign = (cx * m[0] + cy * m[1] + cz * m[2]) < 0.0f ? -1.0f : 1.0f;
        float ax = at[0], az = at[2];
        const float len = ax * ax + az * az;
        if (len < 1e-6f) return false;
        const float inv = 1.0f / sqrtf(len);
        ax *= inv;
        az *= inv;
        // up = (0, 1, 0), at = (ax, 0, az), right = sign * (up x at) = sign * (az, 0, -ax)
        m[0] = sign * az; m[1] = 0.0f; m[2] = -sign * ax;
        m[4] = 0.0f;      m[5] = 1.0f; m[6] = 0.0f;
        m[8] = ax;        m[9] = 0.0f; m[10] = az;
        for (int i = 0; i < 3; ++i) { g_posBefore[i] = p[i]; g_oldBefore[i] = old[i]; }
        p[0] = pos[0]; p[1] = pos[1]; p[2] = pos[2];
        // only if +0x78 looks like a position near the current one (guard
        // against a wrong guess about the layout)
        const float ox = g_posBefore[0] - g_oldBefore[0], oy = g_posBefore[1] - g_oldBefore[1],
                    oz = g_posBefore[2] - g_oldBefore[2];
        if (ox * ox + oy * oy + oz * oz < 2000.0f * 2000.0f) {
            old[0] = pos[0]; old[1] = pos[1]; old[2] = pos[2];
        }
        v[0] = v[1] = v[2] = 0.0f;
        for (unsigned char* c = *reinterpret_cast<unsigned char**>(s + 0x11C); c;
             c = *reinterpret_cast<unsigned char**>(c + 0x10)) {
            void** vt = *reinterpret_cast<void***>(c);
            reinterpret_cast<TeleportedFn>(vt[0x24 / 4])(c);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_lastError, static_cast<LONG>(GetExceptionCode()));
        return false;
    }
}

// The rider's "on bike" byte: 1 / 0, or -1 if it can't be read.
int ReadOnBike(unsigned long& skaterOut)
{
    __try {
        void* manager = *reinterpret_cast<void**>(kSkateManagerGlobal);
        if (!manager) return -1;
        const unsigned long skater = reinterpret_cast<GetSkaterFn>(kGetLocalSkater)(manager);
        if (!skater) return -1;
        unsigned char* comp =
            reinterpret_cast<GetComponentFn>(kGetComponent)(reinterpret_cast<void*>(skater), kBikeComponent);
        if (!comp) return -1;
        skaterOut = skater;
        return comp[kOnBikeByte] ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_lastError, static_cast<LONG>(GetExceptionCode()));
        return -1;
    }
}

bool RunToggle(unsigned long skater)
{
    __try {
        reinterpret_cast<RunScriptFn>(kRunScript)(kToggleBikeScript, nullptr, reinterpret_cast<void*>(skater), 0, 0, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_lastError, static_cast<LONG>(GetExceptionCode()));
        return false;
    }
}

} // namespace

bool BmxCodeChecksPass(void (*log)(const char*, ...))
{
    for (const Check& c : kChecks) {
        const unsigned char* code = reinterpret_cast<const unsigned char*>(c.address);
        for (unsigned int i = 0; i < c.length; ++i)
            if (code[i] != c.bytes[i]) {
                log("bmx: code check 0x%08lX (%s) DIFFERENT at byte %u; BMX mode off", c.address, c.what, i);
                return false;
            }
    }
    const unsigned long* entry = reinterpret_cast<const unsigned long*>(0x007326E0);
    if (entry[0] != 0xE91EAA16 || entry[1] != kBikeComponent) {
        log("bmx: 'is on bike' table entry different (0x%08lX 0x%08lX); BMX mode off", entry[0], entry[1]);
        return false;
    }
    InterlockedExchange(&g_ok, 1);
    log("bmx: code checks passed; reTHAWed's rider follows CJ on / off a BMX");
    return true;
}

// 8.33 C3: the car tow. While the skater is on a car's roof rail (published by the
// GTA plugin), set his velocity to that car's, so the game carries him along with
// the car and the rail stays (almost) still relative to him: grinding a moving car,
// THAW-skitch style. Setting the velocity (not the position) is smooth and cannot
// teleport him, so it cannot blow the physics up the way a moving rail alone did.
LONG g_towFrames = 0;
bool g_towValid = false;
int g_towCar = -1, g_towRail = -1;
float g_towMid[3] = {};
ULONGLONG g_towTick = 0;

void CarTowGameThread(LinkBlock* link, unsigned long skater)
{
    if (!link || !skater || link->gtaSkateMode != 1) {
        g_towValid = false;
        return;
    }
    float pts[kCarRailCars * kCarRailPts * 3];
    const int cars = ReadCarRails(link, pts, static_cast<int>(sizeof(pts) / sizeof(pts[0])));
    if (cars <= 0) {
        g_towValid = false;
        return;
    }
    float sp[3];
    __try {
        const float* p = reinterpret_cast<const float*>(skater + 0x68);
        sp[0] = p[0];
        sp[1] = p[1];
        sp[2] = p[2];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_towValid = false;
        return;
    }
    // the car roof rail the skater is on (nearest segment, close sideways, part way along)
    int bc = -1, br = -1;
    float bestSide = 1e9f, bestMid[3] = {};
    for (int c = 0; c < cars; ++c) {
        for (int r = 0; r < 2; ++r) {
            const float* a = pts + (c * kCarRailPts + r * 2) * 3;
            const float* b = a + 3;
            float d[3], s[3];
            for (int k = 0; k < 3; ++k) { d[k] = b[k] - a[k]; s[k] = sp[k] - a[k]; }
            const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            if (dd < 1e-6f) continue;
            const float t = (s[0] * d[0] + s[1] * d[1] + s[2] * d[2]) / dd;
            if (t <= 0.0f || t >= 1.0f) continue;
            float off[3];
            for (int k = 0; k < 3; ++k) off[k] = sp[k] - (a[k] + d[k] * t);
            const float side = sqrtf(off[0] * off[0] + off[2] * off[2]);
            if (side < 20.0f && fabsf(off[1]) < 30.0f && side < bestSide) {
                bestSide = side;
                bc = c;
                br = r;
                for (int k = 0; k < 3; ++k) bestMid[k] = a[k] + d[k] * 0.5f;
            }
        }
    }
    const ULONGLONG now = GetTickCount64();
    if (bc < 0) {
        g_towValid = false;
        return;
    }
    if (g_towValid && g_towCar == bc && g_towRail == br && now > g_towTick) {
        const float dt = static_cast<float>(now - g_towTick) / 1000.0f;
        float delta[3];
        for (int k = 0; k < 3; ++k) delta[k] = bestMid[k] - g_towMid[k];
        const float dl = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
        if (dt > 0.005f && dl > 0.5f && dl < 400.0f) { // the car actually moved: tow
            __try {
                float* v = reinterpret_cast<float*>(skater + 0xC8);
                v[0] = delta[0] / dt;
                v[1] = delta[1] / dt;
                v[2] = delta[2] / dt;
                ++g_towFrames;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    g_towCar = bc;
    g_towRail = br;
    for (int k = 0; k < 3; ++k) g_towMid[k] = bestMid[k];
    g_towTick = now;
    g_towValid = true;
}

// 8.42 skitch, the way reTHAWed does it (DESIGN 8.42). Press Up, a new press, directly behind a
// moving car to grab on. The car pulls the skater along: his velocity is set to the car's, with
// a spring to a point just behind it, and his position is never touched. Press Down, a new press,
// to let go. He holds the car by its GTA pool index, so the tow cannot switch to another car.
// Balance (Left / Right) is not modelled yet: that is step 2.
constexpr LONG kSkitchGrabButton = kBtnUp;      // reTHAWed: Up on the D-pad (L1 before 8.42)
constexpr LONG kSkitchLetGoButton = kBtnDown;   // reTHAWed: Down lets go
constexpr float kSkitchBackMin = 39.4f;   // 1 m behind the car, in THAW units (inches)
constexpr float kSkitchBackMax = 315.0f;  // 8 m
constexpr float kSkitchCone = 0.6f;       // sideways at most 0.6 x the distance back: about 31 degrees
constexpr float kSkitchLevel = 59.0f;     // 1.5 m: not on another level
constexpr float kSkitchFar = 472.0f;      // 12 m: let go
constexpr float kSkitchStopSpeed = 39.4f; // 1 m/s: slower than this counts as stopped
constexpr DWORD kSkitchGraceMs = 500;     // a stop, or a car that is missing, is allowed this long
constexpr int kSkitchMsgSlots = 8;
char g_skitchMsgs[kSkitchMsgSlots][160] = {}; // lines for BmxSecond to write (the game thread never logs)
volatile LONG g_skitchMsgCount = 0;
char g_skitchWhy[120] = "";  // why the last Up press did not grab
LONG g_skitchRefused = 0;    // Up presses without a grab this second
LONG g_skitchFrames = 0;     // towed frames this second
float g_skitchDist = -1.0f;  // last distance to the car (THAW units)
bool g_skitchHeld = false;   // he is holding a car
LONG g_skitchId = -1;        // its GTA pool index
DWORD g_skitchSince = 0;     // when he grabbed it (GetTickCount)
DWORD g_skitchGone = 0;      // when the held car first stopped or went missing (0 = fine)
bool g_skitchUpPrev = false;
bool g_skitchDownPrev = false;

// The game thread must not touch the log file: it queues a line, and BmxSecond writes it.
void SkitchNote(const char* text)
{
    const LONG n = InterlockedIncrement(&g_skitchMsgCount) - 1;
    snprintf(g_skitchMsgs[n % kSkitchMsgSlots], sizeof g_skitchMsgs[0], "%s", text);
}

void SkitchRefuse(const char* why)
{
    InterlockedIncrement(&g_skitchRefused);
    snprintf(g_skitchWhy, sizeof g_skitchWhy, "%s", why);
}

void SkitchLetGo(LinkBlock* link, const char* why, DWORD now)
{
    char msg[160];
    snprintf(msg, sizeof msg, "skitch 8.42: let go (%s) after %.1f s", why, (now - g_skitchSince) / 1000.0);
    SkitchNote(msg);
    g_skitchHeld = false;
    g_skitchId = -1;
    g_skitchGone = 0;
    link->thawSkitchId = -1;
    link->thawSkitching = 0;
}

// The grab, the hold and the let-go. Runs under the SEH guard in CarSkitchGameThread.
void SkitchStep(LinkBlock* link, unsigned long skater, bool upEdge, bool downEdge, DWORD now)
{
    const float* p = reinterpret_cast<const float*>(skater + 0x68);
    float cp[3] = {}, cf[3] = {}, cv[3] = {};
    bool carThere = link->gtaSkitchValid == 1;
    for (int k = 0; k < 3; ++k) {
        cp[k] = link->gtaSkitchPos[k];
        cf[k] = link->gtaSkitchFwd[k];
        cv[k] = link->gtaSkitchVel[k];
    }
    // 8.44 N1: a car with a bad number counts as no car (a held one is let go after the grace time)
    if (carThere && !(AllFinite(cp, 3) && AllFinite(cf, 3) && AllFinite(cv, 3))) {
        InterlockedIncrement(&g_linkBadNumbers);
        carThere = false;
    }
    if (!g_skitchHeld) {
        if (!upEdge) return;
        if (!carThere || link->gtaSkitchId < 0) {
            SkitchRefuse("no moving car in reach");
            return;
        }
        const float rx = p[0] - cp[0], ry = p[1] - cp[1], rz = p[2] - cp[2];
        const float dist = sqrtf(rx * rx + ry * ry + rz * rz);
        g_skitchDist = dist;
        if (dist > 1.0e7f) {
            SkitchRefuse("bad position");
            return;
        }
        const float fl = sqrtf(cf[0] * cf[0] + cf[2] * cf[2]);
        if (fl < 0.5f) {
            SkitchRefuse("the car's nose is not known");
            return;
        }
        const float fx = cf[0] / fl, fz = cf[2] / fl;
        const float back = -(rx * fx + rz * fz); // more than 0 = behind the car
        const float side = fabsf(rx * fz - rz * fx);
        const float level = fabsf(ry);
        const float degrees = atan2f(side, back) * 57.29578f;
        char why[120];
        if (back < kSkitchBackMin) {
            if (back <= 0.0f) snprintf(why, sizeof why, "not behind it (beside it or in front)");
            else snprintf(why, sizeof why, "not far enough behind it (%.1f m)", back * 0.0254f);
            SkitchRefuse(why);
            return;
        }
        if (back > kSkitchBackMax) {
            snprintf(why, sizeof why, "too far behind it (%.1f m)", back * 0.0254f);
            SkitchRefuse(why);
            return;
        }
        if (side > kSkitchCone * back) {
            snprintf(why, sizeof why, "off to one side of it (%.0f degrees off its nose)", degrees);
            SkitchRefuse(why);
            return;
        }
        if (level > kSkitchLevel) {
            snprintf(why, sizeof why, "on another level (%.1f m)", level * 0.0254f);
            SkitchRefuse(why);
            return;
        }
        g_skitchHeld = true;
        g_skitchId = link->gtaSkitchId;
        g_skitchSince = now;
        g_skitchGone = 0;
        link->thawSkitchId = g_skitchId;
        char msg[160];
        snprintf(msg, sizeof msg, "skitch 8.42: grabbed car %ld from behind (%.1f m back, %.0f degrees off its nose)",
                 g_skitchId, back * 0.0254f, degrees);
        SkitchNote(msg);
    }

    // Holding.
    if (downEdge) {
        SkitchLetGo(link, "Down pressed", now);
        return;
    }
    const bool here = carThere && link->gtaSkitchId == g_skitchId;
    const float speed = sqrtf(cv[0] * cv[0] + cv[1] * cv[1] + cv[2] * cv[2]);
    if (here && speed >= kSkitchStopSpeed) g_skitchGone = 0;
    else if (g_skitchGone == 0) g_skitchGone = now;
    if (g_skitchGone != 0 && now - g_skitchGone > kSkitchGraceMs) {
        SkitchLetGo(link, here ? "car stopped" : "car out of reach", now);
        return;
    }
    if (!here) return; // this pass holds another car's data, or none: do not tow it
    const float rx = p[0] - cp[0], ry = p[1] - cp[1], rz = p[2] - cp[2];
    const float dist = sqrtf(rx * rx + ry * ry + rz * rz);
    g_skitchDist = dist;
    if (dist > kSkitchFar) {
        SkitchLetGo(link, "too far from the car", now);
        return;
    }
    // Tow: his velocity is the car's, plus a spring towards a point 3.2 m behind the car's
    // centre along its nose, so he does not drift into the car or wander off.
    const float kBack = 126.0f;    // 3.2 m, in inches
    const float kGain = 2.0f;      // spring, 1/s
    const float kMaxCorr = 500.0f; // cap the correction (about 12 m/s)
    float* v = reinterpret_cast<float*>(skater + 0xC8);
    float corr[3] = {0.0f, 0.0f, 0.0f};
    const float fl = sqrtf(cf[0] * cf[0] + cf[2] * cf[2]);
    if (fl >= 0.5f) {
        const float fx = cf[0] / fl, fz = cf[2] / fl;
        corr[0] = (cp[0] - fx * kBack - p[0]) * kGain;
        corr[2] = (cp[2] - fz * kBack - p[2]) * kGain;
        const float cm = sqrtf(corr[0] * corr[0] + corr[2] * corr[2]);
        if (cm > kMaxCorr) {
            const float s = kMaxCorr / cm;
            corr[0] *= s;
            corr[2] *= s;
        }
    }
    for (int k = 0; k < 3; ++k) v[k] = cv[k] + corr[k];
    link->thawSkitching = 1; // tell GTA: leave his spot alone while this lasts
    ++g_skitchFrames;
}

void CarSkitchGameThread(LinkBlock* link, unsigned long skater)
{
    if (!link) return;
    link->thawSkitching = 0; // set again by SkitchStep while a tow is happening
    InputShared in;
    const bool read = ReadInputShared(link, in);
    // Grab: the D-pad Up, or the left stick pushed up (GTA's stick is negative up, as in spawner.cpp).
    const bool up = read && ((in.buttons & kSkitchGrabButton) != 0 || in.axes[kAxisLY] < -80);
    const bool down = read && (in.buttons & kSkitchLetGoButton) != 0;
    const bool upEdge = up && !g_skitchUpPrev;
    const bool downEdge = down && !g_skitchDownPrev;
    g_skitchUpPrev = up;
    g_skitchDownPrev = down;
    const DWORD now = GetTickCount();
    if (!skater || link->gtaSkateMode != 1) {
        if (g_skitchHeld) SkitchLetGo(link, "skate mode off", now);
        link->thawSkitchId = -1;
        return;
    }
    // 8.51: while a reTHAWed vehicle follows the GTA car, Up is reTHAWed's own skitch: ours steps aside
    // (and leaves thawSkitchId to vehscan.cpp, which holds the GTA car it follows)
    if (VehFollowOn()) {
        if (g_skitchHeld) SkitchLetGo(link, "reTHAWed vehicle follow mode on", now);
        return;
    }
    __try {
        SkitchStep(link, skater, upEdge, downEdge, now);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void BmxGameThreadTick(LinkBlock* link)
{
    if (!g_ok || !link) return;

    unsigned long skater = 0;
    const int onBike = ReadOnBike(skater);
    // 8.33 skitch: every call, because it sets a velocity and wants every frame
    CarSkitchGameThread(link, skater);

    const DWORD now = GetTickCount();
    if (g_next && static_cast<LONG>(now - g_next) < 0) return;
    g_next = now + 100;

    const LONG state = onBike < 0 ? kBikeStateUnknown : (onBike ? kBikeStateOn : kBikeStateOff);
    if (state != g_state) InterlockedIncrement(&g_changes);
    InterlockedExchange(&g_state, state);
    InterlockedExchange(&link->thawBikeState, state);

    // B2: GTA asked to put the rider somewhere (once per request)
    const LONG placeSeq = link->gtaPlaceSeq;
    if (placeSeq != link->thawPlaceDone && skater) {
        MemoryBarrier();
        float pos[3], at[3];
        for (int i = 0; i < 3; ++i) {
            pos[i] = link->gtaPlacePos[i];
            at[i] = link->gtaPlaceAt[i];
        }
        if (!AllFinite(pos, 3) || !AllFinite(at, 3)) { // 8.44 N1: never place him at a bad number
            InterlockedIncrement(&g_linkBadNumbers);
            InterlockedIncrement(&g_placeErrors);
        } else if (PlaceRider(skater, pos, at)) {
            InterlockedIncrement(&g_placed);
            for (int i = 0; i < 3; ++i) { g_placePos[i] = pos[i]; g_placeAt[i] = at[i]; }
        } else {
            InterlockedIncrement(&g_placeErrors);
        }
        InterlockedExchange(&link->thawPlaceDone, placeSeq); // done (or given up), either way once
    }

    // 8.33 C3: carry the skater with a moving car he is grinding
    CarTowGameThread(link, skater);

    LONG wish = link->gtaBikeWish;
    if (now - static_cast<DWORD>(link->reserved[2]) > kStaleMs) wish = kBikeLeave; // GTA gone
    InterlockedExchange(&g_wish, wish);
    if (wish == kBikeLeave || state == kBikeStateUnknown) return;
    const LONG flags = link->thawFlags;
    if (!(flags & kInLevel) || (flags & kPaused)) return;
    const bool newWish = wish != g_lastWishSeen;
    g_lastWishSeen = wish;
    const bool want = wish == kBikeOn;
    if (want == (state == kBikeStateOn)) return;
    // one toggle per wish; again only if reTHAWed still hasn't switched after kRetryMs
    if (!newWish && g_lastToggle && now - g_lastToggle < kRetryMs) return;
    g_lastToggle = now;
    if (RunToggle(skater))
        InterlockedIncrement(&g_toggles);
    else
        InterlockedIncrement(&g_errors);
}

void BmxSecond(void (*log)(const char*, ...))
{
    if (!g_ok) return;
    const LONG t = InterlockedExchange(&g_toggles, 0), e = InterlockedExchange(&g_errors, 0),
               c = InterlockedExchange(&g_changes, 0);
    static const char* const kState[] = {"unknown", "off the bike", "ON the bike"};
    static const char* const kWish[] = {"leave it", "on the bike", "off the bike"};
    const LONG s = g_state, w = g_wish;
    if (t || e || c)
        log("bmx: rider %s (GTA wants: %s) | toggles run %ld, errors %ld (last code 0x%08lX)",
            (s >= 0 && s <= 2) ? kState[s] : "?", (w >= 0 && w <= 2) ? kWish[w] : "?", t, e,
            static_cast<unsigned long>(g_lastError));
    const LONG pl = InterlockedExchange(&g_placed, 0), pe = InterlockedExchange(&g_placeErrors, 0);
    if (pl || pe)
    {
        log("bmx: rider placed %ld time(s) for GTA (%ld failed): at %.0f %.0f %.0f, facing %.2f %.2f %.2f", pl, pe,
            g_placePos[0], g_placePos[1], g_placePos[2], g_placeAt[0], g_placeAt[1], g_placeAt[2]);
        // check of the +0x78 = previous position guess: just before, it should
        // be within one frame's travel of the position
        const float dx = g_posBefore[0] - g_oldBefore[0], dy = g_posBefore[1] - g_oldBefore[1],
                    dz = g_posBefore[2] - g_oldBefore[2];
        log("bmx: just before: position %.1f %.1f %.1f, +0x78 %.1f %.1f %.1f (%.1f apart)", g_posBefore[0],
            g_posBefore[1], g_posBefore[2], g_oldBefore[0], g_oldBefore[1], g_oldBefore[2],
            sqrtf(dx * dx + dy * dy + dz * dz));
    }
    const LONG tf = InterlockedExchange(&g_towFrames, 0);
    if (tf)
        log("cars: towing in %ld frames this second (skater velocity matched to the car)", tf);
    // 8.42 skitch: the grabs and let-gos queued by the game thread, and refused Up presses
    static LONG notesPrinted = 0;
    const LONG notesNow = g_skitchMsgCount;
    if (notesNow - notesPrinted > kSkitchMsgSlots) notesPrinted = notesNow - kSkitchMsgSlots;
    for (LONG m = notesPrinted; m < notesNow; ++m) log("%s", g_skitchMsgs[m % kSkitchMsgSlots]);
    notesPrinted = notesNow;
    const LONG sf = InterlockedExchange(&g_skitchFrames, 0);
    const LONG sref = InterlockedExchange(&g_skitchRefused, 0);
    if (sf)
        log("skitch 8.42: towing %ld frames this second (%.1f m from the car)", sf, g_skitchDist * 0.0254f);
    if (sref)
        log("skitch 8.42: Up pressed %ld time(s) without a grab; last reason: %s", sref, g_skitchWhy);
}

} // namespace mp::thaw
