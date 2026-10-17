// SkateAndreas: the shared memory block both plugins use to talk (phase 0c;
// version 2 adds the skater, phase 2-1; version 3 adds the controller, phase 3-2;
// version 4 adds the skater's facing and the board bone, phase R4-2;
// version 5 adds the body bone positions, phase 5-3d; version 6: 20 of them;
// version 7: the high-resolution time each reTHAWed frame was first seen, 5-3f).
//
// Windows lets two programs open the same named block of memory. Whichever
// plugin starts first creates it; the other opens the same block. Each side
// writes only its own fields and reads the other's.
//
// Name "Local\SkateAndreas_v12": only programs in the user's own Windows
// session can open it. Nothing goes over the network.
//
// Layout rules: fixed-size 32-bit numbers only, so the GTA plugin and the THAW
// plugin (both 32-bit, both built by our CMake) always agree on it. If the
// layout changes, bump kLinkVersion and the name's _vN.

#pragma once

#include <windows.h>
#include <cmath>
#include <cstdint>

namespace mp {

constexpr LONG kLinkMagic = 0x3154504D; // the letters "MPT1" read as a number
constexpr LONG kLinkVersion = 20;         // ... 15: skitch (8.33); 16: skitching flag (8.33); 17: skitch grab rules (8.42); 18: skitch car's rear (8.51); 19: several skitch cars (8.52); 20: 8 skitch cars
constexpr wchar_t kLinkName[] = L"Local\\SkateAndreas_v20";
constexpr DWORD kLinkMappingSize = 4096; // one memory page; room to grow

// gtaInButtons bits: GTA's own pad button names (GInput maps the controller to
// these). The sticks and L2/R2 are in gtaInAxes.
constexpr LONG kBtnCross = 1 << 0;
constexpr LONG kBtnCircle = 1 << 1;
constexpr LONG kBtnSquare = 1 << 2;
constexpr LONG kBtnTriangle = 1 << 3;
constexpr LONG kBtnL1 = 1 << 4;
constexpr LONG kBtnR1 = 1 << 5;
constexpr LONG kBtnL3 = 1 << 6;
constexpr LONG kBtnR3 = 1 << 7;
constexpr LONG kBtnStart = 1 << 8;
constexpr LONG kBtnSelect = 1 << 9;
constexpr LONG kBtnUp = 1 << 10;
constexpr LONG kBtnDown = 1 << 11;
constexpr LONG kBtnLeft = 1 << 12;
constexpr LONG kBtnRight = 1 << 13;
constexpr int kButtonCount = 14;
inline const char* ButtonName(int bit)
{
    static const char* const names[kButtonCount] = {"Cross", "Circle", "Square", "Triangle", "L1",
        "R1", "L3", "R3", "Start", "Select", "Up", "Down", "Left", "Right"};
    return (bit >= 0 && bit < kButtonCount) ? names[bit] : "?";
}

// gtaInAxes order: GTA's values as GTA itself sees them (sticks about -128..128,
// L2/R2 0..255). Converted for reTHAWed on the THAW side (phase 3-3).
enum InputAxis { kAxisLX = 0, kAxisLY, kAxisRX, kAxisRY, kAxisL2, kAxisR2, kAxisCount };

// thawFlags bits
constexpr LONG kSkaterValid = 1;  // a local skater was read
constexpr LONG kStateValid = 2;   // menus/level/pause were read
constexpr LONG kInLevel = 4;      // reTHAWed is in a level (not the menus)
constexpr LONG kPaused = 8;       // reTHAWed is paused
constexpr LONG kPoseValid = 16;   // thawOrient and thawBoard were read (phase R4-2)
constexpr LONG kBodyValid = 32;   // thawBody was read (phase 5-3d)
constexpr int kLinkBodyBones = 20; // order: see kBodyBones in src/thaw/pose.cpp
constexpr LONG kBikeValid = 64;   // thawBike was worked out (rider on the bike, 8.29 B7)
// thawBike (8.29 B7), all in the space of reTHAWed's bike frame (Bone_Board_Root:
// x = bike's right, y = towards the back wheel, z = bike's up):
constexpr int kBikeFork = 0;      // 9: the fork's turn since the riding pose (rows, row vectors)
constexpr int kBikeCrank = 9;     // 3: direction from the crank axle to the pedal on the bike's right (unit)
constexpr int kBikeHandOn = 12;   // 2: rider's hand on the grip, bike's right then left (0..1)
constexpr int kBikeFootOn = 14;   // 2: rider's foot on the pedal, bike's right then left (0..1)
constexpr int kLinkBikeFloats = 16;
constexpr LONG kDroppedBikeValid = 128; // thawDroppedBike: reTHAWed's bike lying in the world (8.29 B12)

// reserved[0] bits (temporary test shortcuts; the touchpad is not in GTA's pad)
constexpr LONG kPadRawTouch = 1;  // DualSense touchpad pressed down (clicked)
constexpr LONG kPadRawUp = 2;     // D-pad Up, from the same read
constexpr LONG kPadRawDown = 4;   // D-pad Down, from the same read
constexpr LONG kPadRawLeft = 8;   // D-pad Left, from the same read
constexpr LONG kPadRawRight = 16; // D-pad Right, from the same read (C1: reTHAWed lean drawing on / off)

// reserved[1]: what GTA wants reTHAWed's music to do (8.27 radio)
constexpr LONG kMusicLeave = 0;   // don't touch (skate mode off)
constexpr LONG kMusicPlay = 1;    // reTHAWed soundtrack station
constexpr LONG kMusicPause = 2;   // a GTA station or Off

// gtaBikeWish (8.29 BMX)
constexpr LONG kBikeLeave = 0;    // don't touch reTHAWed's bike
constexpr LONG kBikeOn = 1;       // CJ is riding a BMX: rider on the bike
constexpr LONG kBikeOff = 2;      // CJ got off: rider off the bike
// thawBikeState
constexpr LONG kBikeStateUnknown = 0;
constexpr LONG kBikeStateOff = 1;
constexpr LONG kBikeStateOn = 2;

// reserved[5] (THAW, 8.28 a tags): graffiti tags in reTHAWed
constexpr LONG kTagCountMask = 0xFFFF;   // bits 0-15: finished tags so far (wraps)
constexpr LONG kTagNow = 0x10000;        // bit 16: skater is spraying a tag right now
constexpr LONG kTagWatchOk = 0x20000;    // bit 17: the THAW side's tag watch is working
constexpr LONG kTagOnWall = 0x40000;     // bit 18: the last finished tag went on a wall (else the ground)
constexpr LONG kTagSpotOk = 0x80000;     // bit 19: thawTagPos / thawTagNormal hold the last tag's spot

// 8.33 cars (version 14): the GTA plugin publishes the roof rails of the cars
// near CJ so reTHAWed's skater can grind them. Each car gives two rails (its
// roof's left and right edge), each a 2-point segment, in THAW units:
// gtaCarRail[(car*4 + i)*3 + 0..2], i = 0..3 (rail 0 a/b, rail 1 a/b).
constexpr int kCarRailCars = 8;
constexpr int kCarRailPts = 4;
// 8.52 (version 19): the cars the GTA plugin publishes for the proxy pool (version 20: 8, was 4)
constexpr int kPoolCars = 8;

struct LinkBlock {
    volatile LONG magic;    // kLinkMagic once set up (0 in a brand-new block)
    volatile LONG version;  // kLinkVersion
    volatile LONG gtaPid;   // GTA's process id, 0 = not running
    volatile LONG thawPid;  // reTHAWed's process id, 0 = not running
    volatile LONG gtaBeat;  // GTA plugin: +1 every second
    volatile LONG thawBeat; // THAW plugin: +1 every second
    volatile LONG gtaDrawn; // GTA: total frames drawn so far

