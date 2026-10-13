// SkateAndreas: skater facing and skeleton reader. See pose.h. READ ONLY.

#include "pose.h"

#include <cmath>
#include <cstring>

namespace mp::thaw {
namespace {

constexpr unsigned long kOrientOffset = 0x88;
constexpr unsigned long kCompArray = 0x60, kCompCount = 0x64, kCompList = 0x11C;
constexpr unsigned long kCompListNext = 0x10, kCompListChecksum = 0x04;
constexpr unsigned long kSkeletonComponent = 0x222756D5; // "skeleton"
constexpr unsigned long kCompSkeleton = 0x18;
constexpr unsigned long kSkelMatrices = 0x04, kSkelBind = 0x08, kSkelCount = 0x14, kSkelBones = 0x18;
constexpr unsigned long kBoneInfoSize = 0x1C;
constexpr int kMaxBones = 256;

const unsigned long* BodyChecksums(); // below: checksums of kBodyBones


struct Check {
    unsigned long address;
    unsigned char bytes[16];
    unsigned int length;
    const char* what;
};
const Check kChecks[] = {
    {0x0046AA2E, {0x8B, 0x4F, 0x0C, 0x81, 0xC1, 0x88, 0x00, 0x00, 0x00}, 9,
     "bone-to-world: object orientation at +0x88"},
    {0x0046AA41, {0x8B, 0x57, 0x0C, 0x83, 0xC2, 0x68}, 6, "bone-to-world: object position at +0x68"},
    {0x0042E8C1, {0x8B, 0x59, 0x60, 0x85, 0xDB}, 5, "component lookup: array at +0x60"},
    {0x0042E8C9, {0x8B, 0x71, 0x64}, 3, "component lookup: count at +0x64"},
    {0x004EFF22, {0x8B, 0x4F, 0x14}, 3, "skeleton: bone count at +0x14"},
    {0x004EFF2B, {0x8B, 0x77, 0x18, 0x8B, 0xD6, 0x3B, 0x1A}, 7, "skeleton: bone list at +0x18"},
    {0x004EFF35, {0x83, 0xC2, 0x1C}, 3, "skeleton: 0x1C bytes per bone"},
    {0x004EFF73, {0x8B, 0x47, 0x04, 0xC1, 0xE1, 0x06}, 6, "skeleton: bone matrices at +0x04, 64 bytes each"},
    {0x004EFF87, {0x8B, 0x4F, 0x08, 0x8B, 0x51, 0x04, 0x8B, 0x42, 0x28}, 9,
     "skeleton: bind matrices at [[+0x08]+0x04]+0x28"},
};

// ---- 8.26 body flips: the model component's display rotation ----
constexpr unsigned long kModelComponent = 0x286A8D26;       // "model"
const unsigned long kDispQueues[3] = {0x38, 0xCC, 0x160};  // x, y, z
constexpr unsigned long kQueueActive = 0x90;               // byte: anything active
constexpr int kQueueSlots = 6;
constexpr unsigned long kSlotSize = 0x18;
constexpr unsigned long kDispPivot = 0x28, kDispFlipped = 0x1F4;
constexpr unsigned long kClockTicks = 0x008AAD00, kClockFreq = 0x007346B4;
constexpr unsigned long kGameBase = 0x00742C14, kGameScale = 0x00719EA0, kGameAdd = 0x00742C10;
bool g_dispOk = false;

const Check kDispChecks[] = {
    {0x004549F0, {0x8A, 0x87, 0xC8, 0x00, 0x00, 0x00}, 6, "display matrix: x queue active byte at +0xC8"},
    {0x00454A01, {0x8D, 0x4F, 0x38, 0xE8}, 4, "display matrix: x queue at +0x38"},
    {0x00454AC4, {0x8A, 0x87, 0x5C, 0x01, 0x00, 0x00}, 6, "display matrix: y queue active byte at +0x15C"},
    {0x00454B9C, {0x8A, 0x87, 0xF0, 0x01, 0x00, 0x00}, 6, "display matrix: z queue active byte at +0x1F0"},
    {0x00454C86, {0xF3, 0x0F, 0x10, 0x67, 0x28}, 5, "display matrix: pivot at +0x28"},
    {0x00454980, {0x8A, 0x87, 0xF4, 0x01, 0x00, 0x00}, 6, "display matrix: flipped byte at +0x1F4"},
    {0x004543DA, {0xC6, 0x87, 0x90, 0x00, 0x00, 0x00, 0x00}, 7, "queue sum: active byte at +0x90"},
    {0x004543E3, {0xBD, 0x06, 0x00, 0x00, 0x00}, 5, "queue sum: 6 slots"},
    {0x0045440A, {0x83, 0xC6, 0x18}, 3, "queue sum: 0x18 bytes per slot"},
    {0x004534A9, {0x8A, 0x46, 0x14, 0xA8, 0x01}, 5, "slot angle: flags at +0x14"},
    {0x004534C3, {0x2B, 0x46, 0x04}, 3, "slot angle: start time at +0x04"},
    {0x004534D5, {0x8B, 0x0D, 0x10, 0x2C, 0x74, 0x00}, 6, "slot angle: game clock offset 0x742C10"},
    {0x0062C357, {0xA1, 0x00, 0xAD, 0x8A, 0x00}, 5, "real clock: ticks at 0x8AAD00"},
    {0x0062C35E, {0xD8, 0x35, 0xB4, 0x46, 0x73, 0x00}, 6, "real clock: frequency at 0x7346B4"},
    {0x00436182, {0x8B, 0x35, 0x14, 0x2C, 0x74, 0x00}, 6, "game clock: base at 0x742C14"},
    {0x004361A1, {0xD8, 0x0D, 0xA0, 0x9E, 0x71, 0x00}, 6, "game clock: speed at 0x719EA0"},
};

template <typename T>
T Read(unsigned long addr)
{
    return *reinterpret_cast<const volatile T*>(addr);
}

void ReadMatrix(unsigned long addr, float* m)
{
    for (int i = 0; i < 16; ++i) m[i] = Read<float>(addr + i * 4);
}

// Inverse of a matrix whose top-left 3x3 is a rotation (rows, row vectors).
void InverseRigid(const float* a, float* out)
{
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out[r * 4 + c] = a[c * 4 + r];
    out[3] = out[7] = out[11] = 0.0f;
    for (int c = 0; c < 3; ++c)
        out[12 + c] = -(a[12] * out[c] + a[13] * out[4 + c] + a[14] * out[8 + c]);
    out[15] = 1.0f;
}

void Multiply(const float* a, const float* b, float* out) // out = a x b
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = s;
        }
}

