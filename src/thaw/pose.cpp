// my-passthrough: skater facing and skeleton reader. See pose.h. READ ONLY.

#include "pose.h"

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

void ReadPoseUnprotected(unsigned long skater, PoseSample* out)
{
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

int ReadAllBones(unsigned long skater, BoneMatrix* out, int max)
{
    __try {
        return ReadAllUnprotected(skater, out, max);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

} // namespace mp::thaw