    // ---- the skater, written by the THAW plugin (phase 2-1) ----
    // thawSeq is a "sequence lock": odd while the THAW side is writing, even
    // when done. The reader copies everything, then checks thawSeq didn't
    // change, so it never uses a half-written set of numbers.
    volatile LONG thawSeq;
    volatile LONG thawFlags;        // kSkaterValid | kStateValid | kInLevel | kPaused
    volatile LONG thawLevelId;      // reTHAWed's level checksum
    volatile float thawPos[3];      // skater position, THAW units (x, y = up, z)
    volatile float thawVel[3];      // skater velocity, THAW units per second
    volatile LONG thawSampleTick;   // GetTickCount64() of the sample, low 32 bits (ms)
    volatile LONG thawSampleCount;  // +1 per published sample

    volatile LONG gtaSkateMode;     // GTA: 1 while skate mode is on (phase 2-2)

    // ---- the controller, written by the GTA plugin (phase 3-2) ----
    // Pad 0 as GTA read it this frame. gtaInSeq is a sequence lock like thawSeq.
    volatile LONG gtaInSeq;
    volatile LONG gtaInTick;        // GetTickCount64() when read, low 32 bits (ms)
    volatile LONG gtaInCount;       // +1 per published frame
    volatile LONG gtaInButtons;     // kBtn* bits
    volatile short gtaInAxes[kAxisCount];

