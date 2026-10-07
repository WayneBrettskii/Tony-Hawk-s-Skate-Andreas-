// my-passthrough: the shared memory block both plugins use to talk (phase 0c;
// version 2 adds the skater, phase 2-1).
//
// Windows lets two programs open the same named block of memory. Whichever
// plugin starts first creates it; the other opens the same block. Each side
// writes only its own fields and reads the other's.
//
// Name "Local\MyPassthrough_v2": only programs in the user's own Windows
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
constexpr LONG kLinkVersion = 2;         // 2: adds the skater (phase 2-1)
constexpr wchar_t kLinkName[] = L"Local\\MyPassthrough_v2";
constexpr DWORD kLinkMappingSize = 4096; // one memory page; room to grow

// thawFlags bits
constexpr LONG kSkaterValid = 1;  // a local skater was read
constexpr LONG kStateValid = 2;   // menus/level/pause were read
constexpr LONG kInLevel = 4;      // reTHAWed is in a level (not the menus)
constexpr LONG kPaused = 8;       // reTHAWed is paused

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
    LONG reserved[13];              // unused; keeps the block 128 bytes
};
static_assert(sizeof(LinkBlock) == 128, "LinkBlock layout changed: bump kLinkVersion");

// One skater sample, as copied out of the block.
struct SkaterShared {
    LONG seq = 0;
    LONG flags = 0;
    LONG levelId = 0;
    float pos[3] = {};
    float vel[3] = {};
    LONG sampleTick = 0;
    LONG sampleCount = 0;
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
        MemoryBarrier();
        if (b->thawSeq == s1) { out.seq = s1; return true; }
        ++retries;
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

} // namespace mp