unsigned long FindComponent(unsigned long obj, unsigned long checksum)
{
    const unsigned long arr = Read<unsigned long>(obj + kCompArray);
    if (arr) {
        const int count = Read<int>(obj + kCompCount);
        for (int i = 0; i < count && i < 128; ++i)
            if (Read<unsigned long>(arr + i * 8) == checksum) return Read<unsigned long>(arr + i * 8 + 4);
        return 0;
    }
    unsigned long node = Read<unsigned long>(obj + kCompList);
    for (int i = 0; node && i < 128; ++i) {
        if (Read<unsigned long>(node + kCompListChecksum) == checksum) return node;
        node = Read<unsigned long>(node + kCompListNext);
    }
    return 0;
}

unsigned long FindSkeleton(unsigned long skater, int& count)
{
    count = 0;
    const unsigned long comp = FindComponent(skater, kSkeletonComponent);
    if (!comp) return 0;
    const unsigned long skel = Read<unsigned long>(comp + kCompSkeleton);
    if (!skel) return 0;
    const int n = Read<int>(skel + kSkelCount);
    if (n <= 0 || n > kMaxBones) return 0;
    if (!Read<unsigned long>(skel + kSkelMatrices) || !Read<unsigned long>(skel + kSkelBones)) return 0;
    count = n;
    return skel;
}

void BoneAt(unsigned long skel, int i, BoneMatrix& out)
{
    out.name = Read<unsigned long>(Read<unsigned long>(skel + kSkelBones) + i * kBoneInfoSize);
    float cur[16], bindInv[16], bind[16];
    ReadMatrix(Read<unsigned long>(skel + kSkelMatrices) + i * 64, cur);
    const unsigned long bindArr = Read<unsigned long>(Read<unsigned long>(Read<unsigned long>(skel + kSkelBind) + 4) + 0x28);
    ReadMatrix(bindArr + i * 64, bindInv);
    InverseRigid(bindInv, bind);
    Multiply(bind, cur, out.m);
}

int IndexOf(unsigned long skel, int count, unsigned long name)
{
    const unsigned long bones = Read<unsigned long>(skel + kSkelBones);
    for (int i = 0; i < count; ++i)
        if (Read<unsigned long>(bones + i * kBoneInfoSize) == name) return i;
    return -1;
}