    // reserved[0], 2026-10-09 (temporary, remove before release): the THAW
    // plugin's own read of the DualSense (src/thaw/touchpad.cpp), kPadRaw* bits.
    // Read with PadRawButtons().
    // reserved[1], [2] (8.27 radio): written by GTA: wanted reTHAWed music
    // (kMusic*) and GTA's GetTickCount() when written.
    // reserved[3] (GTA): wanted reTHAWed music volume 0..100, -1 = leave it.
    // reserved[4] (THAW): reTHAWed's current music volume 0..100.
    // reserved[5] (THAW, 8.28 a): graffiti tags, kTag* bits.
    LONG reserved[6];               // was padding to 128 bytes in version 3

    // ---- phase R4-2, written by the THAW plugin under thawSeq ----
    // The skater's orientation, rows right, up, at (at = facing; up tilts on
    // ramps and walls), THAW world directions. And the board bone
    // (Bone_Board_Root) in the skater's own space: rows right, up (towards
    // the tail), at (out of the deck's top), position (THAW units).
    volatile float thawOrient[9];
    volatile float thawBoard[12];
    // ---- phase 5-3d: body bone positions in the skater's own space (THAW
    // units), kLinkBodyBones x (x, y, z), same order as the THAW plugin's kBodyBones.
    volatile float thawBody[kLinkBodyBones * 3];
    // ---- phase 5-3f: QueryPerformanceCounter value (same clock in every
    // program on the PC) when the THAW plugin first saw this reTHAWed frame;
    // low and high 32 bits. GTA blends between frames by these times.
    volatile LONG thawFrameQpcLo, thawFrameQpcHi;
    // ---- 8.28 a (version 8), written by the THAW plugin before it bumps the
    // finished-tag count in reserved[5]: where reTHAWed put the last tag (THAW
    // units) and the direction the tag faces (out of the wall / up from the ground).
    volatile float thawTagPos[3];
    volatile float thawTagNormal[3];
    // ---- 8.29 BMX (version 9) ----
    volatile LONG gtaBikeWish;      // GTA: kBike* (is CJ riding a BMX?)
    volatile LONG thawBikeState;    // THAW: kBikeState* (is reTHAWed's rider on the bike?)
    // ---- 8.29 B2 (version 10): GTA asks the THAW plugin to put reTHAWed's
    // rider at a spot (THAW units, feet) facing a direction (THAW world, level).
    // GTA writes pos / facing first, then bumps gtaPlaceSeq; THAW does it once
    // and copies the number into thawPlaceDone.
    volatile float gtaPlacePos[3];
    volatile float gtaPlaceAt[3];
    volatile LONG gtaPlaceSeq;
    volatile LONG thawPlaceDone;
    // ---- 8.29 B7 (version 11), written by the THAW plugin under thawSeq
    // with kBikeValid: reTHAWed's bike parts, see kBike* above.
    volatile float thawBike[kLinkBikeFloats];
    // ---- 8.29 B12 (version 12), under thawSeq with kDroppedBikeValid: while
    // the rider is off the bike, reTHAWed's bike frame in THAW's world (rows
    // right, up (towards the back), at (up), position), same convention as thawBoard.
    volatile float thawDroppedBike[12];
    // ---- 8.32 R3b test (version 13): the F5 test rail, so GTA can show it.
    // THAW writes the two ends (THAW units) first, then thawTestRailOn = 1;
    // 0 = no test rail (none placed yet, or the level went away).
    volatile float thawTestRail[6];
    volatile LONG thawTestRailOn;
    // ---- 8.33 cars (version 14), written by the GTA plugin: the roof rails of
    // the cars near CJ (see kCarRailCars above), under gtaCarSeq (a sequence
    // lock like thawSeq). gtaCarCount cars, 0 = none.
    volatile LONG gtaCarSeq;
    volatile LONG gtaCarCount;
    volatile float gtaCarRail[kCarRailCars * kCarRailPts * 3];
    // ---- 8.33 skitch (version 15), written by the GTA plugin: the nearest MOVING
    // car, so the skater can grab it and be towed (THAW's skitch). Position and
    // velocity in THAW units; gtaSkitchValid = 1 when there is one.
    volatile LONG gtaSkitchValid;
    volatile float gtaSkitchPos[3];
    volatile float gtaSkitchVel[3];
    // ---- 8.42 (version 17): which car that is (its GTA pool index) and which way its nose
    // points (THAW direction, unit length). Also: the car the THAW side is holding, so the
    // GTA side keeps publishing that one (thawSkitchId, -1 = none).
    volatile LONG gtaSkitchId;
    volatile float gtaSkitchFwd[3];
    // ---- 8.33 (version 16), written by the THAW plugin: 1 while it is skitching a
    // moving car. GTA then leaves CJ's spot alone (no car push-out, no car-hit bail):
    // the whole point is to travel with the car, and the push-out was shoving him
    // about inside it.
    volatile LONG thawSkitching;
    volatile LONG thawSkitchId;
    // ---- 8.51 step 6 (version 18), written by the GTA plugin with gtaSkitchPos: how far the
    // published car's rear bumper is behind its centre, in THAW inches (from its collision box),
    // so the THAW side can put the proxy's hold point on the bumper.
    volatile float gtaSkitchBack;
    // ---- 8.52 step 1 (version 19), written by the GTA plugin under gtaPoolSeq (a sequence lock
    // like gtaCarSeq): up to kPoolCars moving cars near CJ, nearest first, the held car
    // (thawSkitchId) always among them. Per car: GTA pool index, position and velocity (THAW
    // units, inches per second), nose (THAW direction, unit length), rear bumper behind its
    // centre (THAW inches). The one-car gtaSkitch* fields above are still filled as before.
    volatile LONG gtaPoolSeq;
    volatile LONG gtaPoolCount;
    volatile LONG gtaPoolId[kPoolCars];
    volatile float gtaPoolPos[kPoolCars * 3];
    volatile float gtaPoolVel[kPoolCars * 3];
    volatile float gtaPoolFwd[kPoolCars * 3];
    volatile float gtaPoolBack[kPoolCars];
};
static_assert(sizeof(LinkBlock) == 1472, "LinkBlock layout changed: bump kLinkVersion");

// 8.44 N1 (audit, 2026-10-15): every number one side reads from the other is checked
// for NaN / infinity before use. A bad value once spread from reTHAWed into CJ's
// position and blanked GTA's screen (the moving-rail NaN, 8.41). Each process counts
// the samples it dropped; the per-second logs print the count when it grows.
inline volatile LONG g_linkBadNumbers = 0;
inline LONG LinkBadNumbers() { return g_linkBadNumbers; }
inline bool AllFinite(const float* p, int n)
{
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(p[i])) return false;
    return true;
}

