// SkateAndreas: CJ's body copies the skater's body. See body.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CPed.h"
#include "CPlayerPed.h"
#include "CTimer.h"
#include "RenderWare.h"
#include "common.h"
#include <windows.h>

#include "body.h"
#include "recorder.h"
#include "../common/link.h"

#include <cmath>
#include <cstdio>

namespace mp::gta {
namespace {

// ---- settings ----
bool g_copyBody = true;              // F10 switches it (5-3f); false = CJ keeps GTA's animation only
constexpr ULONGLONG kStaleMs = 200;  // target older than this = don't use
constexpr int kMaxNodes = 64;
// Fix 3: neck, head and the two face bones (GTA ids 5, 8, 6, 7) keep the pose
// they had in the first frame of skate mode (relative to the chest), so GTA's
// head turning (looking at things / up) and talking mouth don't show.
// 5-3f: CJ's skeleton log shows bone 4 is the neck (the collar bones and the
// head hang from it) and 5 the head (jaw and brows 6, 7, 8 hang from it);
// plugin-sdk's names are one off. Fix 3 had missed the neck.
// 5-3g: plus the collar bones (21, 31): nothing copies them from the skater,
// so GTA's skating animation kept rolling the shoulders under the copied
// arms (the shoulder "shimmy" with the copy on; user's F10 test, 10:51).
// (5-3h: now every bone is held; see Apply.)
// 5-3f: take the calm pose only after this many drawn frames of skate mode,
// once GTA's look-at and talking (switched off by skate mode) have faded.
constexpr int kLockAfterFrames = 20;

// Order of the bones the THAW plugin sends (src/thaw/pose.cpp kBodyBones).
enum ThawBone {
    T_PELVIS, T_STOMACH_UP, T_NECK, T_HEAD,
    T_BICEP_L, T_FOREARM_L, T_WRIST_L, T_BICEP_R, T_FOREARM_R, T_WRIST_R,
    T_THIGH_L, T_KNEE_L, T_ANKLE_L, T_TOE_L, T_THIGH_R, T_KNEE_R, T_ANKLE_R, T_TOE_R,
    T_CHEST, T_CHEST_SIDE,  // fix 2: chest, and a point towards the skater's left along the chest
    T_COUNT
};

static_assert(T_COUNT == mp::kLinkBodyBones, "body bone lists differ (link.h, src/thaw/pose.cpp)");

// GTA SA ped bone IDs (plugin-sdk ePedBones).
enum GtaBone {
    // 5-3i: CJ's skeleton log: bone 4 is the neck (collar bones and head hang
    // from it), 5 the head (jaw / brows hang from it). plugin-sdk calls 5 "neck",
    // so the hips and chest had been aimed at CJ's HEAD while the targets were
    // the skater's NECK: that tipped him back (user 11:14: "leaning back,
    // looking up").
    G_PELVIS1 = 1, G_SPINE1 = 3, G_NECK = 4, G_HEAD = 5,
    G_R_CLAVICLE = 21, G_L_CLAVICLE = 31,
    G_R_SHOULDER = 22, G_R_ELBOW = 23, G_R_WRIST = 24, G_R_HAND = 25,
    G_L_SHOULDER = 32, G_L_ELBOW = 33, G_L_WRIST = 34, G_L_HAND = 35,
    G_L_HIP = 41, G_L_KNEE = 42, G_L_ANKLE = 43, G_L_FOOT = 44,
    G_R_HIP = 51, G_R_KNEE = 52, G_R_ANKLE = 53, G_R_FOOT = 54,
};

enum OpKind { MOVE, AIM, AIM2 };
struct Op {
    OpKind kind;
    int bone;        // GTA bone turned (or moved)
    int end;         // GTA bone whose direction from `bone` is matched
    int tFrom, tTo;  // skater bones giving the wanted direction
    int sideA, sideB;            // AIM2: GTA bones giving a second direction (B - A)
    int tSideA, tSideB;          // AIM2: skater bones for it
};
// Top to bottom: hips first, then spine, head, arms, legs.
const Op kOps[] = {
    {MOVE, G_PELVIS1, 0, T_PELVIS, T_PELVIS, 0, 0, 0, 0},
    {AIM2, G_PELVIS1, G_NECK, T_PELVIS, T_NECK, G_L_HIP, G_R_HIP, T_THIGH_L, T_THIGH_R},
    // chest twist: CJ's collar-bone line (left to right) matched to the skater's
    // chest axis (from its left point to the chest = towards its right). Fix 2:
    // not the shoulder joints, which move with the arms (shoulder "shimmy").
    {AIM2, G_SPINE1, G_NECK, T_STOMACH_UP, T_NECK, G_L_CLAVICLE, G_R_CLAVICLE, T_CHEST_SIDE, T_CHEST},
    // 5-3i: neck aimed so neck-to-head matches the skater's (fix 3 had removed
    // an aim that paired the wrong bones: CJ's head-to-jaw with the skater's
    // neck-to-head, which tipped his face up).
    {AIM, G_NECK, G_HEAD, T_NECK, T_HEAD, 0, 0, 0, 0},
    {AIM, G_R_SHOULDER, G_R_ELBOW, T_BICEP_R, T_FOREARM_R, 0, 0, 0, 0},
    {AIM, G_R_ELBOW, G_R_WRIST, T_FOREARM_R, T_WRIST_R, 0, 0, 0, 0},
    {AIM, G_L_SHOULDER, G_L_ELBOW, T_BICEP_L, T_FOREARM_L, 0, 0, 0, 0},
    {AIM, G_L_ELBOW, G_L_WRIST, T_FOREARM_L, T_WRIST_L, 0, 0, 0, 0},
    {AIM, G_L_HIP, G_L_KNEE, T_THIGH_L, T_KNEE_L, 0, 0, 0, 0},
    {AIM, G_L_KNEE, G_L_ANKLE, T_KNEE_L, T_ANKLE_L, 0, 0, 0, 0},
    {AIM, G_L_ANKLE, G_L_FOOT, T_ANKLE_L, T_TOE_L, 0, 0, 0, 0},
    {AIM, G_R_HIP, G_R_KNEE, T_THIGH_R, T_KNEE_R, 0, 0, 0, 0},
    {AIM, G_R_KNEE, G_R_ANKLE, T_KNEE_R, T_ANKLE_R, 0, 0, 0, 0},
    {AIM, G_R_ANKLE, G_R_FOOT, T_ANKLE_R, T_TOE_R, 0, 0, 0, 0},
};

// ---- small maths (row vectors, like RenderWare) ----
struct V3 {
    float x = 0, y = 0, z = 0;
    V3() = default;
    V3(float a, float b, float c) : x(a), y(b), z(c) {}
};
V3 operator+(V3 a, V3 b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
V3 operator-(V3 a, V3 b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
V3 operator*(V3 a, float k) { return V3(a.x * k, a.y * k, a.z * k); }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
float Len(V3 a) { return std::sqrt(Dot(a, a)); }
bool Unit(V3& a)
{
    const float l = Len(a);
    if (!(l > 1e-6f)) return false;
    a = a * (1.0f / l);
    return true;
}

struct M43 {  // rows: right, up, at, pos
    V3 r[3];
    V3 p;
};
V3 RotateRow(V3 v, const M43& m) { return m.r[0] * v.x + m.r[1] * v.y + m.r[2] * v.z; }
M43 Mul(const M43& a, const M43& b)  // a then b
{
    M43 o;
    for (int i = 0; i < 3; ++i) o.r[i] = RotateRow(a.r[i], b);
    o.p = RotateRow(a.p, b) + b.p;
    return o;
}
M43 InverseRigid(const M43& m)
{
    M43 o;
    // transpose of the 3x3 (rows may carry a little scale; fine for our use)
    o.r[0] = V3(m.r[0].x, m.r[1].x, m.r[2].x);
    o.r[1] = V3(m.r[0].y, m.r[1].y, m.r[2].y);
    o.r[2] = V3(m.r[0].z, m.r[1].z, m.r[2].z);
    o.p = RotateRow(m.p, o) * -1.0f;
    return o;
}
// Rotation about a unit axis, as a matrix acting on row vectors (v' = v * R).
M43 AxisAngle(V3 axis, float angle)
{
    const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
    const float x = axis.x, y = axis.y, z = axis.z;
    M43 m;
    m.r[0] = V3(t * x * x + c, t * x * y + s * z, t * x * z - s * y);
    m.r[1] = V3(t * x * y - s * z, t * y * y + c, t * y * z + s * x);
    m.r[2] = V3(t * x * z + s * y, t * y * z - s * x, t * z * z + c);
    return m;
}
// The rotation turning direction `from` onto `to` (both unit).
M43 FromTo(V3 from, V3 to)
{
    V3 axis = Cross(from, to);
    const float s = Len(axis), c = Dot(from, to);
    if (s < 1e-6f) {
        if (c > 0) return AxisAngle(V3(0, 0, 1), 0.0f);
        V3 any = std::fabs(from.x) < 0.9f ? V3(1, 0, 0) : V3(0, 1, 0);
        axis = Cross(from, any);
        Unit(axis);
        return AxisAngle(axis, 3.14159265f);
    }
    axis = axis * (1.0f / s);
    return AxisAngle(axis, std::atan2(s, c));
}

M43 FromRw(const RwMatrix& m)
{
    M43 o;
    o.r[0] = V3(m.right.x, m.right.y, m.right.z);
    o.r[1] = V3(m.up.x, m.up.y, m.up.z);
    o.r[2] = V3(m.at.x, m.at.y, m.at.z);
    o.p = V3(m.pos.x, m.pos.y, m.pos.z);
    return o;
}
void ToRw(const M43& m, RwMatrix& o)
{
    o.right.x = m.r[0].x; o.right.y = m.r[0].y; o.right.z = m.r[0].z;
    o.up.x = m.r[1].x; o.up.y = m.r[1].y; o.up.z = m.r[1].z;
    o.at.x = m.r[2].x; o.at.y = m.r[2].y; o.at.z = m.r[2].z;
    o.pos.x = m.p.x; o.pos.y = m.p.y; o.pos.z = m.p.z;
}
bool Finite(const M43& m)
{
    const float* f = &m.r[0].x;
    for (int i = 0; i < 12; ++i)
        if (!std::isfinite(f[i])) return false;
    return true;
}

// ---- state ----
struct Target {
    bool valid = false;
    ULONGLONG tick = 0;
    V3 pos[T_COUNT];  // GTA world positions of the skater's bones
} g_target;
// 8.29 B7: where CJ's hands / feet should be (GTA world): 0 grip on the bike's
// right, 1 grip on its left, 2 right pedal, 3 left pedal; how much (0..1)
struct LimbTargets {
    bool valid = false;
    ULONGLONG tick = 0;
    V3 pos[4];
    float weight[4] = {};
} g_limbs;
unsigned g_reached = 0; // limbs bent onto a target since the last log
// 8.53 step 3: gun mode's aim pose, on top of the skater copy: chest turned towards the point, right arm
// stretched at it, left hand under the barrel for two-handed guns (GTA world point; weight 0..1)
struct AimTarget {
    bool valid = false;
    ULONGLONG tick = 0;
    V3 point;
    float weight = 0.0f;
    bool twoHanded = false;
} g_aim;
constexpr float kAimChestMaxDeg = 60.0f; // test 49: was 70
// test 48 (video: "arms were going behind him"): the arm aims at most this far round from the (turned)
// chest's front, and at most kAimArmPitchDeg up or down; a point further round is aimed at the edge
constexpr float kAimArmMaxDeg = 55.0f;
constexpr float kAimLowerShare = 0.45f; // test 49: part of the chest turn given to the bone below spine1
constexpr float kAimArmPitchDeg = 60.0f;
unsigned g_aimArmClamped = 0; // frames where the arm's direction was pulled back in front of the chest
unsigned g_aimFrames = 0, g_aimBeyond = 0; // frames with the aim pose / with the point further round than the chest limit
float g_aimChestDeg = 0.0f;                // the chest turn used in the last such frame
// test 46: one detail line per gun-mode aim (the chest turn read 0 deg in every log line)
char g_aimInfo[900] = {};
bool g_aimInfoReady = false;
bool g_aimInfoWanted = true;
unsigned g_frames = 0;
unsigned g_skipped = 0; // samples skipped as half-updated
float g_segRef[7] = {};  // usual bone lengths of the skater
int g_tornRun = 0;
M43 g_lock[kMaxNodes];   // 5-3h: the calm pose of every bone (local matrices)
bool g_haveLock = false;
int g_lockWait = 0;
unsigned g_lastFrameCounter = 0xFFFFFFFFu;
char g_info[600] = {};
bool g_infoReady = false, g_infoLogged = false;

void Recompute(const M43* local, const int* parent, int n, const M43& root, M43* world)
{
    for (int i = 0; i < n; ++i) world[i] = parent[i] < 0 ? Mul(local[i], root) : Mul(local[i], world[parent[i]]);
}

// 8.29 B7: bend a two-joint limb (A = shoulder / hip, B = elbow / knee,
// C = wrist / ankle, E = hand / foot) so E lands on `target`, by `w` (0..1).
// Solved for E itself (elbow-to-hand counts as the forearm), so the hand /
// foot keeps its turn relative to the forearm / shin. The elbow / knee keeps
// bending to the side it already bends to. Returns false if left alone.
bool Reach(M43* local, M43* world, const int* parent, int n, const M43& root, int iA, int iB, int iC, int iE,
           V3 target, float w)
{
    if (iA < 0 || iB < 0 || iC < 0 || !(w > 0.001f)) return false;
    if (iE < 0) iE = iC;
    Recompute(local, parent, n, root, world);
    const V3 A = world[iA].p, B = world[iB].p, E = world[iE].p;
    const V3 goal = E + (target - E) * (w > 1.0f ? 1.0f : w);
    const float l1 = Len(B - A), l2 = Len(E - B);
    if (!(l1 > 1e-4f) || !(l2 > 1e-4f)) return false;
    V3 dir = goal - A;
    float d = Len(dir);
    if (!Unit(dir)) return false;
    const float dMin = std::fabs(l1 - l2) + 1e-3f, dMax = (l1 + l2) * 0.999f;
    if (d < dMin) d = dMin;
    if (d > dMax) d = dMax;
    V3 bend = (B - A) - dir * Dot(B - A, dir);
    if (!Unit(bend)) { // straight now: bend the way the joint's "up" points
        bend = world[iB].r[1] - dir * Dot(world[iB].r[1], dir);
        if (!Unit(bend)) return false;
    }
    const float along = (l1 * l1 + d * d - l2 * l2) / (2.0f * d);
    const float h2 = l1 * l1 - along * along;
    const V3 newB = A + dir * along + bend * (h2 > 0.0f ? std::sqrt(h2) : 0.0f);
    // upper part: A turned so B goes to newB
    {
        V3 from = B - A, to = newB - A;
        if (!Unit(from) || !Unit(to)) return false;
        const M43 q = FromTo(from, to);
        M43 wa = world[iA];
        for (int k = 0; k < 3; ++k) wa.r[k] = RotateRow(wa.r[k], q);
        local[iA] = Mul(wa, InverseRigid(parent[iA] < 0 ? root : world[parent[iA]]));
        Recompute(local, parent, n, root, world);
    }
    // lower part: B turned so E goes to the goal
    {
        const V3 b2 = world[iB].p;
        V3 from = world[iE].p - b2, to = goal - b2;
        if (!Unit(from) || !Unit(to)) return false;
        const M43 q = FromTo(from, to);
        M43 wb = world[iB];
        for (int k = 0; k < 3; ++k) wb.r[k] = RotateRow(wb.r[k], q);
        local[iB] = Mul(wb, InverseRigid(parent[iB] < 0 ? root : world[parent[iB]]));
        Recompute(local, parent, n, root, world);
    }
    return true;
}

void Apply(CPed* ped)
{
    if (!g_copyBody || !g_target.valid) return;
    if (GetTickCount64() - g_target.tick > kStaleMs) return;
    if (ped != FindPlayerPed()) return;
    if (CTimer::m_FrameCounter == g_lastFrameCounter) return; // once per frame
    if (!ped->m_pRwClump || !ped->m_matrix) return;
    RpHAnimHierarchy* h = GetAnimHierarchyFromSkinClump(ped->m_pRwClump);
    if (!h || !h->pMatrixArray || !h->pNodeInfo) return;
    const int n = h->numNodes;
    if (n <= 0 || n > kMaxNodes) return;
    g_lastFrameCounter = CTimer::m_FrameCounter;

    // Parents from RenderWare's push/pop flags.
    int parent[kMaxNodes], stack[kMaxNodes], sp = 0, cur = -1;
    for (int i = 0; i < n; ++i) {
        const RwInt32 f = h->pNodeInfo[i].flags;
        if (f & rpHANIMPUSHPARENTMATRIX) stack[sp++] = cur;
        parent[i] = cur;
        if (f & rpHANIMPOPPARENTMATRIX) cur = sp > 0 ? stack[--sp] : -1;
        else cur = i;
    }

    // Are the matrices in world space, or relative to CJ (his matrix)?
    const CMatrixLink& pm = *ped->m_matrix;
    M43 pedM;
    pedM.r[0] = V3(pm.right.x, pm.right.y, pm.right.z);
    pedM.r[1] = V3(pm.up.x, pm.up.y, pm.up.z);
    pedM.r[2] = V3(pm.at.x, pm.at.y, pm.at.z);
    pedM.p = V3(pm.pos.x, pm.pos.y, pm.pos.z);
    M43 world[kMaxNodes], local[kMaxNodes];
    for (int i = 0; i < n; ++i) world[i] = FromRw(h->pMatrixArray[i]);
    const bool worldSpace = Len(world[0].p - pedM.p) < 3.0f;
    M43 root;  // the space the first bone hangs from: identity (world or CJ space)
    root.r[0] = V3(1, 0, 0); root.r[1] = V3(0, 1, 0); root.r[2] = V3(0, 0, 1); root.p = V3(0, 0, 0);
    for (int i = 0; i < n; ++i)
        local[i] = parent[i] < 0 ? world[i] : Mul(world[i], InverseRigid(world[parent[i]]));

    // Fix 3: calm head. First frame: remember neck / head / face; then every
    // frame put them back (GTA's look-at and talking change them each frame).
    // 5-3h: not just the head: EVERY bone (except the root, which carries CJ's
    // place in the world) starts from the calm pose each frame, and only then
    // is turned to match the skater. GTA's skating animation underneath had
    // still been rolling the arms around their own length (the shoulder
    // "shimmy", user 11:00: still there after locking the collar bones), and
    // swaying the lower spine.
    for (int li = 0; li < n; ++li) {
        if (parent[li] < 0) continue;
        if (g_haveLock) local[li] = g_lock[li];
        else if (g_lockWait >= kLockAfterFrames) g_lock[li] = local[li];
    }
    if (!g_haveLock && ++g_lockWait > kLockAfterFrames) g_haveLock = true;

    // Targets in the matrices' space.
    V3 t[T_COUNT];
    const M43 toSpace = InverseRigid(pedM);
    for (int b = 0; b < T_COUNT; ++b) t[b] = worldSpace ? g_target.pos[b] : RotateRow(g_target.pos[b], toSpace) + toSpace.p;

    auto idx = [h](int id) { return RpHAnimIDGetIndex(h, id); };

    if (!g_infoReady) {
        int len = snprintf(g_info, sizeof(g_info), "CJ skeleton %d bones, matrices in %s space; bones (id:parent id):",
                           n, worldSpace ? "world" : "CJ's own");
        for (int i = 0; i < n && len < static_cast<int>(sizeof(g_info)) - 16; ++i)
            len += snprintf(g_info + len, sizeof(g_info) - len, " %d:%d", h->pNodeInfo[i].nodeID,
                            parent[i] >= 0 ? h->pNodeInfo[parent[i]].nodeID : -1);
        g_infoReady = true;
    }

    for (const Op& op : kOps) {
        const int i = idx(op.bone);
        if (i < 0) continue;
        Recompute(local, parent, n, root, world);
        M43 w = world[i];
        if (op.kind == MOVE) {
            w.p = t[op.tFrom];
        } else {
            const int e = idx(op.end);
            if (e < 0) continue;
            V3 have = world[e].p - w.p, want = t[op.tTo] - t[op.tFrom];
            if (!Unit(have) || !Unit(want)) continue;
            M43 q = FromTo(have, want);
            if (op.kind == AIM2) {
                const int a = idx(op.sideA), b = idx(op.sideB);
                if (a >= 0 && b >= 0) {
                    V3 s1 = RotateRow(world[b].p - world[a].p, q);
                    V3 s2 = t[op.tSideB] - t[op.tSideA];
                    s1 = s1 - want * Dot(s1, want);
                    s2 = s2 - want * Dot(s2, want);
                    if (Unit(s1) && Unit(s2)) {
                        const float ang = std::atan2(Dot(Cross(s1, s2), want), Dot(s1, s2));
                        q = Mul(q, AxisAngle(want, ang));
                    }
                }
            }
            for (int k = 0; k < 3; ++k) w.r[k] = RotateRow(w.r[k], q);
        }
        const M43 parentWorld = parent[i] < 0 ? root : world[parent[i]];
        local[i] = Mul(w, InverseRigid(parentWorld));
    }
    // 8.29 B7: hands onto the bike's grips, feet onto its pedals, as much as
    // reTHAWed's rider has his on them. Which grip / pedal for which hand /
    // foot: the one on CJ's side (so the level's mirror doesn't matter).
    if (g_limbs.valid && GetTickCount64() - g_limbs.tick <= kStaleMs) {
        Recompute(local, parent, n, root, world);
        V3 lt[4];
        for (int k = 0; k < 4; ++k) lt[k] = worldSpace ? g_limbs.pos[k] : RotateRow(g_limbs.pos[k], toSpace) + toSpace.p;
        struct Limb { int a, b, c, e; };
        const Limb rightArm{idx(G_R_SHOULDER), idx(G_R_ELBOW), idx(G_R_WRIST), idx(G_R_HAND)};
        const Limb leftArm{idx(G_L_SHOULDER), idx(G_L_ELBOW), idx(G_L_WRIST), idx(G_L_HAND)};
        const Limb rightLeg{idx(G_R_HIP), idx(G_R_KNEE), idx(G_R_ANKLE), idx(G_R_FOOT)};
        const Limb leftLeg{idx(G_L_HIP), idx(G_L_KNEE), idx(G_L_ANKLE), idx(G_L_FOOT)};
        for (int pair = 0; pair < 2; ++pair) { // 0: hands / grips 0, 1; 1: feet / pedals 2, 3
            const Limb& r = pair == 0 ? rightArm : rightLeg;
            const Limb& l = pair == 0 ? leftArm : leftLeg;
            if (r.a < 0 || l.a < 0) continue;
            const V3 cjRight = world[r.a].p - world[l.a].p;
            const int t0 = pair * 2, t1 = pair * 2 + 1;
            const bool straight = Dot(lt[t0] - lt[t1], cjRight) >= 0.0f; // target 0 on CJ's right
            const int forRight = straight ? t0 : t1, forLeft = straight ? t1 : t0;
            if (Reach(local, world, parent, n, root, r.a, r.b, r.c, r.e, lt[forRight], g_limbs.weight[forRight])) ++g_reached;
            if (Reach(local, world, parent, n, root, l.a, l.b, l.c, l.e, lt[forLeft], g_limbs.weight[forLeft])) ++g_reached;
        }
    }
    // 8.53 step 3: gun mode's aim pose (only while aiming; eased in and out by the caller's weight)
    if (g_aim.valid && GetTickCount64() - g_aim.tick <= kStaleMs && g_aim.weight > 0.001f) {
        Recompute(local, parent, n, root, world);
        const V3 aimPt = worldSpace ? g_aim.point : RotateRow(g_aim.point, toSpace) + toSpace.p;
        const float wgt = g_aim.weight > 1.0f ? 1.0f : g_aim.weight;
        const V3 up(0.0f, 0.0f, 1.0f); // z is up in GTA's world and in CJ's own space alike
        // (3) chest: the upper spine turns about the vertical towards the point, at most kAimChestMaxDeg
        // test 48: the chest's sideways line runs between the shoulders (22, 32): the two clavicle bones sit
        // on the same spot (test 47 log: "clavicle line 0.00 0.00 0.00 (too short)"), so the chest never turned
        const int sp = idx(G_SPINE1), rc = idx(G_R_SHOULDER), lc = idx(G_L_SHOULDER);
        int infoLen = 0;
        const bool wantInfo = g_aimInfoWanted && !g_aimInfoReady;
        if (wantInfo)
            infoLen = snprintf(g_aimInfo, sizeof(g_aimInfo), "aim pose detail: matrices in %s space; spine %d, shoulders R %d L %d; aim point "
                               "%.2f %.2f %.2f", worldSpace ? "world" : "CJ's own", sp, rc, lc, aimPt.x, aimPt.y, aimPt.z);
        if (sp >= 0 && rc >= 0 && lc >= 0) {
            V3 right = world[rc].p - world[lc].p;
            right = right - up * Dot(right, up);
            V3 want = aimPt - world[sp].p;
            want = want - up * Dot(want, up);
            const bool okR = Unit(right), okW = Unit(want);
            if (wantInfo && infoLen > 0 && infoLen < static_cast<int>(sizeof(g_aimInfo)))
                infoLen += snprintf(g_aimInfo + infoLen, sizeof(g_aimInfo) - infoLen,
                                    " | spine at %.2f %.2f %.2f; shoulder line %.2f %.2f %.2f (%s); to the point %.2f %.2f %.2f (%s)",
                                    world[sp].p.x, world[sp].p.y, world[sp].p.z, right.x, right.y, right.z, okR ? "ok" : "too short",
                                    want.x, want.y, want.z, okW ? "ok" : "too short");
            if (okR && okW) {
                const V3 fwd = Cross(up, right);
                float ang = std::atan2(Dot(Cross(fwd, want), up), Dot(fwd, want));
                if (wantInfo && infoLen > 0 && infoLen < static_cast<int>(sizeof(g_aimInfo)))
                    infoLen += snprintf(g_aimInfo + infoLen, sizeof(g_aimInfo) - infoLen, " | chest faces %.2f %.2f %.2f, turn %.0f deg",
                                        fwd.x, fwd.y, fwd.z, ang * 57.2958f);
                const float lim = kAimChestMaxDeg * 3.14159265f / 180.0f;
                if (ang > lim) { ang = lim; ++g_aimBeyond; }
                if (ang < -lim) { ang = -lim; ++g_aimBeyond; }
                g_aimChestDeg = ang * 57.2958f;
                // test 49 ("cj's body still kinda cork screws"): the whole turn sat in one spine bone, a sharp
                // twist at mid-back. It is now shared: the bone below spine1 (the lower spine, when it is not the
                // pelvis) takes kAimLowerShare, spine1 the rest, so the twist spreads over the back.
                const int low = parent[sp];
                const bool share = low >= 0 && low != idx(G_PELVIS1);
                const float lowAng = share ? ang * kAimLowerShare : 0.0f;
                if (share) {
                    const M43 ql = AxisAngle(up, lowAng * wgt);
                    M43 wl = world[low];
                    for (int k = 0; k < 3; ++k) wl.r[k] = RotateRow(wl.r[k], ql);
                    local[low] = Mul(wl, InverseRigid(parent[low] < 0 ? root : world[parent[low]]));
                    Recompute(local, parent, n, root, world);
                }
                const M43 q = AxisAngle(up, (ang - lowAng) * wgt);
                M43 w = world[sp];
                for (int k = 0; k < 3; ++k) w.r[k] = RotateRow(w.r[k], q);
                local[sp] = Mul(w, InverseRigid(parent[sp] < 0 ? root : world[parent[sp]]));
                Recompute(local, parent, n, root, world);
                if (wantInfo && infoLen > 0 && infoLen < static_cast<int>(sizeof(g_aimInfo)))
                    infoLen += snprintf(g_aimInfo + infoLen, sizeof(g_aimInfo) - infoLen, " | lower spine bone %d %s", low,
                                        share ? "shares the turn" : "is the pelvis or missing: spine1 alone");
            }
        }
        // (4) right arm stretched at the point: the forearm, and the gun with it, points there
        const int ra = idx(G_R_SHOULDER), rb = idx(G_R_ELBOW), rcw = idx(G_R_WRIST), re = idx(G_R_HAND);
        V3 handGoal;
        bool haveHand = false;
        if (ra >= 0 && rb >= 0 && rcw >= 0) {
            const int e = re >= 0 ? re : rcw;
            V3 dir = aimPt - world[ra].p;
            const float armLen = Len(world[rb].p - world[ra].p) + Len(world[e].p - world[rb].p);
            bool okDir = Unit(dir);
            // test 48: keep the arm in front of the (now turned) chest: at most kAimArmMaxDeg round from its
            // front and kAimArmPitchDeg up / down, so a point behind CJ never pulls the arm behind his back
            if (okDir && rc >= 0 && lc >= 0) {
                V3 side = world[rc].p - world[lc].p;
                side = side - up * Dot(side, up);
                V3 flat = dir - up * Dot(dir, up);
                if (Unit(side) && Unit(flat)) {
                    const V3 front = Cross(up, side);
                    const float yaw = std::atan2(Dot(Cross(front, flat), up), Dot(front, flat));
                    float pitch = std::asin(Dot(dir, up) > 1.0f ? 1.0f : Dot(dir, up) < -1.0f ? -1.0f : Dot(dir, up));
                    const float yawLim = kAimArmMaxDeg * 3.14159265f / 180.0f, pitchLim = kAimArmPitchDeg * 3.14159265f / 180.0f;
                    float yawUse = yaw;
                    if (yawUse > yawLim) yawUse = yawLim;
                    if (yawUse < -yawLim) yawUse = -yawLim;
                    if (pitch > pitchLim) pitch = pitchLim;
                    if (pitch < -pitchLim) pitch = -pitchLim;
                    if (yawUse != yaw || std::fabs(pitch - std::asin(Dot(dir, up) > 1.0f ? 1.0f : Dot(dir, up) < -1.0f ? -1.0f : Dot(dir, up))) > 1e-4f) {
                        ++g_aimArmClamped;
                        const V3 h = RotateRow(front, AxisAngle(up, yawUse));
                        dir = h * std::cos(pitch) + up * std::sin(pitch);
                        okDir = Unit(dir);
                    }
                }
            }
            if (okDir && armLen > 0.1f) {
                handGoal = world[ra].p + dir * (armLen * 0.97f);
                haveHand = Reach(local, world, parent, n, root, ra, rb, rcw, re, handGoal, wgt);
                if (haveHand) {
                    ++g_reached;
                    // test 46 (video: the gun pointed up, lay on its side or tilted while the arm aimed): the hand keeps
                    // the skater's wrist turn, so the gun did not follow the forearm. The hand is now turned so the
                    // gun's barrel (its model's X axis: GTA's muzzle offsets are along +X) points at the point and
                    // the gun stays upright (its Z axis as close to straight up as the aim allows), eased by the weight.
                    V3 gunUp = up;
                    // test 49 ("the gun is sideways"): GTA draws the gun on bone 24 (its "hand"; plugin-sdk calls it
                    // the wrist) and its muzzle offsets are in that bone's axes; 25 is the fingers. Test 47/48 turned
                    // 25, so the gun kept the skater's wrist roll. Now bone 24 is turned (the fingers follow it).
                    const int gb = rcw;
                    if (gb >= 0) {
                        Recompute(local, parent, n, root, world);
                        // test 48: along the arm's (possibly clamped) direction when the point is out of reach round
                        // the side, else at the point itself
                        V3 dHand = aimPt - world[gb].p;
                        if (Unit(dHand) && Dot(dHand, dir) < 0.94f) dHand = dir;
                        if (Unit(dHand)) {
                            V3 z = up - dHand * Dot(up, dHand);
                            if (Unit(z)) {
                                gunUp = z;
                                M43 want = world[gb];
                                if (wantInfo && infoLen > 0 && infoLen < static_cast<int>(sizeof(g_aimInfo)))
                                    infoLen += snprintf(g_aimInfo + infoLen, sizeof(g_aimInfo) - infoLen,
                                                        " | hand rows before: x %.2f %.2f %.2f, y %.2f %.2f %.2f, z %.2f %.2f %.2f; aim %.2f %.2f %.2f",
                                                        want.r[0].x, want.r[0].y, want.r[0].z, want.r[1].x, want.r[1].y, want.r[1].z,
                                                        want.r[2].x, want.r[2].y, want.r[2].z, dHand.x, dHand.y, dHand.z);
                                M43 target = want;
                                target.r[0] = dHand;
                                target.r[2] = z;
                                target.r[1] = Cross(z, dHand);
                                // ease: turn from the current hand towards the target by the weight
                                V3 from = want.r[0];
                                if (Unit(from)) {
                                    M43 q1 = FromTo(from, dHand);
                                    M43 eased = want;
                                    for (int k = 0; k < 3; ++k) eased.r[k] = RotateRow(want.r[k], q1);
                                    // then roll about the barrel so Z goes up
                                    V3 zNow = eased.r[2] - dHand * Dot(eased.r[2], dHand);
                                    if (Unit(zNow)) {
                                        const float roll = std::atan2(Dot(Cross(zNow, z), dHand), Dot(zNow, z));
                                        const M43 q2 = AxisAngle(dHand, roll);
                                        for (int k = 0; k < 3; ++k) eased.r[k] = RotateRow(eased.r[k], q2);
                                    }
                                    if (wgt < 0.999f) {
                                        // partial: only part of the barrel turn (keeps the roll fix small too)
                                        V3 mid = want.r[0] * (1.0f - wgt) + dHand * wgt;
                                        if (Unit(mid)) {
                                            M43 qp = FromTo(from, mid);
                                            for (int k = 0; k < 3; ++k) eased.r[k] = RotateRow(want.r[k], qp);
                                        }
                                    }
                                    const int hp = parent[gb];
                                    local[gb] = Mul(eased, InverseRigid(hp < 0 ? root : world[hp]));
                                    Recompute(local, parent, n, root, world);
                                    if (wantInfo && infoLen > 0 && infoLen < static_cast<int>(sizeof(g_aimInfo))) {
                                        V3 gx = world[gb].r[0], gz = world[gb].r[2];
                                        Unit(gx);
                                        Unit(gz);
                                        infoLen += snprintf(g_aimInfo + infoLen, sizeof(g_aimInfo) - infoLen,
                                                            " | gun bone %d after: barrel . aim %.2f, top . up %.2f (1 = right on)", gb,
                                                            Dot(gx, dHand), Dot(gz, z));
                                    }
                                }
                                (void)target;
                            }
                        }
                    }
                    // (5) left hand under the barrel for two-handed guns
                    if (g_aim.twoHanded) {
                        const int la = idx(G_L_SHOULDER), lb = idx(G_L_ELBOW), lcw = idx(G_L_WRIST), le = idx(G_L_HAND);
                        const V3 handNow = rcw >= 0 ? world[rcw].p : handGoal;
                        const V3 grip = handNow + dir * 0.18f - gunUp * 0.04f;
                        if (Reach(local, world, parent, n, root, la, lb, lcw, le, grip, wgt)) ++g_reached;
                    }
                }
            }
        }
        ++g_aimFrames;
        if (wantInfo) g_aimInfoReady = true;
    }
    Recompute(local, parent, n, root, world);
    for (int i = 0; i < n; ++i)
        if (!Finite(world[i])) return; // something went wrong: leave GTA's pose
    for (int i = 0; i < n; ++i) ToRw(world[i], h->pMatrixArray[i]);
    ++g_frames;
    if (RecorderOn()) {
        RecSet(REC_DRAW_MS, RecNowMs());
        RecSet(REC_DRAW_FRAME, static_cast<float>(CTimer::m_FrameCounter));
        const int hip = idx(G_PELVIS1);
        if (hip >= 0) {
            // as drawn, in world space (CJ's own space turned back into world if needed)
            V3 w = world[hip].p;
            if (!worldSpace) w = RotateRow(w, pedM) + pedM.p;
            RecSet3(REC_DRAW_HIPS_X, w.x, w.y, w.z);
        }
    }
}

void ApplySafe(CPed* ped)
{
    __try {
        Apply(ped);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_target.valid = false;
    }
}

} // namespace

void BodyAddHook()
{
    // Right before GTA draws a ped (after its animation for this frame).
    plugin::Events::pedRenderEvent.Add([](CPed* ped) { ApplySafe(ped); });
}

void BodySetTarget(const float* body, const float* orient, float scale, float ySign, float feetX, float feetY,
                   float feetZ)
{
    // A skeleton's bones don't change length. If some do in this sample, it
    // was read while reTHAWed was half-way through updating them: skip it and
    // keep the last good pose.
    {
        static const int seg[][2] = {{T_THIGH_L, T_KNEE_L}, {T_KNEE_L, T_ANKLE_L}, {T_THIGH_R, T_KNEE_R},
                                     {T_KNEE_R, T_ANKLE_R}, {T_BICEP_L, T_FOREARM_L}, {T_BICEP_R, T_FOREARM_R},
                                     {T_PELVIS, T_NECK}};
        bool torn = false;
        float* ref = g_segRef;
        for (int k = 0; k < 7; ++k) {
            const float* a2 = body + seg[k][0] * 3;
            const float* b2 = body + seg[k][1] * 3;
            const float len = Len(V3(a2[0] - b2[0], a2[1] - b2[1], a2[2] - b2[2]));
            if (ref[k] <= 0.0f) ref[k] = len;
            else if (k < 6 && std::fabs(len - ref[k]) > 0.15f * ref[k]) torn = true;
        }
        if (torn && ++g_tornRun < 30) {
            ++g_skipped;
            return;
        }
        // good sample (or 30 odd ones in a row: the skeleton really changed): learn the lengths
        for (int k = 0; k < 7; ++k) {
            const float* a2 = body + seg[k][0] * 3;
            const float* b2 = body + seg[k][1] * 3;
            const float len = Len(V3(a2[0] - b2[0], a2[1] - b2[1], a2[2] - b2[2]));
            ref[k] = g_tornRun >= 30 ? len : ref[k] * 0.9f + len * 0.1f;
        }
        g_tornRun = 0;
    }
    // THAW direction -> GTA direction: (x, z * ySign, y) (convert.h's ThawDeltaToGta)
    auto map = [ySign](V3 v) { return V3(v.x, v.z * ySign, v.y); };
    const V3 R = map(V3(orient[0], orient[1], orient[2]));
    const V3 U = map(V3(orient[3], orient[4], orient[5]));
    const V3 A = map(V3(orient[6], orient[7], orient[8]));
    // The skater's left bones are on +x of its own space. If that lands on CJ's
    // right (CJ's right = facing x up), drive CJ's right bones with them.
    const bool swap = Dot(R, Cross(A, U)) > 0.0f;
    const V3 feet(feetX, feetY, feetZ);
    for (int b = 0; b < T_COUNT; ++b) {
        int src = b;
        if (swap) {
            if (b >= T_BICEP_L && b <= T_WRIST_L) src = b + 3;
            else if (b >= T_BICEP_R && b <= T_WRIST_R) src = b - 3;
            else if (b >= T_THIGH_L && b <= T_TOE_L) src = b + 4;
            else if (b >= T_THIGH_R && b <= T_TOE_R) src = b - 4;
            else if (b == T_CHEST) src = T_CHEST_SIDE;   // the chest axis flips too
            else if (b == T_CHEST_SIDE) src = T_CHEST;
        }
        const float* p = body + src * 3;
        g_target.pos[b] = feet + (R * p[0] + U * p[1] + A * p[2]) * scale;
    }
    g_target.tick = GetTickCount64();
    g_target.valid = true;
    if (RecorderOn()) {
        const V3& h = g_target.pos[T_PELVIS];
        RecSet3(REC_HIPS_X, h.x, h.y, h.z);
        V3 c = g_target.pos[T_CHEST] - g_target.pos[T_CHEST_SIDE];
        Unit(c);
        RecSet3(REC_CHEST_X, c.x, c.y, c.z);
        const V3 a = g_target.pos[T_FOREARM_R] - g_target.pos[T_BICEP_R];
        RecSet3(REC_ARM_X, a.x, a.y, a.z);
    }
}

void BodySetLimbTargets(const float (*pos)[3], const float* weight)
{
    for (int k = 0; k < 4; ++k) {
        g_limbs.pos[k] = V3(pos[k][0], pos[k][1], pos[k][2]);
        g_limbs.weight[k] = weight[k];
    }
    g_limbs.tick = GetTickCount64();
    g_limbs.valid = true;
}

void BodyClearLimbTargets() { g_limbs.valid = false; }

void BodySetAim(const float* point, float weight, bool twoHanded)
{
    g_aim.point = V3(point[0], point[1], point[2]);
    g_aim.weight = weight;
    g_aim.twoHanded = twoHanded;
    g_aim.tick = GetTickCount64();
    g_aim.valid = true;
}

void BodyClearAim()
{
    if (g_aim.valid) g_aimInfoWanted = true; // the next aim logs its detail line again
    g_aim.valid = false;
}

const char* BodyTakeAimInfo()
{
    if (!g_aimInfoReady) return nullptr;
    g_aimInfoReady = false;
    g_aimInfoWanted = false;
    return g_aimInfo;
}

unsigned BodyAimFramesAndReset(unsigned* beyond, float* chestDeg, unsigned* armClamped)
{
    const unsigned f = g_aimFrames;
    if (armClamped) *armClamped = g_aimArmClamped;
    g_aimArmClamped = 0;
    if (beyond) *beyond = g_aimBeyond;
    if (chestDeg) *chestDeg = g_aimChestDeg;
    g_aimFrames = 0;
    g_aimBeyond = 0;
    return f;
}

unsigned BodyReachedAndReset()
{
    const unsigned r = g_reached;
    g_reached = 0;
    return r;
}

void BodyClear()
{
    g_target.valid = false;
    g_limbs.valid = false;
    g_aim.valid = false;
    for (float& r : g_segRef) r = 0.0f; // next skater may be built differently
    g_tornRun = 0;
    g_haveLock = false; // fix 3: take a fresh calm head pose next time
    g_lockWait = 0;
}

bool BodyToggle()
{
    g_copyBody = !g_copyBody;
    return g_copyBody;
}

unsigned BodyFramesAndReset(unsigned* skipped)
{
    const unsigned f = g_frames;
    g_frames = 0;
    if (skipped) *skipped = g_skipped;
    g_skipped = 0;
    return f;
}

const char* BodyTakeInfo()
{
    if (!g_infoReady || g_infoLogged) return nullptr;
    g_infoLogged = true;
    return g_info;
}

} // namespace mp::gta
