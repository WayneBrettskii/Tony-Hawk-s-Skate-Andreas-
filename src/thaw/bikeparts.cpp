// my-passthrough: reTHAWed's bike for GTA. READ ONLY. See bikeparts.h.

#include <windows.h>

#include <cmath>

#include "bikeparts.h"
#include "bikefind.h"
#include "pose.h"

namespace mp::thaw {
namespace {

constexpr DWORD kRestAfterMs = 4000; // riding pose taken this long after getting on (the switch takes ~3 s)
// hand on the grip / foot on the pedal: fully within the first distance (THAW
// units) of how it was in the riding pose, not at all beyond the second
constexpr float kHandNear = 3.0f, kHandFar = 10.0f;
// B9 fix 1 (test 13:57: the riding pose was taken mid-trick, feet 11-18 units
// from the pedals): a foot is on its pedal while it is near the crank axle
// (THAW units; riding: ~14-18 seen at 13:15), no learned pose
constexpr float kFootNear = 22.0f, kFootFar = 32.0f;
constexpr float kStraightFork = 10.0f; // riding pose only while the fork is this close to straight (degrees)

// rider bones used here
enum Rider { kBoardRoot, kWristL, kWristR, kAnkleL, kToeL, kAnkleR, kToeR, kRiderCount };
const char* const kRiderNames[kRiderCount] = {"Bone_Board_Root", "Bone_Wrist_L", "Bone_Wrist_R", "Bone_Ankle_L",
                                              "Bone_Toe_L",      "Bone_Ankle_R", "Bone_Toe_R"};
// bike bones used here
enum Bike { kFrame, kFront, kCrank, kPedalR, kPedalL, kBikeCount };
const unsigned long kBikeSums[kBikeCount] = {kBikeBoneFrame, kBikeBoneFront, kBikeBoneCrank, kBikeBonePedalR,
                                             kBikeBonePedalL};

struct M4 {
    float m[16];
};

bool g_onBike = false, g_haveRest = false, g_found = false;
DWORD g_onSince = 0;
M4 g_offset;                    // frame -> board (riding pose): board = offset x frame
float g_frontRest[9] = {};      // the fork's turn in the bike's (board's) space, riding pose
float g_gripLocal[2][3] = {};   // the hand on each side (0 = bike's right, +x), in the fork's space
int g_handBone[2] = {}, g_footBone[2] = {}, g_pedalBone[2] = {}; // which rider hand / foot, which pedal bone per side
float g_footMin[2] = {1e9f, 1e9f}, g_footMax[2] = {0, 0}; // log: foot to crank axle this second
bool g_haveBindFork = false;
float g_bindFork[9] = {};       // fix 1: the fork's turn relative to the frame in the bike's default pose (straight)
DWORD g_waitLogged = 0;
float g_out[kLinkBikeFloats] = {};
// log, this second
float g_forkMax = 0.0f, g_handOnMin[2] = {1, 1}, g_footOnMin[2] = {1, 1};
unsigned g_samples = 0;
float g_crankTurn = 0.0f, g_lastCrankAng = 0.0f;
bool g_haveCrankAng = false;

void Mul4(const float* a, const float* b, float* o) // o = a x b (row vectors)
{
    float t[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            t[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
}

// a bone matrix with its rotation rows made unit length (bones may carry a little scale)
M4 Clean(const float* m)
{
    M4 o;
    for (int i = 0; i < 16; ++i) o.m[i] = m[i];
    for (int r = 0; r < 3; ++r) {
        float* row = o.m + r * 4;
        const float l = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
        if (l > 1e-6f) { row[0] /= l; row[1] /= l; row[2] /= l; }
        row[3] = 0.0f;
    }
    o.m[15] = 1.0f;
    return o;
}

M4 InverseRigid(const M4& a)
{
    M4 o;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) o.m[r * 4 + c] = a.m[c * 4 + r];
        o.m[r * 4 + 3] = 0.0f;
    }
    for (int c = 0; c < 3; ++c)
        o.m[12 + c] = -(a.m[12] * o.m[c] + a.m[13] * o.m[4 + c] + a.m[14] * o.m[8 + c]);
    o.m[15] = 1.0f;
    return o;
}

// rel = B x R^T (3x3): b's rotation in r's space (row vectors)
void Relative(const float* b, const float* r, float* rel)
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            rel[i * 3 + j] = b[i * 4 + 0] * r[j * 4 + 0] + b[i * 4 + 1] * r[j * 4 + 1] + b[i * 4 + 2] * r[j * 4 + 2];
}

// a point (rider's space) in a matrix's space: (p - pos) . rows
void ToSpace(const float* p, const float* m, float* out)
{
    const float d[3] = {p[0] - m[12], p[1] - m[13], p[2] - m[14]};
    for (int j = 0; j < 3; ++j) out[j] = d[0] * m[j * 4] + d[1] * m[j * 4 + 1] + d[2] * m[j * 4 + 2];
}

// M = rest^T x now (row vectors: now = rest x M), 3x3
void TurnSince(const float* rest, const float* now, float* m)
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[i * 3 + j] = rest[i] * now[j] + rest[3 + i] * now[3 + j] + rest[6 + i] * now[6 + j];
}