// One skater sample, as copied out of the block.
struct SkaterShared {
    LONG seq = 0;
    LONG flags = 0;
    LONG levelId = 0;
    float pos[3] = {};
    float vel[3] = {};
    LONG sampleTick = 0;
    LONG sampleCount = 0;
    float orient[9] = {};  // valid with kPoseValid
    float board[12] = {};  // valid with kPoseValid
    float body[kLinkBodyBones * 3] = {}; // valid with kBodyValid
    long long frameQpc = 0;              // 5-3f: when this reTHAWed frame was first seen
    float bike[kLinkBikeFloats] = {};    // 8.29 B7: valid with kBikeValid
    float droppedBike[12] = {};          // 8.29 B12: valid with kDroppedBikeValid
};

// THAW side (the only writer): publish one sample.
inline void PublishSkater(LinkBlock* b, const SkaterShared& s)
{
    const LONG seq = b->thawSeq;
    InterlockedExchange(&b->thawSeq, seq + 1); // odd: writing
    b->thawFlags = s.flags;
    b->thawLevelId = s.levelId;
    for (int i = 0; i < 3; ++i) {
        b->thawPos[i] = s.pos[i];
        b->thawVel[i] = s.vel[i];
    }
    b->thawSampleTick = s.sampleTick;
    b->thawSampleCount = s.sampleCount;
    for (int i = 0; i < 9; ++i) b->thawOrient[i] = s.orient[i];
    for (int i = 0; i < 12; ++i) b->thawBoard[i] = s.board[i];
    for (int i = 0; i < kLinkBodyBones * 3; ++i) b->thawBody[i] = s.body[i];
    for (int i = 0; i < kLinkBikeFloats; ++i) b->thawBike[i] = s.bike[i];
    for (int i = 0; i < 12; ++i) b->thawDroppedBike[i] = s.droppedBike[i];
    b->thawFrameQpcLo = static_cast<LONG>(s.frameQpc & 0xFFFFFFFFll);
    b->thawFrameQpcHi = static_cast<LONG>(s.frameQpc >> 32);
    InterlockedExchange(&b->thawSeq, seq + 2); // even: done
}

