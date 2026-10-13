// SkateAndreas: find reTHAWed's bike skeleton. READ ONLY. See bikefind.h.

#include <windows.h>

#include "bikefind.h"

namespace mp::thaw {
namespace {

constexpr unsigned long kCompArray = 0x60, kCompCount = 0x64; // as pose.cpp
constexpr unsigned long kWindow = 0x200000;                   // +-2 MB around the rider's skeleton
constexpr DWORD kRetryMs = 500;

unsigned long g_addr = 0;
DWORD g_lastTry = 0;
bool g_loggedMiss = false;

bool Readable(unsigned long p) { return p >= 0x00010000u && p < 0x7FFF0000u && (p & 3) == 0; }

// A skeleton (bone count +0x14, bone list +0x18, 0x1C bytes per bone, name
// checksum first) with Bone_Bike_Frame among its bones.
bool IsBike(unsigned long s)
{
    __try {
        if (!Readable(s)) return false;
        const int count = *reinterpret_cast<const int*>(s + 0x14);
        if (count < 3 || count > 32) return false;
        const unsigned long bones = *reinterpret_cast<const unsigned long*>(s + 0x18);
        const unsigned long mats = *reinterpret_cast<const unsigned long*>(s + 0x04);
        if (!Readable(bones) || !Readable(mats)) return false;
        for (int i = 0; i < count; ++i)
            if (*reinterpret_cast<const unsigned long*>(bones + i * 0x1C) == kBikeBoneFrame) return true;
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Through the rider's components: a component holding the skeleton at +0x18
// (as the rider's skeleton component does), or anywhere in its first 0x200 bytes.
unsigned long ViaComponents(unsigned long skater, unsigned long& compOut, unsigned long& offOut)
{
    __try {
        const unsigned long arr = *reinterpret_cast<const unsigned long*>(skater + kCompArray);
        const int count = *reinterpret_cast<const int*>(skater + kCompCount);
        if (!Readable(arr) || count <= 0 || count > 128) return 0;
        for (int i = 0; i < count; ++i) {
            const unsigned long comp = *reinterpret_cast<const unsigned long*>(arr + i * 8 + 4);
            if (!Readable(comp)) continue;
            for (unsigned long off = 0; off < 0x200; off += 4) {
                const unsigned long p = *reinterpret_cast<const unsigned long*>(comp + off);
                if (IsBike(p)) {
                    compOut = *reinterpret_cast<const unsigned long*>(arr + i * 8);
                    offOut = off;
                    return p;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}

// Near the rider's skeleton (test 13:45: 0x3FE0 after it).
unsigned long Nearby(unsigned long rider)
{
    if (!rider) return 0;
    const unsigned long lo = rider > kWindow + 0x10000 ? rider - kWindow : 0x10000;
    const unsigned long hi = rider + kWindow;
    for (unsigned long s = lo; s < hi; s += 4) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<void*>(s), &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
        const unsigned long base = reinterpret_cast<unsigned long>(mbi.BaseAddress);
        const unsigned long end = base + static_cast<unsigned long>(mbi.RegionSize);
        const DWORD prot = mbi.Protect & 0xFF;
        const bool rw = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
                        (prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE);
        if (!rw) { s = end - 4; continue; }
        const unsigned long stop = end < hi ? end : hi;
        for (; s + 0x20 <= stop; s += 4)
            if (IsBike(s)) return s;
        s = stop - 4;
    }
    return 0;
}

} // namespace

void ForgetBikeSkeleton()
{
    g_addr = 0;
    g_loggedMiss = false;
}

unsigned long FindBikeSkeleton(unsigned long riderSkeleton, unsigned long skater, void (*log)(const char*, ...))
{
    if (g_addr && IsBike(g_addr)) return g_addr;
    g_addr = 0;
    const DWORD now = GetTickCount();
    if (g_lastTry && now - g_lastTry < kRetryMs) return 0;
    g_lastTry = now;
    unsigned long comp = 0, off = 0;
    unsigned long a = skater ? ViaComponents(skater, comp, off) : 0;
    if (a) {
        log("bike skeleton: found at 0x%08lX through the rider's component 0x%08lX (+0x%lX)", a, comp, off);
    } else {
        a = Nearby(riderSkeleton);
        if (a)
            log("bike skeleton: found at 0x%08lX, %+ld bytes from the rider's skeleton (not through a component)", a,
                static_cast<long>(a) - static_cast<long>(riderSkeleton));
    }
    if (!a && !g_loggedMiss) {
        g_loggedMiss = true;
        log("bike skeleton: NOT found (rider's skeleton 0x%08lX); bike parts off until it is", riderSkeleton);
    }
    if (a) g_loggedMiss = false;
    g_addr = a;
    return a;
}

} // namespace mp::thaw