float Angle3(const float* m) // of a 3x3 turn, degrees
{
    float c = (m[0] + m[4] + m[8] - 1.0f) * 0.5f;
    if (c > 1.0f) c = 1.0f;
    if (c < -1.0f) c = -1.0f;
    return std::acos(c) * 57.29578f;
}

float Dist(const float* a, const float* b)
{
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

float OnAmount(float d, float nearDist, float farDist)
{
    if (d <= nearDist) return 1.0f;
    if (d >= farDist) return 0.0f;
    return (farDist - d) / (farDist - nearDist);
}

void Reset()
{
    g_haveRest = false;
    g_found = false;
    g_haveBindFork = false;
    g_waitLogged = 0;
    g_haveCrankAng = false;
    ForgetBikeSkeleton();
}

} // namespace

bool BikePartsTick(unsigned long skater, unsigned long riderSkeleton, bool onBike, void (*log)(const char*, ...),
                   float* out, float* board12, bool* boardValid)
{
    *boardValid = false;
    const DWORD now = GetTickCount();
    if (!onBike || !skater) {
        if (g_onBike) Reset();
        g_onBike = false;
        return false;
    }
    if (!g_onBike) {
        g_onBike = true;
        g_onSince = now;
        Reset();
    }
    // the rider's bones
    static BoneMatrix rb[64];
    const int rn = ReadAllBones(skater, rb, 64);
    if (rn <= 0) return false;
    static unsigned long rsum[kRiderCount] = {};
    if (!rsum[0])
        for (int k = 0; k < kRiderCount; ++k) rsum[k] = NameChecksum(kRiderNames[k]);
    int ri[kRiderCount];
    for (int k = 0; k < kRiderCount; ++k) {
        ri[k] = -1;
        for (int i = 0; i < rn; ++i)
            if (rb[i].name == rsum[k]) { ri[k] = i; break; }
        if (ri[k] < 0) return false;
    }
    // the bike's bones (B8: its own skeleton)
    const unsigned long bikeSkel = FindBikeSkeleton(riderSkeleton, skater, log);
    if (!bikeSkel) return false;
    static BoneMatrix bb[32];
    const int bn = ReadSkeletonBones(bikeSkel, bb, 32);
    if (bn <= 0) return false;
    int bi[kBikeCount];
    for (int k = 0; k < kBikeCount; ++k) {
        bi[k] = -1;
        for (int i = 0; i < bn; ++i)
            if (bb[i].name == kBikeSums[k]) { bi[k] = i; break; }
        if (bi[k] < 0) {
            if (!g_found) log("bike parts: bike skeleton at 0x%08lX lacks bone %d of ours; off", bikeSkel, k);
            g_found = true;
            return false;
        }
    }
    if (!g_found) {
        g_found = true;
        log("bike parts: bike skeleton 0x%08lX, %d bones (frame #%d, front #%d, crank #%d, pedals #%d #%d); "
            "riding pose in %lu s", bikeSkel, bn, bi[kFrame], bi[kFront], bi[kCrank], bi[kPedalR], bi[kPedalL],
            kRestAfterMs / 1000);
    }
    const M4 frame = Clean(bb[bi[kFrame]].m);
    if (!g_haveBindFork) {
        // fix 1: "straight" from the bike's default pose, not from whenever the riding pose is taken
        static BoneMatrix bind[32];
        if (ReadSkeletonBind(bikeSkel, bind, 32) == bn) {
            const M4 bf = Clean(bind[bi[kFrame]].m), bfr = Clean(bind[bi[kFront]].m);
            Relative(bfr.m, bf.m, g_bindFork); // in the frame's space; the offset (board vs frame) is applied below
            g_haveBindFork = true;
        }
    }
    if (!g_haveBindFork) return false;

    // hands and feet (rider's space)
    const float* wrist[2] = {rb[ri[kWristL]].m + 12, rb[ri[kWristR]].m + 12};
    float foot[2][3];
    for (int j = 0; j < 3; ++j) {
        foot[0][j] = (rb[ri[kAnkleL]].m[12 + j] + rb[ri[kToeL]].m[12 + j]) * 0.5f; // about the ball of the foot
        foot[1][j] = (rb[ri[kAnkleR]].m[12 + j] + rb[ri[kToeR]].m[12 + j]) * 0.5f;
    }

    if (!g_haveRest) {
        if (now - g_onSince < kRestAfterMs) return false;
        // fix 1: only while the fork is straight (not mid bar spin / tailwhip)
        {
            const M4 fr = Clean(bb[bi[kFront]].m);
            float relF[9], m[9];
            Relative(fr.m, frame.m, relF);
            TurnSince(g_bindFork, relF, m);
            if (Angle3(m) > kStraightFork) {
                if (!g_waitLogged || now - g_waitLogged > 5000) {
                    g_waitLogged = now;
                    log("bike parts: waiting for the bars to be straight to take the riding pose (now %.0f deg)", Angle3(m));
                }
                return false;
            }
        }
        // line the bike frame up with the rider's board bone (GTA's picture code
        // is built on the board bone's convention): board = offset x frame
        const M4 board = Clean(rb[ri[kBoardRoot]].m);
        Mul4(board.m, InverseRigid(frame).m, g_offset.m);
        float b[16];
        Mul4(g_offset.m, frame.m, b);
        const M4 front = Clean(bb[bi[kFront]].m);
        // the straight fork in the board's space: Relative(front_bind, offset x frame_bind)
        // = front_bind x frame_bind^T x offset^T = g_bindFork x offset^T (rotation rows)
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                g_frontRest[r * 3 + c] = g_bindFork[r * 3] * g_offset.m[c * 4] + g_bindFork[r * 3 + 1] * g_offset.m[c * 4 + 1] +
                                         g_bindFork[r * 3 + 2] * g_offset.m[c * 4 + 2];
        // sides (0 = bike's right, +x in the board's space)
        float h0[3], h1[3], f0[3], f1[3], pr[3], pl[3];
        ToSpace(wrist[0], b, h0);
        ToSpace(wrist[1], b, h1);
        ToSpace(foot[0], b, f0);
        ToSpace(foot[1], b, f1);
        ToSpace(bb[bi[kPedalR]].m + 12, b, pr);
        ToSpace(bb[bi[kPedalL]].m + 12, b, pl);
        g_handBone[0] = h1[0] >= h0[0] ? 1 : 0;
        g_handBone[1] = 1 - g_handBone[0];
        g_footBone[0] = f1[0] >= f0[0] ? 1 : 0;
        g_footBone[1] = 1 - g_footBone[0];
        g_pedalBone[0] = pr[0] >= pl[0] ? kPedalR : kPedalL;
        g_pedalBone[1] = g_pedalBone[0] == kPedalR ? kPedalL : kPedalR;
        for (int s = 0; s < 2; ++s) {
            // hand in the fork's space
            float hp[3];
            ToSpace(wrist[g_handBone[s]], front.m, hp);
            for (int j = 0; j < 3; ++j) g_gripLocal[s][j] = hp[j];
        }
        g_haveRest = true;
        const float* rp = g_pedalBone[0] == kPedalR ? pr : pl;
        const float* lp = g_pedalBone[0] == kPedalR ? pl : pr;
        log("bike parts: riding pose taken. Board vs bike frame: offset rows %.2f %.2f %.2f | %.2f %.2f %.2f | %.2f "
            "%.2f %.2f | pos %.1f %.1f %.1f. Pedals (board space) right %.1f %.1f %.1f, left %.1f %.1f %.1f; feet "
            "%.1f / %.1f units from the crank axle; the rider's %s hand and %s foot on the bike's right",
            g_offset.m[0], g_offset.m[1], g_offset.m[2], g_offset.m[4], g_offset.m[5], g_offset.m[6], g_offset.m[8],
            g_offset.m[9], g_offset.m[10], g_offset.m[12], g_offset.m[13], g_offset.m[14], rp[0], rp[1], rp[2],
            lp[0], lp[1], lp[2], Dist(foot[g_footBone[0]], bb[bi[kCrank]].m + 12),
            Dist(foot[g_footBone[1]], bb[bi[kCrank]].m + 12), g_handBone[0] ? "right" : "left",
            g_footBone[0] ? "right" : "left");
        return false;
    }

    // ---- every pass ----
    float b[16];
    Mul4(g_offset.m, frame.m, b); // the bike frame in the board bone's convention
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c) board12[r * 3 + c] = b[r * 4 + c];
    *boardValid = true;

    float* o = g_out;
    // fork / bars
    const M4 front = Clean(bb[bi[kFront]].m);
    float relFront[9];
    Relative(front.m, b, relFront);
    TurnSince(g_frontRest, relFront, o + kBikeFork);
    const float forkAng = Angle3(o + kBikeFork);
    if (forkAng > g_forkMax) g_forkMax = forkAng;
    // crank: axle -> pedal on the bike's right, square to the bike's x
    float axle[3], pedal[3];
    ToSpace(bb[bi[kCrank]].m + 12, b, axle);
    ToSpace(bb[bi[g_pedalBone[0]]].m + 12, b, pedal);
    const float arm[3] = {0.0f, pedal[1] - axle[1], pedal[2] - axle[2]};
    const float l = std::sqrt(arm[1] * arm[1] + arm[2] * arm[2]);
    if (l > 1e-3f) {
        o[kBikeCrank] = 0.0f;
        o[kBikeCrank + 1] = arm[1] / l;
        o[kBikeCrank + 2] = arm[2] / l;
        const float ang = std::atan2(arm[2], arm[1]);
        if (g_haveCrankAng) {
            float d = ang - g_lastCrankAng;
            while (d > 3.14159265f) d -= 6.2831853f;
            while (d < -3.14159265f) d += 6.2831853f;
            g_crankTurn += std::fabs(d) * 57.29578f;
        }
        g_lastCrankAng = ang;
        g_haveCrankAng = true;
    }
    // hands on the grips (each side's grip moves with the fork), feet on the pedals
    for (int s = 0; s < 2; ++s) {
        const float* gl = g_gripLocal[s];
        float grip[3];
        for (int j = 0; j < 3; ++j)
            grip[j] = front.m[12 + j] + gl[0] * front.m[j] + gl[1] * front.m[4 + j] + gl[2] * front.m[8 + j];
        o[kBikeHandOn + s] = OnAmount(Dist(wrist[g_handBone[s]], grip), kHandNear, kHandFar);
        const float d = Dist(foot[g_footBone[s]], bb[bi[kCrank]].m + 12);
        o[kBikeFootOn + s] = OnAmount(d, kFootNear, kFootFar);
        if (d < g_footMin[s]) g_footMin[s] = d;
        if (d > g_footMax[s]) g_footMax[s] = d;
        if (o[kBikeHandOn + s] < g_handOnMin[s]) g_handOnMin[s] = o[kBikeHandOn + s];
        if (o[kBikeFootOn + s] < g_footOnMin[s]) g_footOnMin[s] = o[kBikeFootOn + s];
    }
    for (int k = 0; k < kLinkBikeFloats; ++k) out[k] = o[k];
    ++g_samples;
    return true;
}

