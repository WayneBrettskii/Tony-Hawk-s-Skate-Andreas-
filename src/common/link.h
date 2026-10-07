// my-passthrough: the shared memory block both plugins use to talk (phase 0c).
//
// Windows lets two programs open the same named block of memory. Whichever
// plugin starts first creates it; the other opens the same block. Each side
// writes only its own fields and reads the other's.
//
// Name "Local\MyPassthrough_v1": only programs in the user's own Windows
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
constexpr LONG kLinkVersion = 1;
constexpr wchar_t kLinkName[] = L"Local\\MyPassthrough_v1";
constexpr DWORD kLinkMappingSize = 4096; // one memory page; room to grow

struct LinkBlock {
    volatile LONG magic;    // kLinkMagic once set up (0 in a brand-new block)
    volatile LONG version;  // kLinkVersion
    volatile LONG gtaPid;   // GTA's process id, 0 = not running
    volatile LONG thawPid;  // reTHAWed's process id, 0 = not running
    volatile LONG gtaBeat;  // GTA plugin: +1 every second
    volatile LONG thawBeat; // THAW plugin: +1 every second
    volatile LONG gtaDrawn; // GTA: total frames drawn so far
    LONG reserved[9];       // unused; keeps the block 64 bytes
};
static_assert(sizeof(LinkBlock) == 64, "LinkBlock layout changed: bump kLinkVersion");

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
