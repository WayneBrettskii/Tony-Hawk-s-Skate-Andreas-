// SkateAndreas: 8.32 research, LOG ONLY. See statefind.h.

#include <windows.h>

#include "statefind.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace mp::thaw {
namespace {

constexpr int kKey = VK_F3;
constexpr float kNearXZ = 2500.0f; // candidate must be within this of the skater in X/Z (~60 m)
constexpr float kMinMove = 15.0f;  // only report candidates that moved at least this
constexpr long kMaxRaw = 1000000;
constexpr unsigned long kWinBytes = 0x40000000u; // effectively no window: scan all memory

struct P {
    float x = 0, y = 0, z = 0;
};

struct Raw {
    unsigned long addr;
    float x, y, z;
};
Raw g_raw[kMaxRaw];
long g_rawCount = 0;

std::unordered_map<unsigned long, P> g_a; // candidates from scan A
bool g_haveA = false;
P g_skaterA;

bool Finite(float f) { return f > -1e9f && f < 1e9f && f == f; }

bool ReadableRegion(const MEMORY_BASIC_INFORMATION& m)
{
    if (m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD p = m.Protect & 0xFF;
    return p == PAGE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READWRITE ||
           p == PAGE_EXECUTE_WRITECOPY;
}

// No C++ objects here, so __try is allowed.
void ScanRegionRaw(unsigned long base, unsigned long end, float sx, float sz)
{
    __try {
        for (unsigned long a = base; a + 12 <= end; a += 4) {
            const float x = *reinterpret_cast<const float*>(a);
            if (!Finite(x)) continue;
            const float dx = x - sx;
            if (dx < -kNearXZ || dx > kNearXZ) continue;
            const float z = *reinterpret_cast<const float*>(a + 8);
            if (!Finite(z)) continue;
            const float dz = z - sz;
            if (dx * dx + dz * dz > kNearXZ * kNearXZ) continue;
            const float y = *reinterpret_cast<const float*>(a + 4);
            if (!Finite(y)) continue;
            if (g_rawCount < kMaxRaw) g_raw[g_rawCount++] = Raw{a, x, y, z};
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

struct Mover {
    float m;
    unsigned long addr;
    P a, b;
};

// No C++ objects here, so __try is allowed.
bool ReadSkaterPos(unsigned long skater, float out[3])
{
    __try {
        const float* sp = reinterpret_cast<const float*>(skater + 0x68);
        out[0] = sp[0];
        out[1] = sp[1];
        out[2] = sp[2];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Scan(unsigned long skater, void (*log)(const char*, ...))
{
    float sp[3];
    if (!ReadSkaterPos(skater, sp)) return;
    const float sx = sp[0], sy = sp[1], sz = sp[2];
    if (!g_haveA) {
        g_skaterA = P{sx, sy, sz};
        g_a.clear();
    }

    g_rawCount = 0;
    const unsigned long lo = (skater > kWinBytes) ? skater - kWinBytes : 0x10000u;
    const unsigned long hi = skater + kWinBytes;
    for (unsigned long a0 = 0x10000; a0 < 0x7FFF0000u;) {
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery(reinterpret_cast<void*>(a0), &m, sizeof(m)) != sizeof(m)) break;
        const unsigned long base = reinterpret_cast<unsigned long>(m.BaseAddress);
        const unsigned long end = base + static_cast<unsigned long>(m.RegionSize);
        if (end > lo && base < hi && ReadableRegion(m)) {
            const unsigned long s = base > lo ? base : lo;
            const unsigned long e = end < hi ? end : hi;
            ScanRegionRaw(s, e, sx, sz);
        }
        if (end <= a0) break;
        a0 = end;
    }

    if (!g_haveA) {
        for (long i = 0; i < g_rawCount; ++i) g_a[g_raw[i].addr] = P{g_raw[i].x, g_raw[i].y, g_raw[i].z};
        log("camfind: scan A: %ld candidate positions near the skater (%.0f %.0f %.0f); now rotate the camera and "
            "press F3 again", g_rawCount, g_skaterA.x, g_skaterA.y, g_skaterA.z);
        g_haveA = true;
        return;
    }

    std::vector<Mover> moved;
    for (long i = 0; i < g_rawCount; ++i) {
        auto it = g_a.find(g_raw[i].addr);
        if (it == g_a.end()) continue;
        const float mx = g_raw[i].x - it->second.x, my = g_raw[i].y - it->second.y, mz = g_raw[i].z - it->second.z;
        const float dm = sqrtf(mx * mx + my * my + mz * mz);
        if (dm < kMinMove) continue;
        // the new value must still be a plausible point near the skater (the camera
        // orbits him): this drops scratch buffers that flip to absurd values.
        const float nx = g_raw[i].x - sx, nz = g_raw[i].z - sz;
        if (nx < -kNearXZ || nx > kNearXZ || nz < -kNearXZ || nz > kNearXZ) continue;
        if (fabsf(g_raw[i].y - sy) > 4000.0f) continue;
        moved.push_back(Mover{dm, g_raw[i].addr, it->second, P{g_raw[i].x, g_raw[i].y, g_raw[i].z}});
    }
    std::sort(moved.begin(), moved.end(), [](const Mover& a, const Mover& b) { return a.m > b.m; });
    log("camfind: scan B: %ld candidates, %zu moved >= %.0f units (skater was %.0f %.0f %.0f)", g_rawCount,
        moved.size(), kMinMove, g_skaterA.x, g_skaterA.y, g_skaterA.z);
    const size_t n = moved.size() < 40 ? moved.size() : 40;
    for (size_t i = 0; i < n; ++i)
        log("camfind:  moved %.0f: 0x%08lX  (%.0f %.0f %.0f) -> (%.0f %.0f %.0f)", moved[i].m, moved[i].addr,
            moved[i].a.x, moved[i].a.y, moved[i].a.z, moved[i].b.x, moved[i].b.y, moved[i].b.z);
    g_haveA = false;
}

} // namespace

void StateFindTick(unsigned long skater, void (*log)(const char*, ...))
{
    static bool was = false;
    if (!skater) return;
    const bool down = (GetAsyncKeyState(kKey) & 0x8000) != 0;
    if (down && !was) Scan(skater, log);
    was = down;
}

} // namespace mp::thaw
