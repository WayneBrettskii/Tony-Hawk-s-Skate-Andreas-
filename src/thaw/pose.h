// my-passthrough: reading the skater's facing and skeleton from reTHAWed
// (phase R4-1, docs/DESIGN.md 8.15). READ ONLY.
//
// Where the numbers come from (read from a copy of reTHAWed.exe 5.0.0.1):
//   - the skater object's orientation: 4 x 4 floats at +0x88 (rows), used by
//     0x0046A9A0 (the bone-to-world code behind the script command
//     GetBonePosition) next to the position at +0x68
//   - the object's components: sorted (checksum, component) pairs at +0x60,
//     count at +0x64, fallback list at +0x11C (0x0042E8C0); the skeleton
//     component has checksum 0x222756D5 ("skeleton"); its skeleton at +0x18
//   - the skeleton (0x004EFF10): bone count +0x14, bone list +0x18 (0x1C bytes
//     each, first value = the bone name's checksum), current bone matrices
//     +0x04 (64 bytes each), bind-pose inverse matrices at [[+0x08]+0x04]+0x28.
//     A bone's matrix in the skater's own space = inverse(bind inverse) x
//     current, as the game computes it.

#pragma once

#include <windows.h>

namespace mp::thaw {

struct BoneMatrix {
    unsigned long name = 0; // checksum of the bone's name
    float m[16] = {};       // rows: right, up, at (forward), position; skater's own space
};

// Phase 5-3d: the body bones sent to GTA, in this order (positions only).
constexpr int kBodyBoneCount = 20; // the last two: chest, and a point 10 units along the chest's right axis
extern const char* const kBodyBones[kBodyBoneCount];

struct PoseSample {
    bool readOk = false;      // false = an exception happened while reading
    DWORD exceptionCode = 0;
    float orient[16] = {};    // skater object's orientation (rows)
    unsigned long skeleton = 0;
    int boneCount = 0;        // 0 = no skeleton found
    int boardIndex = -1, pelvisIndex = -1;
    BoneMatrix board, pelvis; // Bone_Board_Root, Bone_Pelvis (valid if index >= 0)
    bool haveBody = false;    // all kBodyBones found
    float body[kBodyBoneCount * 3] = {}; // their positions, skater's own space (THAW units)
};

// Code checks for the addresses above; all must pass before ReadPose is used.
bool PoseCodeChecksPass(void (*log)(const char*, ...));

// Reads orientation, board and pelvis. `skater` from ReadSkater().
PoseSample ReadPose(unsigned long skater);

// Every bone (up to `max`); returns how many were read, -1 on a read failure.
int ReadAllBones(unsigned long skater, BoneMatrix* out, int max);

// THPS checksum of a name (CRC32 of the lower-case text, as the game uses).
unsigned long NameChecksum(const char* name);

// Our name for a bone checksum, or nullptr if we don't know it.
const char* BoneName(unsigned long checksum);

} // namespace mp::thaw