// Angle of one queued rotation (as 0x004534A0 computes it), degrees.
float SlotAngle(unsigned long slot, unsigned long realMs, unsigned long gameMs)
{
    const unsigned char flags = Read<unsigned char>(slot + 0x14);
    if (!(flags & 1)) return 0.0f;
    const unsigned long start = Read<unsigned long>(slot + 0x04);
    const unsigned long elapsedTicks = (flags & 4) ? realMs - start : gameMs - start;
    const double elapsed = static_cast<double>(elapsedTicks); // unsigned, as the game's fild + 2^32
    const float duration = Read<float>(slot + 0x00);
    const float startAngle = Read<float>(slot + 0x08);
    const float total = Read<float>(slot + 0x0C);
    const int mode = Read<int>(slot + 0x10);
    if (elapsed > duration) return (flags & 2) ? startAngle + total : 0.0f;
    if (duration <= 0.0f) return startAngle;
    double f;
    if (mode == 0) {
        f = elapsed / duration;
    } else {
        const double x = elapsed / duration * 1.5707963705062866;
        if (mode < 0) {
            f = 1.0 - std::sin(1.5707963705062866 - x);
        } else {
            f = std::sin(x);
            for (int i = 1; i < mode && i < 8; ++i) f *= f;
        }
    }
    return static_cast<float>(startAngle + total * f);
}