// GTA side: copy the latest sample. Returns false if it kept changing while
// being read (tries a few times); `retries` says how many extra tries it took.
inline bool ReadSkaterShared(const LinkBlock* b, SkaterShared& out, int& retries)
{
    retries = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const LONG s1 = b->thawSeq;
        if (s1 & 1) { ++retries; YieldProcessor(); continue; }
        MemoryBarrier();
        out.flags = b->thawFlags;
        out.levelId = b->thawLevelId;
        for (int i = 0; i < 3; ++i) {
            out.pos[i] = b->thawPos[i];
            out.vel[i] = b->thawVel[i];
        }
        out.sampleTick = b->thawSampleTick;
        out.sampleCount = b->thawSampleCount;
        for (int i = 0; i < 9; ++i) out.orient[i] = b->thawOrient[i];
        for (int i = 0; i < 12; ++i) out.board[i] = b->thawBoard[i];
        for (int i = 0; i < kLinkBodyBones * 3; ++i) out.body[i] = b->thawBody[i];
        for (int i = 0; i < kLinkBikeFloats; ++i) out.bike[i] = b->thawBike[i];
        for (int i = 0; i < 12; ++i) out.droppedBike[i] = b->thawDroppedBike[i];
        out.frameQpc = (static_cast<long long>(b->thawFrameQpcHi) << 32) |
                       static_cast<long long>(static_cast<unsigned long>(b->thawFrameQpcLo));
        MemoryBarrier();
        if (b->thawSeq == s1) {
            out.seq = s1;
            // 8.44 N1: a bad position or speed makes the whole sample unusable (callers
            // skip it like a torn read); bad pose / body / bike numbers only lose their flag.
            if (!AllFinite(out.pos, 3) || !AllFinite(out.vel, 3)) {
                InterlockedIncrement(&g_linkBadNumbers);
                return false;
            }
            if (!AllFinite(out.orient, 9) || !AllFinite(out.board, 12)) out.flags &= ~kPoseValid;
            if (!AllFinite(out.body, kLinkBodyBones * 3)) out.flags &= ~kBodyValid;
            if (!AllFinite(out.bike, kLinkBikeFloats)) out.flags &= ~kBikeValid;
            if (!AllFinite(out.droppedBike, 12)) out.flags &= ~kDroppedBikeValid;
            return true;
        }
        ++retries;
    }
    return false;
}

// One controller frame, as copied out of the block.
struct InputShared {
    LONG seq = 0;
    LONG tick = 0;
    LONG count = 0;
    LONG buttons = 0;
    short axes[kAxisCount] = {};
};