// ---- B12: the bike lying in the world while the rider is off it ----
namespace {
bool g_dropOn = false;           // rider off the bike, tracking
bool g_dropConfirmed = false, g_dropAttached = false, g_dropHaveStart = false;
float g_dropRider0[3] = {}, g_dropLocal0[3] = {}, g_dropWorld0[3] = {};
bool g_haveOffset = false;       // g_offset from some riding pose (else: identity; test 13:57 showed identity)
unsigned g_dropFound = 0, g_dropValid = 0;
float g_dropDist = 0.0f;
} // namespace

bool DroppedBikeTick(unsigned long skater, unsigned long riderSkeleton, bool onBike, const float* orient16,
                     const float* pos3, void (*log)(const char*, ...), float* out12)
{
    if (g_haveRest) g_haveOffset = true;
    if (onBike || !skater) {
        if (g_dropOn) {
            g_dropOn = false;
            g_dropHaveStart = false;
        }
        return false;
    }
    if (!g_dropOn) {
        g_dropOn = true;
        g_dropConfirmed = g_dropAttached = g_dropHaveStart = false;
    }
    const unsigned long bikeSkel = FindBikeSkeleton(riderSkeleton, skater, log);
    if (!bikeSkel) return false;
    static BoneMatrix bb[32];
    const int bn = ReadSkeletonBones(bikeSkel, bb, 32);
    if (bn <= 0) return false;
    int fi = -1;
    for (int i = 0; i < bn; ++i)
        if (bb[i].name == kBikeBoneFrame) { fi = i; break; }
    if (fi < 0) return false;
    ++g_dropFound;
    // the board convention (offset x frame) in the rider's space, then THAW's world
    M4 frame = Clean(bb[fi].m), b;
    if (g_haveOffset) Mul4(g_offset.m, frame.m, b.m);
    else b = frame;
    float w[12];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            w[r * 3 + c] = b.m[r * 4] * orient16[c] + b.m[r * 4 + 1] * orient16[4 + c] + b.m[r * 4 + 2] * orient16[8 + c];
    for (int c = 0; c < 3; ++c)
        w[9 + c] = pos3[c] + b.m[12] * orient16[c] + b.m[13] * orient16[4 + c] + b.m[14] * orient16[8 + c];
    const float local[3] = {b.m[12], b.m[13], b.m[14]};
    if (!g_dropHaveStart) {
        g_dropHaveStart = true;
        for (int c = 0; c < 3; ++c) {
            g_dropRider0[c] = pos3[c];
            g_dropLocal0[c] = local[c];
            g_dropWorld0[c] = w[9 + c];
        }
    }
    if (!g_dropConfirmed && !g_dropAttached && Dist(pos3, g_dropRider0) > 10.0f) {
        const float worldMoved = Dist(w + 9, g_dropWorld0), localMoved = Dist(local, g_dropLocal0);
        if (worldMoved < 3.0f) {
            g_dropConfirmed = true;
            log("bike parts: B12: reTHAWed's bike stays in the world after getting off (moved %.1f units while the "
                "rider moved %.0f): the GTA bike will be put there", worldMoved, Dist(pos3, g_dropRider0));
        } else if (localMoved < 2.0f) {
            g_dropAttached = true;
            log("bike parts: B12: reTHAWed's bike skeleton moves WITH the rider after getting off (not lying in the "
                "world): not used");
        }
    }
    g_dropDist = Dist(w + 9, pos3);
    if (!g_dropConfirmed) return false;
    for (int i = 0; i < 12; ++i) out12[i] = w[i];
    ++g_dropValid;
    return true;
}

