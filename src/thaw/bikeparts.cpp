// my-passthrough: reTHAWed's bike parts for GTA. READ ONLY. See bikeparts.h.

#include <windows.h>

#include <cmath>

#include "bikeparts.h"
#include "pose.h"

namespace mp::thaw {
namespace {

constexpr int kParts = 4;
const char* const kPartNames[kParts] = {"Bone_Board_Nose", "Bone_Trucks_Nose", "Bone_Board_Tail", "Bone_Trucks_Tail"};
constexpr int kNose = 0, kTail = 2;    // the fork, the cranks
constexpr DWORD kRestAfterMs = 4000;   // riding pose taken this long after getting on (the switch takes ~3 s)
constexpr float kLogAngle = 20.0f;     // degrees from the riding pose worth a line
constexpr DWORD kLineGapMs = 100;      // at most 10 lines a second
constexpr DWORD kAlwaysTurnedMs = 3000; // turned this long without a break = turns all the time
// B7: hand on the grip / foot on the pedal: fully within the first distance
// (THAW units) of where it was in the riding pose, not at all beyond the second
constexpr float kHandNear = 3.0f, kHandFar = 10.0f;
// B7 fix 1 (test 13:15: feet counted "off" all the time while pedalling): a foot
// is on its pedal while it keeps its distance from the crank axle and its place
// along the axle (as in the riding pose), wherever round the circle it is
constexpr float kFootNear = 3.0f, kFootFar = 8.0f;

// extra bones for B7 (wrists, ankles, toes)
enum Extra { kWristL, kWristR, kAnkleL, kToeL, kAnkleR, kToeR, kExtraCount };
const char* const kExtraNames[kExtraCount] = {"Bone_Wrist_L", "Bone_Wrist_R", "Bone_Ankle_L",
                                              "Bone_Toe_L",   "Bone_Ankle_R", "Bone_Toe_R"};

struct Part {
    float rest[9] = {};       // turn relative to Board_Root in the riding pose (rows)
    float angle = 0.0f;       // now, degrees from rest
    float axis[3] = {};       // about this axis (the part's own space in the riding pose)
    float off[3] = {};        // position relative to Board_Root (its space, THAW units)
    DWORD turnedSince = 0;    // > kLogAngle since (0 = not)
    float maxAngle = 0.0f;    // this second
    float maxAxis[3] = {};
};

Part g_part[kParts];
bool g_found = false;
bool g_haveRest = false;
DWORD g_onSince = 0;
DWORD g_lastLine = 0;
unsigned g_lines = 0, g_samples = 0;
bool g_onBike = false;

// B7 riding pose: per bike side (0 = right, +x; 1 = left)
float g_gripLocal[2][3] = {};  // the hand on that side, in the fork's space
float g_footR0[2] = {}, g_footX0[2] = {}; // fix 1: each foot's distance from the axle / place along it, riding pose
float g_e1[3] = {}, g_e2[3] = {}; // fix 1: two directions square to the crank axle (bike frame space)
float g_phaseOff = 0.0f;       // fix 1: right-foot angle minus the crank bone's angle (kept while the foot is off)
float g_lastFootAng = 0.0f, g_lastTailAng = 0.0f;
bool g_haveLastAng = false;
float g_footTurn = 0.0f, g_tailTurn = 0.0f; // research: degrees turned this second (feet on)
int g_handBone[2] = {}, g_footBone[2] = {}; // which Extra (wrist / ankle) is on that side
float g_armRest[3] = {};       // crank axle -> right foot, bike frame space, at rest
float g_out[kLinkBikeFloats] = {};
float g_handOnMin[2] = {1, 1}, g_footOnMin[2] = {1, 1}; // this second (log)

// rows of a 4x4 bone matrix: right, up, at; the rotation of `b` in `r`'s
// space: rel = B x R^T (row vectors, as the game's bone matrices)
void Relative(const float* b, const float* r, float* rel)
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            rel[i * 3 + j] = b[i * 4 + 0] * r[j * 4 + 0] + b[i * 4 + 1] * r[j * 4 + 1] + b[i * 4 + 2] * r[j * 4 + 2];
}

// a point (skater space) in a bone's space: (p - pos) . rows
void ToBone(const float* p, const float* m, float* out)
{
    const float d[3] = {p[0] - m[12], p[1] - m[13], p[2] - m[14]};
    for (int j = 0; j < 3; ++j) out[j] = d[0] * m[j * 4] + d[1] * m[j * 4 + 1] + d[2] * m[j * 4 + 2];
}

// a point in a part's space -> bike frame space: off + l x rel
void FromPart(const float* l, const float* rel, const float* off, float* out)
{
    for (int j = 0; j < 3; ++j) out[j] = off[j] + l[0] * rel[j] + l[1] * rel[3 + j] + l[2] * rel[6 + j];
}

float Dist(const float* a, const float* b)
{
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

float Wrap(float a) // to -pi..pi
{
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

// fix 1: a foot round the crank axle (axle at `c`, direction `ax`): its
// distance from the axle, its place along it, its angle (g_e1 / g_e2)
void FootPolar(const float* f, const float* c, const float* ax, float& r, float& x, float& ang)
{
    const float d[3] = {f[0] - c[0], f[1] - c[1], f[2] - c[2]};
    x = d[0] * ax[0] + d[1] * ax[1] + d[2] * ax[2];
    const float a = d[0] * g_e1[0] + d[1] * g_e1[1] + d[2] * g_e1[2];
    const float b = d[0] * g_e2[0] + d[1] * g_e2[1] + d[2] * g_e2[2];
    r = std::sqrt(a * a + b * b);
    ang = std::atan2(b, a);
}

float OnAmount(float d, float nearDist, float farDist)
{
    if (d <= nearDist) return 1.0f;
    if (d >= farDist) return 0.0f;
    return (farDist - d) / (farDist - nearDist);
}

// angle (degrees) and axis of a * b^T
float AngleBetween(const float* a, const float* b, float* axis)
{
    float d[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            d[i * 3 + j] = a[i * 3 + 0] * b[j * 3 + 0] + a[i * 3 + 1] * b[j * 3 + 1] + a[i * 3 + 2] * b[j * 3 + 2];
    float c = (d[0] + d[4] + d[8] - 1.0f) * 0.5f;
    if (c > 1.0f) c = 1.0f;
    if (c < -1.0f) c = -1.0f;
    axis[0] = d[5] - d[7];
    axis[1] = d[6] - d[2];
    axis[2] = d[1] - d[3];
    const float l = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (l > 1e-4f) {
        for (int k = 0; k < 3; ++k) axis[k] /= l;
    } else if (c < 0.0f) { // about 180 degrees: axis from the diagonal
        int k = (d[0] > d[4] && d[0] > d[8]) ? 0 : (d[4] > d[8] ? 1 : 2);
        axis[0] = axis[1] = axis[2] = 0.0f;
        axis[k] = 1.0f;
    }
    return std::acos(c) * 57.29578f;
}

// M = rest^T x now (row vectors: now = rest x M), 3x3
void TurnSince(const float* rest, const float* now, float* m)
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[i * 3 + j] = rest[0 * 3 + i] * now[0 * 3 + j] + rest[1 * 3 + i] * now[1 * 3 + j] + rest[2 * 3 + i] * now[2 * 3 + j];
}

void Reset()
{
    g_haveRest = false;
    g_found = false;
    for (Part& p : g_part) p = Part{};
}

} // namespace

bool BikePartsTick(unsigned long skater, bool onBike, void (*log)(const char*, ...), float* out)
{
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
    static BoneMatrix bones[64];
    const int n = ReadAllBones(skater, bones, 64);
    if (n <= 0) return false;
    static unsigned long sums[kParts + 1] = {}, extraSums[kExtraCount] = {};
    if (!sums[0]) {
        sums[0] = NameChecksum("Bone_Board_Root");
        for (int k = 0; k < kParts; ++k) sums[k + 1] = NameChecksum(kPartNames[k]);
        for (int k = 0; k < kExtraCount; ++k) extraSums[k] = NameChecksum(kExtraNames[k]);
    }
    auto find = [&](unsigned long sum) {
        for (int i = 0; i < n; ++i)
            if (bones[i].name == sum) return i;
        return -1;
    };
    int idx[kParts + 1], ex[kExtraCount];
    for (int k = 0; k <= kParts; ++k) idx[k] = find(sums[k]);
    bool haveExtra = true;
    for (int k = 0; k < kExtraCount; ++k) {
        ex[k] = find(extraSums[k]);
        if (ex[k] < 0) haveExtra = false;
    }
    if (idx[0] < 0) return false;
    if (!g_found) {
        g_found = true;
        log("bike parts: on the bike, %d bones; Board_Root #%d, Board_Nose #%d, Trucks_Nose #%d, Board_Tail #%d, "
            "Trucks_Tail #%d, wrists / ankles / toes %s (riding pose taken in %lu s)", n, idx[0], idx[1], idx[2],
            idx[3], idx[4], haveExtra ? "found" : "MISSING", kRestAfterMs / 1000);
    }
    const float* root = bones[idx[0]].m;
    float rel[kParts][9] = {};
    for (int k = 0; k < kParts; ++k) {
        if (idx[k + 1] < 0) continue;
        const float* b = bones[idx[k + 1]].m;
        Relative(b, root, rel[k]);
        ToBone(b + 12, root, g_part[k].off);
    }
    // hands and feet in the bike frame's space
    float hand[2][3] = {}, foot[2][3] = {}; // index: 0 = the rider's left, 1 = right
    if (haveExtra) {
        ToBone(bones[ex[kWristL]].m + 12, root, hand[0]);
        ToBone(bones[ex[kWristR]].m + 12, root, hand[1]);
        float a[3], t[3];
        ToBone(bones[ex[kAnkleL]].m + 12, root, a);
        ToBone(bones[ex[kToeL]].m + 12, root, t);
        for (int j = 0; j < 3; ++j) foot[0][j] = (a[j] + t[j]) * 0.5f; // about the ball of the foot
        ToBone(bones[ex[kAnkleR]].m + 12, root, a);
        ToBone(bones[ex[kToeR]].m + 12, root, t);
        for (int j = 0; j < 3; ++j) foot[1][j] = (a[j] + t[j]) * 0.5f;
    }
    const bool canB7 = haveExtra && idx[kNose + 1] >= 0 && idx[kTail + 1] >= 0;

    if (!g_haveRest) {
        if (now - g_onSince < kRestAfterMs) return false;
        g_haveRest = true;
        for (int k = 0; k < kParts; ++k)
            if (idx[k + 1] >= 0)
                for (int j = 0; j < 9; ++j) g_part[k].rest[j] = rel[k][j];
        if (canB7) {
            // which of the rider's hands / feet is on the bike's right (+x)
            g_handBone[0] = hand[1][0] >= hand[0][0] ? 1 : 0;
            g_handBone[1] = 1 - g_handBone[0];
            g_footBone[0] = foot[1][0] >= foot[0][0] ? 1 : 0;
            g_footBone[1] = 1 - g_footBone[0];
            const Part& nose = g_part[kNose];
            const Part& tail = g_part[kTail];
            for (int s = 0; s < 2; ++s) {
                // hand in the fork's space: (p - off) . rest rows
                const float* h = hand[g_handBone[s]];
                const float dh[3] = {h[0] - nose.off[0], h[1] - nose.off[1], h[2] - nose.off[2]};
                for (int j = 0; j < 3; ++j)
                    g_gripLocal[s][j] = dh[0] * nose.rest[j * 3] + dh[1] * nose.rest[j * 3 + 1] + dh[2] * nose.rest[j * 3 + 2];
            }
            // crank: axle -> the right foot, square to the crank's axis (its x row)
            const float* ax = tail.rest;
            float arm[3];
            for (int j = 0; j < 3; ++j) arm[j] = foot[g_footBone[0]][j] - tail.off[j];
            const float d = arm[0] * ax[0] + arm[1] * ax[1] + arm[2] * ax[2];
            for (int j = 0; j < 3; ++j) arm[j] -= ax[j] * d;
            const float l = std::sqrt(arm[0] * arm[0] + arm[1] * arm[1] + arm[2] * arm[2]);
            for (int j = 0; j < 3; ++j) g_armRest[j] = l > 1e-4f ? arm[j] / l : (j == 2 ? -1.0f : 0.0f);
            // fix 1: e1 = the riding pose's crank direction, e2 = axle x e1
            for (int j = 0; j < 3; ++j) g_e1[j] = g_armRest[j];
            g_e2[0] = ax[1] * g_e1[2] - ax[2] * g_e1[1];
            g_e2[1] = ax[2] * g_e1[0] - ax[0] * g_e1[2];
            g_e2[2] = ax[0] * g_e1[1] - ax[1] * g_e1[0];
            for (int s = 0; s < 2; ++s) {
                float ang;
                FootPolar(foot[g_footBone[s]], tail.off, ax, g_footR0[s], g_footX0[s], ang);
            }
            g_phaseOff = 0.0f;
            g_haveLastAng = false;
        }
        log("bike parts: riding pose taken; offsets from Board_Root: nose %.1f %.1f %.1f | trucks nose %.1f %.1f %.1f "
            "| tail %.1f %.1f %.1f | trucks tail %.1f %.1f %.1f", g_part[0].off[0], g_part[0].off[1], g_part[0].off[2],
            g_part[1].off[0], g_part[1].off[1], g_part[1].off[2], g_part[2].off[0], g_part[2].off[1], g_part[2].off[2],
            g_part[3].off[0], g_part[3].off[1], g_part[3].off[2]);
        if (canB7)
            log("bike parts: B7 riding pose: right hand %.1f %.1f %.1f, left hand %.1f %.1f %.1f (bike frame); right foot "
                "%.1f %.1f %.1f, left foot %.1f %.1f %.1f; crank to the right pedal %.2f %.2f %.2f; the rider's %s hand "
                "and %s foot are on the bike's right",
                hand[g_handBone[0]][0], hand[g_handBone[0]][1], hand[g_handBone[0]][2], hand[g_handBone[1]][0],
                hand[g_handBone[1]][1], hand[g_handBone[1]][2], foot[g_footBone[0]][0], foot[g_footBone[0]][1],
                foot[g_footBone[0]][2], foot[g_footBone[1]][0], foot[g_footBone[1]][1], foot[g_footBone[1]][2],
                g_armRest[0], g_armRest[1], g_armRest[2], g_handBone[0] ? "right" : "left",
                g_footBone[0] ? "right" : "left");
        else
            log("bike parts: B7 off: bones missing");
        return false;
    }

    // ---- B7: what GTA needs ----
    bool valid = false;
    if (canB7) {
        const Part& nose = g_part[kNose];
        const Part& tail = g_part[kTail];
        float* o = g_out;
        TurnSince(nose.rest, rel[kNose], o + kBikeFork);
        float mc[9];
        TurnSince(tail.rest, rel[kTail], mc);
        float arm[3];
        for (int j = 0; j < 3; ++j) arm[j] = g_armRest[0] * mc[j] + g_armRest[1] * mc[3 + j] + g_armRest[2] * mc[6 + j];
        // fix 1: the crank's angle from the rider's feet while they are on the
        // pedals (that's what CJ's feet stand on); from the crank bone,
        // continuing from there, while they are off (crank flips)
        const float* ax = tail.rest;
        const float tailAng = std::atan2(arm[0] * g_e2[0] + arm[1] * g_e2[1] + arm[2] * g_e2[2],
                                         arm[0] * g_e1[0] + arm[1] * g_e1[1] + arm[2] * g_e1[2]);
        float footAng[2];
        for (int s = 0; s < 2; ++s) {
            float r, x;
            FootPolar(foot[g_footBone[s]], tail.off, ax, r, x, footAng[s]);
            const float dev = std::sqrt((r - g_footR0[s]) * (r - g_footR0[s]) + (x - g_footX0[s]) * (x - g_footX0[s]));
            o[kBikeFootOn + s] = OnAmount(dev, kFootNear, kFootFar);
        }
        float crankAng;
        bool fromFoot = true;
        if (o[kBikeFootOn] >= 0.5f) crankAng = footAng[0];
        else if (o[kBikeFootOn + 1] >= 0.5f) crankAng = Wrap(footAng[1] + 3.14159265f);
        else fromFoot = false, crankAng = Wrap(tailAng + g_phaseOff);
        if (fromFoot) {
            g_phaseOff = Wrap(crankAng - tailAng);
            if (g_haveLastAng) { // research: does the crank bone turn as fast as the feet?
                g_footTurn += std::fabs(Wrap(crankAng - g_lastFootAng)) * 57.29578f;
                g_tailTurn += std::fabs(Wrap(tailAng - g_lastTailAng)) * 57.29578f;
            }
            g_lastFootAng = crankAng;
            g_lastTailAng = tailAng;
            g_haveLastAng = true;
        } else {
            g_haveLastAng = false;
        }
        for (int j = 0; j < 3; ++j)
            o[kBikeCrank + j] = g_e1[j] * std::cos(crankAng) + g_e2[j] * std::sin(crankAng);
        for (int s = 0; s < 2; ++s) {
            float grip[3];
            FromPart(g_gripLocal[s], rel[kNose], nose.off, grip);
            o[kBikeHandOn + s] = OnAmount(Dist(hand[g_handBone[s]], grip), kHandNear, kHandFar);
            if (o[kBikeHandOn + s] < g_handOnMin[s]) g_handOnMin[s] = o[kBikeHandOn + s];
            if (o[kBikeFootOn + s] < g_footOnMin[s]) g_footOnMin[s] = o[kBikeFootOn + s];
        }
        for (int k = 0; k < kLinkBikeFloats; ++k) out[k] = o[k];
        valid = true;
    }

    // ---- B6 log ----
    ++g_samples;
    bool worth = false;
    for (int k = 0; k < kParts; ++k) {
        Part& p = g_part[k];
        if (idx[k + 1] < 0) continue;
        p.angle = AngleBetween(rel[k], p.rest, p.axis);
        if (p.angle > p.maxAngle) {
            p.maxAngle = p.angle;
            for (int j = 0; j < 3; ++j) p.maxAxis[j] = p.axis[j];
        }
        if (p.angle > kLogAngle) {
            if (!p.turnedSince) p.turnedSince = now;
            if (now - p.turnedSince < kAlwaysTurnedMs) worth = true;
        } else {
            p.turnedSince = 0;
        }
    }
    // the cranks turn all the time while pedalling: only the fork makes a line
    worth = g_part[kNose].angle > kLogAngle;
    if (!worth || now - g_lastLine < kLineGapMs) return valid;
    g_lastLine = now;
    ++g_lines;
    const Part* q = g_part;
    log("bike parts: fork %3.0f deg (%.2f %.2f %.2f) | cranks %3.0f (%.2f %.2f %.2f) | hands on %.1f %.1f | feet on "
        "%.1f %.1f", q[kNose].angle, q[kNose].axis[0], q[kNose].axis[1], q[kNose].axis[2], q[kTail].angle,
        q[kTail].axis[0], q[kTail].axis[1], q[kTail].axis[2], g_out[kBikeHandOn], g_out[kBikeHandOn + 1],
        g_out[kBikeFootOn], g_out[kBikeFootOn + 1]);
    return valid;
}

void BikePartsSecond(void (*log)(const char*, ...))
{
    if (!g_onBike || !g_haveRest) return;
    const Part* q = g_part;
    log("bike parts (1 s, %u samples, %u lines): most turned: fork %.0f deg, cranks %.0f deg | lowest 'on' this second: "
        "hands %.1f %.1f, feet %.1f %.1f (bike's right, left) | crank now %.2f %.2f %.2f | feet on: feet went round "
        "%.0f deg, the crank bone %.0f deg",
        g_samples, g_lines, q[kNose].maxAngle, q[kTail].maxAngle, g_handOnMin[0], g_handOnMin[1], g_footOnMin[0],
        g_footOnMin[1], g_out[kBikeCrank], g_out[kBikeCrank + 1], g_out[kBikeCrank + 2], g_footTurn, g_tailTurn);
    g_footTurn = g_tailTurn = 0.0f;
    for (Part& p : g_part) p.maxAngle = 0.0f;
    g_samples = g_lines = 0;
    g_handOnMin[0] = g_handOnMin[1] = g_footOnMin[0] = g_footOnMin[1] = 1.0f;
}

} // namespace mp::thaw
