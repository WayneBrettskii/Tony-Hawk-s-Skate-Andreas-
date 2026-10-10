// my-passthrough: the shared memory block both plugins use to talk (phase 0c;
// version 2 adds the skater, phase 2-1; version 3 adds the controller, phase 3-2;
// version 4 adds the skater's facing and the board bone, phase R4-2;
// version 5 adds the body bone positions, phase 5-3d; version 6: 20 of them;
// version 7: the high-resolution time each reTHAWed frame was first seen, 5-3f).
//
// Windows lets two programs open the same named block of memory. Whichever
// plugin starts first creates it; the other opens the same block. Each side
// writes only its own fields and reads the other's.
//
// Name "Local\MyPassthrough_v7": only programs in the user's own Windows
// session can open it. Nothing goes over the network.
//
// Layout rules: fixed-size 32-bit numbers only, so the GTA plugin and the THAW
// plugin (both 32-bit, both built by our CMake) always agree on it. If the
// layout changes, bump kLinkVersion and the name's _vN.

#pragma once

#include <windows.h>
#include <cstdint>

namespace mp {

constexpr LONG kLinkMagic = 0x3154504D; // the letters "MPT1" read as a number
constexpr LONG kLinkVersion = 7;         // 2: skater (2-1); 3: controller (3-2); 4: facing + board (R4-2); 5: body (5-3d)
constexpr wchar_t kLinkName[] = L"Local\\MyPassthrough_v7";
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

// reserved[0] bits (temporary test shortcuts; the touchpad is not in GTA's pad)
constexpr LONG kPadRawTouch = 1;  // DualSense touchpad pressed down (clicked)
constexpr LONG kPadRawUp = 2;     // D-pad Up, from the same read
constexpr LONG kPadRawDown = 4;   // D-pad Down, from the same read
constexpr LONG kPadRawLeft = 8;   // D-pad Left, from the same read

// reserved[1]: what GTA wants reTHAWed's music to do (8.27 radio)
constexpr LONG kMusicLeave = 0;   // don't touch (skate mode off)
constexpr LONG kMusicPlay = 1;    // reTHAWed soundtrack station
constexpr LONG kMusicPause = 2;   // a GTA station or Off

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
    // (kMusic*) and GTA's GetTickCount() when written. reserved[3..5] unused.
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
};
static_assert(sizeof(LinkBlock) == 460, "LinkBlock layout changed: bump kLinkVersion");

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
        out.frameQpc = (static_cast<long long>(b->thawFrameQpcHi) << 32) |
                       static_cast<long long>(static_cast<unsigned long>(b->thawFrameQpcLo));
        MemoryBarrier();
        if (b->thawSeq == s1) { out.seq = s1; return true; }
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