void BikePartsSecond(void (*log)(const char*, ...))
{
    if (!g_onBike && g_dropOn && (g_dropFound || g_dropValid)) {
        log("bike parts: B12 (off the bike): bike skeleton read %u times, sent %u times; bike %.0f units from the "
            "rider%s", g_dropFound, g_dropValid, g_dropDist,
            g_dropAttached ? " (moves with the rider: not used)" : (g_dropConfirmed ? "" : " (not confirmed yet)"));
        g_dropFound = g_dropValid = 0;
    }
    if (!g_onBike || !g_haveRest) return;
    log("bike parts (1 s, %u samples): fork turned up to %.0f deg, cranks went round %.0f deg | lowest 'on': hands "
        "%.1f %.1f, feet %.1f %.1f (bike's right, left) | crank now %.2f %.2f | feet to crank axle %.0f-%.0f, "
        "%.0f-%.0f units", g_samples, g_forkMax, g_crankTurn, g_handOnMin[0], g_handOnMin[1], g_footOnMin[0],
        g_footOnMin[1], g_out[kBikeCrank + 1], g_out[kBikeCrank + 2], g_footMin[0], g_footMax[0], g_footMin[1],
        g_footMax[1]);
    g_footMin[0] = g_footMin[1] = 1e9f;
    g_footMax[0] = g_footMax[1] = 0.0f;
    g_forkMax = 0.0f;
    g_crankTurn = 0.0f;
    g_samples = 0;
    g_handOnMin[0] = g_handOnMin[1] = g_footOnMin[0] = g_footOnMin[1] = 1.0f;
}

} // namespace mp::thaw