// GTA side (the only writer): publish one controller frame.
inline void PublishInput(LinkBlock* b, const InputShared& in)
{
    const LONG seq = b->gtaInSeq;
    InterlockedExchange(&b->gtaInSeq, seq + 1); // odd: writing
    b->gtaInTick = in.tick;
    b->gtaInCount = in.count;
    b->gtaInButtons = in.buttons;
    for (int i = 0; i < kAxisCount; ++i) b->gtaInAxes[i] = in.axes[i];
    InterlockedExchange(&b->gtaInSeq, seq + 2); // even: done
}

// THAW side: copy the latest controller frame (false if it kept changing).
inline bool ReadInputShared(const LinkBlock* b, InputShared& out)
{
    for (int attempt = 0; attempt < 8; ++attempt) {
        const LONG s1 = b->gtaInSeq;
        if (s1 & 1) { YieldProcessor(); continue; }
        MemoryBarrier();
        out.tick = b->gtaInTick;
        out.count = b->gtaInCount;
        out.buttons = b->gtaInButtons;
        for (int i = 0; i < kAxisCount; ++i) out.axes[i] = b->gtaInAxes[i];
        MemoryBarrier();
        if (b->gtaInSeq == s1) { out.seq = s1; return true; }
    }
    return false;
}

// 8.33 cars, GTA side (the only writer): publish the roof rails of the cars near
// CJ (count cars x kCarRailPts x 3 floats, see kCarRailCars).
inline void PublishCarRails(LinkBlock* b, int count, const float* pts)
{
    if (count < 0) count = 0;
    if (count > kCarRailCars) count = kCarRailCars;
    const LONG seq = b->gtaCarSeq;
    InterlockedExchange(&b->gtaCarSeq, seq + 1); // odd: writing
    b->gtaCarCount = count;
    const int n = count * kCarRailPts * 3;
    for (int i = 0; i < n; ++i) b->gtaCarRail[i] = pts[i];
    InterlockedExchange(&b->gtaCarSeq, seq + 2); // even: done
}

// 8.52 step 1: one published skitch car (see gtaPool* in LinkBlock)
struct PoolCar {
    LONG id = -1;
    float pos[3] = {}, vel[3] = {}, fwd[3] = {};
    float back = 0.0f;
};

// GTA side (the only writer): publish up to kPoolCars cars
inline void PublishPoolCars(LinkBlock* b, int count, const PoolCar* cars)
{
    if (count < 0) count = 0;
    if (count > kPoolCars) count = kPoolCars;
    const LONG seq = b->gtaPoolSeq;
    InterlockedExchange(&b->gtaPoolSeq, seq + 1); // odd: writing
    b->gtaPoolCount = count;
    for (int i = 0; i < count; ++i) {
        b->gtaPoolId[i] = cars[i].id;
        for (int k = 0; k < 3; ++k) {
            b->gtaPoolPos[i * 3 + k] = cars[i].pos[k];
            b->gtaPoolVel[i * 3 + k] = cars[i].vel[k];
            b->gtaPoolFwd[i * 3 + k] = cars[i].fwd[k];
        }
        b->gtaPoolBack[i] = cars[i].back;
    }
    InterlockedExchange(&b->gtaPoolSeq, seq + 2); // even: done
}

// THAW side: copy them out. Returns the number of cars (0 if none / unstable / a bad number).
inline int ReadPoolCars(const LinkBlock* b, PoolCar* out)
{
    for (int attempt = 0; attempt < 8; ++attempt) {
        const LONG s1 = b->gtaPoolSeq;
        if (s1 & 1) { YieldProcessor(); continue; }
        MemoryBarrier();
        int count = b->gtaPoolCount;
        if (count < 0) count = 0;
        if (count > kPoolCars) count = kPoolCars;
        for (int i = 0; i < count; ++i) {
            out[i].id = b->gtaPoolId[i];
            for (int k = 0; k < 3; ++k) {
                out[i].pos[k] = b->gtaPoolPos[i * 3 + k];
                out[i].vel[k] = b->gtaPoolVel[i * 3 + k];
                out[i].fwd[k] = b->gtaPoolFwd[i * 3 + k];
            }
            out[i].back = b->gtaPoolBack[i];
        }
        MemoryBarrier();
        if (b->gtaPoolSeq == s1) {
            // 8.44 N1: no bad number may reach reTHAWed
            for (int i = 0; i < count; ++i)
                if (!AllFinite(out[i].pos, 3) || !AllFinite(out[i].vel, 3) || !AllFinite(out[i].fwd, 3) ||
                    !AllFinite(&out[i].back, 1)) {
                    InterlockedIncrement(&g_linkBadNumbers);
                    return 0;
                }
            return count;
        }
    }
    return 0;
}