void MulRows(const float* a, const float* b, float* out) // 3x3, out = a x b
{
    float t[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) t[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
    memcpy(out, t, sizeof(t));
}

void ReadDisplayRotation(unsigned long skater, DisplayRot& d)
{
    const unsigned long model = FindComponent(skater, kModelComponent);
    if (!model) return;
    // the game's clocks (0x0062C350, 0x00436180)
    const double freq = Read<float>(kClockFreq);
    if (!(freq > 0.0)) return;
    const unsigned long ticks = Read<unsigned long>(kClockTicks);
    const unsigned long realMs = static_cast<unsigned long>(static_cast<long long>(ticks * (1000.0 / freq)));
    const unsigned long sinceBase = realMs - Read<unsigned long>(kGameBase);
    const unsigned long gameMs = static_cast<unsigned long>(
        static_cast<long long>(static_cast<double>(sinceBase) * Read<float>(kGameScale))) + Read<unsigned long>(kGameAdd);

    d.read = true;
    for (int a = 0; a < 3; ++a) {
        const unsigned long q = model + kDispQueues[a];
        float sum = 0.0f;
        if (Read<unsigned char>(q + kQueueActive))
            for (int i = 0; i < kQueueSlots; ++i) sum += SlotAngle(q + i * kSlotSize, realMs, gameMs);
        d.angle[a] = sum;
    }
    for (int c = 0; c < 3; ++c) d.pivot[c] = Read<float>(model + kDispPivot + c * 4);
    d.flipped = Read<unsigned char>(model + kDispFlipped);

    // L = Rx * Ry * Rz (DirectX row-vector rotations, as 0x00406E00/80, 0x00406F00)
    float L[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int a = 0; a < 3; ++a) {
        if (d.angle[a] == 0.0f) continue;
        const float r = d.angle[a] * 0.017453292f, c = std::cos(r), s = std::sin(r);
        float R[9];
        if (a == 0) { const float m[9] = {1, 0, 0, 0, c, s, 0, -s, c}; memcpy(R, m, sizeof(R)); }
        else if (a == 1) { const float m[9] = {c, 0, -s, 0, 1, 0, s, 0, c}; memcpy(R, m, sizeof(R)); }
        else { const float m[9] = {c, s, 0, -s, c, 0, 0, 0, 1}; memcpy(R, m, sizeof(R)); }
        MulRows(L, R, L);
        d.active = true;
    }
    memcpy(d.L, L, sizeof(L));
}

void ReadPoseUnprotected(unsigned long skater, PoseSample* out)
{
    if (g_dispOk) ReadDisplayRotation(skater, out->disp);
    ReadMatrix(skater + kOrientOffset, out->orient);
    int count = 0;
    const unsigned long skel = FindSkeleton(skater, count);
    out->skeleton = skel;
    out->boneCount = count;
    if (!skel) return;
    static const unsigned long kBoard = NameChecksum("Bone_Board_Root");
    static const unsigned long kPelvis = NameChecksum("Bone_Pelvis");
    out->boardIndex = IndexOf(skel, count, kBoard);
    out->pelvisIndex = IndexOf(skel, count, kPelvis);
    if (out->boardIndex >= 0) BoneAt(skel, out->boardIndex, out->board);
    if (out->pelvisIndex >= 0) BoneAt(skel, out->pelvisIndex, out->pelvis);
    // Phase 5-3d: body bones for CJ.
    const unsigned long* sums = BodyChecksums();
    out->haveBody = true;
    for (int b = 0; b < kBodyBoneCount; ++b) {
        const int i = IndexOf(skel, count, sums[b]);
        if (i < 0) { out->haveBody = false; break; }
        BoneMatrix m;
        BoneAt(skel, i, m);
        out->body[b * 3 + 0] = m.m[12];
        out->body[b * 3 + 1] = m.m[13];
        out->body[b * 3 + 2] = m.m[14];
        if (b == kBodyBoneCount - 1) {
            // fix 2: not a joint: a point 10 units along the chest's right
            // axis, so GTA can turn CJ's chest like the skater's chest (the
            // shoulder joints move with the arms and made CJ's chest wobble)
            for (int c = 0; c < 3; ++c) out->body[b * 3 + c] += 10.0f * m.m[c];
        }
    }
}

int ReadAllUnprotected(unsigned long skater, BoneMatrix* out, int max)
{
    int count = 0;
    const unsigned long skel = FindSkeleton(skater, count);
    if (!skel) return 0;
    if (count > max) count = max;
    for (int i = 0; i < count; ++i) BoneAt(skel, i, out[i]);
    return count;
}

// the bone in the skeleton's default (bind) pose: what BoneAt gives when the
// current matrix is the identity
void BindAt(unsigned long skel, int i, BoneMatrix& out)
{
    out.name = Read<unsigned long>(Read<unsigned long>(skel + kSkelBones) + i * kBoneInfoSize);
    float bindInv[16];
    const unsigned long bindArr = Read<unsigned long>(Read<unsigned long>(Read<unsigned long>(skel + kSkelBind) + 4) + 0x28);
    ReadMatrix(bindArr + i * 64, bindInv);
    InverseRigid(bindInv, out.m);
}

int ReadSkeletonBindUnprotected(unsigned long skel, BoneMatrix* out, int max)
{
    const int n = Read<int>(skel + kSkelCount);
    if (n <= 0 || n > kMaxBones) return 0;
    const int count = n > max ? max : n;
    for (int i = 0; i < count; ++i) BindAt(skel, i, out[i]);
    return count;
}

int ReadSkeletonUnprotected(unsigned long skel, BoneMatrix* out, int max)
{
    const int n = Read<int>(skel + kSkelCount);
    if (n <= 0 || n > kMaxBones) return 0;
    const int count = n > max ? max : n;
    for (int i = 0; i < count; ++i) BoneAt(skel, i, out[i]);
    return count;
}

const unsigned long* BodyChecksums()
{
    static unsigned long sums[kBodyBoneCount] = {};
    static bool done = false;
    if (!done) {
        for (int i = 0; i < kBodyBoneCount; ++i) sums[i] = NameChecksum(kBodyBones[i]);
        done = true;
    }
    return sums;
}

const char* const kBoneNames[] = {
    "Control_Root", "Bone_Pelvis", "Bone_Stomach_Lower", "Bone_Stomach_Upper", "Bone_Chest",
    "Bone_Collar_L", "Bone_Bicep_L", "Bone_Forearm_L", "Bone_Palm_L", "Bone_Fingers_Base_L",
    "Bone_Fingers_Tip_L", "Bone_Forefinger_Base_L", "Bone_Forefinger_Tip_L", "Bone_Thumb_L", "Bone_Wrist_L",
    "Bone_Bicep_Twist_Mid_L", "Bone_Bicep_Twist_Top_L", "Bone_Collar_R", "Bone_Bicep_R", "Bone_Forearm_R",
    "Bone_Palm_R", "Bone_Forefinger_Base_R", "Bone_Forefinger_Tip_R", "Bone_Fingers_Base_R",
    "Bone_Fingers_Tip_R", "Bone_Thumb_R", "Bone_Wrist_R", "Bone_Bicep_Twist_Mid_R",
    "Bone_Bicep_Twist_Top_R", "Bone_Neck", "Bone_Head", "Bone_Head_Top_Scale", "Bone_Ponytail_1",
    "Bone_Ponytail_2", "Bone_Nose_Scale", "Bone_Chin_Scale", "Cloth_Breast", "Cloth_Shirt_L",
    "Cloth_Shirt_C", "Cloth_Shirt_R", "Bone_Thigh_R", "Bone_Knee_R", "Bone_Ankle_R", "Bone_Toe_R",
    "Bone_Thigh_L", "Bone_Knee_L", "Bone_Ankle_L", "Bone_Toe_L", "Bone_Board_Root", "Bone_Board_Nose",
    "Bone_Trucks_Nose", "Bone_Board_Tail", "Bone_Trucks_Tail",
};

} // namespace

