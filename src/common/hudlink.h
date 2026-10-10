// my-passthrough: shared memory block for reTHAWed's HUD picture (phase 6 H3,
// docs/DESIGN.md 8.25). Written by the THAW plugin (src/thaw/hud.cpp), read by
// the GTA plugin (src/gta/hudshow.cpp).
//
// Layout: a 4 KB header, then 2 picture slots. A slot holds one picture of up
// to kHudMaxW x kHudMaxH pixels, rows packed (width * 4 bytes), BGRA with the
// colours already multiplied by alpha. The writer fills the slot that isn't
// the newest one, then marks it newest. slotSeq[] is odd while a slot is being
// written; the reader compares it before and after copying.

#pragma once

#include <windows.h>

namespace mp {

constexpr LONG kHudMagic = 0x44554850; // "PHUD"
constexpr LONG kHudVersion = 1;
constexpr int kHudMaxW = 1920;
constexpr int kHudMaxH = 1200;
constexpr DWORD kHudHeaderBytes = 4096;
constexpr DWORD kHudSlotBytes = kHudMaxW * kHudMaxH * 4;
constexpr DWORD kHudTotalBytes = kHudHeaderBytes + 2 * kHudSlotBytes;
constexpr const wchar_t* kHudName = L"Local\\MyPassthrough_Hud_v1";

struct HudLinkHeader {
    volatile LONG magic;       // kHudMagic once the writer set it up
    volatile LONG version;     // kHudVersion
    volatile LONG latest;      // newest complete slot (0 or 1), -1 = none yet
    volatile LONG frame;       // +1 per published picture
    volatile LONG tickMs;      // GetTickCount() when the newest picture was published
    volatile LONG slotSeq[2];  // odd = being written
    volatile LONG slotW[2];    // picture size in each slot
    volatile LONG slotH[2];
};

inline BYTE* HudSlot(HudLinkHeader* h, int slot)
{
    return reinterpret_cast<BYTE*>(h) + kHudHeaderBytes + static_cast<size_t>(slot) * kHudSlotBytes;
}

// Create or open the block. Both sides may call it; whoever is first creates it
// (zeroed). Returns nullptr on failure (error in *error).
inline HudLinkHeader* OpenHudLink(HANDLE& mapping, DWORD* error)
{
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kHudTotalBytes, kHudName);
    if (!mapping) {
        if (error) *error = GetLastError();
        return nullptr;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, kHudTotalBytes);
    if (!view) {
        if (error) *error = GetLastError();
        CloseHandle(mapping);
        mapping = nullptr;
        return nullptr;
    }
    return static_cast<HudLinkHeader*>(view);
}

} // namespace mp
