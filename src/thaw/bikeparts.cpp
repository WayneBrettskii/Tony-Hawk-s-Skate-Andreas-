// my-passthrough: which bike bones turn in BMX tricks. LOG ONLY. See bikeparts.h.

#include <windows.h>

#include <cmath>

#include "bikeparts.h"
#include "pose.h"

namespace mp::thaw {
namespace {

constexpr int kParts = 4;
const char* const kPartNames[kParts] = {"Bone_Board_Nose", "Bone_Trucks_Nose", "Bone_Board_Tail", "Bone_Trucks_Tail"};
constexpr DWORD kRestAfterMs = 4000;   // riding pose taken this long after getting on (the switch takes ~3 s)
constexpr float kLogAngle = 20.0f;     // degrees from the riding pose worth a line
constexpr DWORD kLineGapMs = 100;      // at most 10 lines a second
constexpr DWORD kAlwaysTurnedMs = 3000; // turned this long without a break = turns all the time (wheel?)

struct Part {
    float rest[9] = {};       // turn relative to Board_Root in the riding pose (rows)
    float angle = 0.0f;       // now, degrees from rest
    float axis[3] = {};       // about this axis (Board_Root's space)
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

// rows of a 4x4 bone matrix: right, up, at; then the rotation of `b` in `r`'s
// space: rel = B x R^T (row vectors, as the game's bone matrices)
void Relative(const float* b, const float* r, float* rel)
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            rel[i * 3 + j] = b[i * 4 + 0] * r[j * 4 + 0] + b[i * 4 + 1] * r[j * 4 + 1] + b[i * 4 + 2] * r[j * 4 + 2];
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

void Reset()
{
    g_haveRest = false;
    g_found = false;
    for (Part& p : g_part) p = Part{};
}

} // namespace

void BikePartsTick(unsigned long skater, bool onBike, void (*log)(const char*, ...))
{
    const DWORD now = GetTickCount();
    if (!onBike || !skater) {
        if (g_onBike) Reset();
        g_onBike = false;
        return;
    }
    if (!g_onBike) {
        g_onBike = true;
        g_onSince = now;
        Reset();
    }
    static BoneMatrix bones[64];
    const int n = ReadAllBones(skater, bones, 64);
    if (n <= 0) return;
    static unsigned long sums[kParts + 1] = {};
    if (!sums[0]) {
        sums[0] = NameChecksum("Bone_Board_Root");
        for (int k = 0; k < kParts; ++k) sums[k + 1] = NameChecksum(kPartNames[k]);
    }
    int idx[kParts + 1];
    for (int k = 0; k <= kParts; ++k) {
        idx[k] = -1;
        for (int i = 0; i < n; ++i)
            if (bones[i].name == sums[k]) { idx[k] = i; break; }
    }
    if (idx[0] < 0) return;
    if (!g_found) {
        g_found = true;
        log("bike parts: on the bike, %d bones; Board_Root #%d, Board_Nose #%d, Trucks_Nose #%d, Board_Tail #%d, "
            "Trucks_Tail #%d (riding pose taken in %lu s)", n, idx[0], idx[1], idx[2], idx[3], idx[4],
            kRestAfterMs / 1000);
    }
    const float* root = bones[idx[0]].m;
    float rel[kParts][9];
    for (int k = 0; k < kParts; ++k) {
        if (idx[k + 1] < 0) continue;
        const float* b = bones[idx[k + 1]].m;
        Relative(b, root, rel[k]);
        const float d[3] = {b[12] - root[12], b[13] - root[13], b[14] - root[14]};
        for (int j = 0; j < 3; ++j) g_part[k].off[j] = d[0] * root[j * 4] + d[1] * root[j * 4 + 1] + d[2] * root[j * 4 + 2];
    }
    if (!g_haveRest) {
        if (now - g_onSince < kRestAfterMs) return;
        g_haveRest = true;
        for (int k = 0; k < kParts; ++k)
            if (idx[k + 1] >= 0)
                for (int j = 0; j < 9; ++j) g_part[k].rest[j] = rel[k][j];
        log("bike parts: riding pose taken; offsets from Board_Root: nose %.1f %.1f %.1f | trucks nose %.1f %.1f %.1f "
            "| tail %.1f %.1f %.1f | trucks tail %.1f %.1f %.1f", g_part[0].off[0], g_part[0].off[1], g_part[0].off[2],
            g_part[1].off[0], g_part[1].off[1], g_part[1].off[2], g_part[2].off[0], g_part[2].off[1], g_part[2].off[2],
            g_part[3].off[0], g_part[3].off[1], g_part[3].off[2]);
        return;
    }
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
    if (!worth || now - g_lastLine < kLineGapMs) return;
    g_lastLine = now;
    ++g_lines;
    const Part* q = g_part;
    log("bike parts: nose %3.0f deg (%.2f %.2f %.2f) | trucks nose %3.0f (%.2f %.2f %.2f) | tail %3.0f (%.2f %.2f %.2f) "
        "| trucks tail %3.0f (%.2f %.2f %.2f)", q[0].angle, q[0].axis[0], q[0].axis[1], q[0].axis[2], q[1].angle,
        q[1].axis[0], q[1].axis[1], q[1].axis[2], q[2].angle, q[2].axis[0], q[2].axis[1], q[2].axis[2], q[3].angle,
        q[3].axis[0], q[3].axis[1], q[3].axis[2]);
}

void BikePartsSecond(void (*log)(const char*, ...))
{
    if (!g_onBike || !g_haveRest) return;
    const Part* q = g_part;
    log("bike parts (1 s, %u samples, %u lines): most turned: nose %.0f deg (%.2f %.2f %.2f)%s | trucks nose %.0f "
        "(%.2f %.2f %.2f)%s | tail %.0f (%.2f %.2f %.2f)%s | trucks tail %.0f (%.2f %.2f %.2f)%s",
        g_samples, g_lines, q[0].maxAngle, q[0].maxAxis[0], q[0].maxAxis[1], q[0].maxAxis[2],
        q[0].turnedSince && GetTickCount() - q[0].turnedSince >= kAlwaysTurnedMs ? " always" : "", q[1].maxAngle,
        q[1].maxAxis[0], q[1].maxAxis[1], q[1].maxAxis[2],
        q[1].turnedSince && GetTickCount() - q[1].turnedSince >= kAlwaysTurnedMs ? " always" : "", q[2].maxAngle,
        q[2].maxAxis[0], q[2].maxAxis[1], q[2].maxAxis[2],
        q[2].turnedSince && GetTickCount() - q[2].turnedSince >= kAlwaysTurnedMs ? " always" : "", q[3].maxAngle,
        q[3].maxAxis[0], q[3].maxAxis[1], q[3].maxAxis[2],
        q[3].turnedSince && GetTickCount() - q[3].turnedSince >= kAlwaysTurnedMs ? " always" : "");
    for (Part& p : g_part) p.maxAngle = 0.0f;
    g_samples = g_lines = 0;
}

} // namespace mp::thaw