const char* const kBodyBones[kBodyBoneCount] = {
    "Bone_Pelvis", "Bone_Stomach_Upper", "Bone_Neck", "Bone_Head",
    "Bone_Bicep_L", "Bone_Forearm_L", "Bone_Wrist_L", "Bone_Bicep_R", "Bone_Forearm_R", "Bone_Wrist_R",
    "Bone_Thigh_L", "Bone_Knee_L", "Bone_Ankle_L", "Bone_Toe_L",
    "Bone_Thigh_R", "Bone_Knee_R", "Bone_Ankle_R", "Bone_Toe_R",
    "Bone_Chest", "Bone_Chest", // the second becomes a point along the chest's right axis
};

unsigned long NameChecksum(const char* name)
{
    unsigned long crc = 0xFFFFFFFFu;
    for (const char* p = name; *p; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        crc ^= static_cast<unsigned char>(c);
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc; // no final inversion (THPS style)
}

const char* BoneName(unsigned long checksum)
{
    for (const char* n : kBoneNames)
        if (NameChecksum(n) == checksum) return n;
    return nullptr;
}

bool DisplayCodeChecksPass(void (*log)(const char*, ...))
{
    bool ok = true;
    for (const Check& c : kDispChecks) {
        const unsigned char* code = reinterpret_cast<const unsigned char*>(c.address);
        for (unsigned int i = 0; i < c.length; ++i)
            if (code[i] != c.bytes[i]) {
                log("code check 0x%08lX: DIFFERENT at byte %u (expected %02X, found %02X) (%s)", c.address, i,
                    c.bytes[i], code[i], c.what);
                ok = false;
                break;
            }
    }
    log(ok ? "flips: %u code checks passed; reading reTHAWed's display rotation (body flips)"
           : "flips: code checks FAILED (%u checked); body flips not read", static_cast<unsigned>(sizeof(kDispChecks) / sizeof(kDispChecks[0])));
    g_dispOk = ok;
    return ok;
}

void ApplyDisplayRotation(const DisplayRot& d, float* points, int count, float* boardRows)
{
    if (!d.active) return;
    auto turnPoint = [&](float* q) {
        const float x = q[0] - d.pivot[0], y = q[1] - d.pivot[1], z = q[2] - d.pivot[2];
        for (int c = 0; c < 3; ++c) q[c] = x * d.L[c] + y * d.L[3 + c] + z * d.L[6 + c] + d.pivot[c];
    };
    for (int i = 0; i < count; ++i) turnPoint(points + i * 3);
    if (boardRows) {
        for (int r = 0; r < 3; ++r) { // axes: turned, not moved
            float* v = boardRows + r * 3;
            const float x = v[0], y = v[1], z = v[2];
            for (int c = 0; c < 3; ++c) v[c] = x * d.L[c] + y * d.L[3 + c] + z * d.L[6 + c];
        }
        turnPoint(boardRows + 9);
    }
}

bool PoseCodeChecksPass(void (*log)(const char*, ...))
{
    bool ok = true;
    for (const Check& c : kChecks) {
        const unsigned char* code = reinterpret_cast<const unsigned char*>(c.address);
        unsigned int bad = c.length;
        for (unsigned int i = 0; i < c.length; ++i)
            if (code[i] != c.bytes[i]) { bad = i; break; }
        if (bad == c.length) {
            log("code check 0x%08lX: MATCH (%s)", c.address, c.what);
        } else {
            log("code check 0x%08lX: DIFFERENT at byte %u (expected %02X, found %02X) (%s)", c.address, bad,
                c.bytes[bad], code[bad], c.what);
            ok = false;
        }
    }
    log(ok ? "pose: code checks passed; reading facing and skeleton every 100 ms"
           : "pose: code checks FAILED; facing and skeleton not read");
    return ok;
}

PoseSample ReadPose(unsigned long skater)
{
    PoseSample s;
    __try {
        ReadPoseUnprotected(skater, &s);
        s.readOk = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s.readOk = false;
        s.exceptionCode = GetExceptionCode();
    }
    return s;
}

int ReadSkeletonBones(unsigned long skeleton, BoneMatrix* out, int max)
{
    __try {
        return ReadSkeletonUnprotected(skeleton, out, max);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int ReadSkeletonBind(unsigned long skeleton, BoneMatrix* out, int max)
{
    __try {
        return ReadSkeletonBindUnprotected(skeleton, out, max);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int ReadAllBones(unsigned long skater, BoneMatrix* out, int max)
{
    __try {
        return ReadAllUnprotected(skater, out, max);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

} // namespace mp::thaw