// THAW side: copy them out. Returns the number of cars (0 if none / unstable).
inline int ReadCarRails(const LinkBlock* b, float* out, int floats)
{
    for (int attempt = 0; attempt < 8; ++attempt) {
        const LONG s1 = b->gtaCarSeq;
        if (s1 & 1) { YieldProcessor(); continue; }
        MemoryBarrier();
        int count = b->gtaCarCount;
        if (count < 0) count = 0;
        if (count > kCarRailCars) count = kCarRailCars;
        int n = count * kCarRailPts * 3;
        if (n > floats) {
            count = floats / (kCarRailPts * 3);
            n = count * kCarRailPts * 3;
        }
        for (int i = 0; i < n; ++i) out[i] = b->gtaCarRail[i];
        MemoryBarrier();
        if (b->gtaCarSeq == s1) {
            // 8.44 N1: a car rail with a bad number must never reach reTHAWed's rail records
            if (!AllFinite(out, n)) {
                InterlockedIncrement(&g_linkBadNumbers);
                return 0;
            }
            return count;
        }
    }
    return 0;
}

enum class LinkOpenResult { Ok, MappingFailed, ViewFailed, WrongLayout };
// Open the shared block, creating it if this side is first.
// On success `block` points into the shared memory and `createdHere` says
// whether this side created it. `error` gets the Windows error number on failure.
inline LinkOpenResult OpenLink(HANDLE& mapping, LinkBlock*& block, bool& createdHere, DWORD& error)
{
    block = nullptr;
    createdHere = false;
    error = 0;

    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                 kLinkMappingSize, kLinkName);
    if (!mapping) {
        error = GetLastError();
        return LinkOpenResult::MappingFailed;
    }
    createdHere = (GetLastError() != ERROR_ALREADY_EXISTS);

    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LinkBlock));
    if (!view) {
        error = GetLastError();
        CloseHandle(mapping);
        mapping = nullptr;
        return LinkOpenResult::ViewFailed;
    }
    block = static_cast<LinkBlock*>(view);

    // 8.42: a fresh block is all zeros, and 0 would mean "holding car 0": start both car ids at -1.
    if (createdHere) {
        block->gtaSkitchId = -1;
        block->thawSkitchId = -1;
    }

    // Set up the header if nobody has yet. Both sides may get here at the same
    // moment; writing the same version twice is harmless, and the atomic
    // compare-and-set on magic means only one side "sets it up".
    InterlockedCompareExchange(&block->version, kLinkVersion, 0);
    InterlockedCompareExchange(&block->magic, kLinkMagic, 0);

    if (block->magic != kLinkMagic || block->version != kLinkVersion) {
        return LinkOpenResult::WrongLayout; // caller logs the values it found
    }
    return LinkOpenResult::Ok;
}

inline const char* LinkOpenResultText(LinkOpenResult r)
{
    switch (r) {
    case LinkOpenResult::Ok: return "ok";
    case LinkOpenResult::MappingFailed: return "could not create/open the shared block";
    case LinkOpenResult::ViewFailed: return "could not map the shared block";
    case LinkOpenResult::WrongLayout: return "shared block has a different layout (magic/version)";
    }
    return "unknown";
}

// Tracks the other side, so the logs only need one call per second.
struct PeerWatch {
    LONG lastPid = -1;  // -1 = nothing logged yet
    LONG lastBeat = 0;
};

// The THAW plugin's direct DualSense read (kPadRaw* bits; 0 when none).
inline LONG PadRawButtons(const LinkBlock* b) { return b ? b->reserved[0] : 0; }

} // namespace mp
