// my-passthrough: find reTHAWed's bike skeleton. LOG ONLY. See bikefind.h.

#include <windows.h>

#include <cmath>

#include "bikefind.h"
#include "pose.h"

namespace mp::thaw {
namespace {

constexpr DWORD kScanAfterMs = 6000; // on the bike this long: scan (the switch takes ~3 s)
constexpr int kMaxFound = 48;
constexpr int kMaxTracked = 8;
constexpr int kMaxTrackBones = 64;

struct Found {
    unsigned long addr = 0;
    int count = 0;
};

struct Tracked {
    unsigned long addr = 0;
    int count = 0;
    float first[kMaxTrackBones][9] = {}; // each bone's turn in the first sample
    float maxAngle[kMaxTrackBones] = {}; // this second
    float allMax[kMaxTrackBones] = {};   // since tracking started
    bool haveFirst = false;
    bool dead = false;
};

bool g_on = false, g_scanned = false;
DWORD g_onSince = 0;
Tracked g_tr[kMaxTracked];
int g_trCount = 0;
unsigned g_passes = 0;

bool Readable(unsigned long p) { return p >= 0x00010000u && p < 0x7FFF0000u && (p & 3) == 0; }

// The same layout as the rider's skeleton (pose.cpp): bone count +0x14, bone
// list +0x18 (0x1C bytes each, first = name checksum), matrices +0x04 (64
// bytes each), bind data [[+0x08]+0x04]+0x28. Checked strictly so random
// memory doesn't pass.
bool LooksLikeSkeleton(unsigned long s, int& countOut)
{
    __try {
        const int count = *reinterpret_cast<const int*>(s + 0x14);
        if (count < 3 || count > 128) return false;
        const unsigned long bones = *reinterpret_cast<const unsigned long*>(s + 0x18);
        const unsigned long mats = *reinterpret_cast<const unsigned long*>(s + 0x04);
        const unsigned long bind = *reinterpret_cast<const unsigned long*>(s + 0x08);
        if (!Readable(bones) || !Readable(mats) || !Readable(bind)) return false;
        const unsigned long b2 = *reinterpret_cast<const unsigned long*>(bind + 4);
        if (!Readable(b2)) return false;
        const unsigned long bindArr = *reinterpret_cast<const unsigned long*>(b2 + 0x28);
        if (!Readable(bindArr)) return false;
        unsigned long seen[128];
        for (int i = 0; i < count; ++i) {
            const unsigned long c = *reinterpret_cast<const unsigned long*>(bones + i * 0x1C);
            if (c == 0 || c == 0xFFFFFFFFu) return false;
            for (int k = 0; k < i; ++k)
                if (seen[k] == c) return false;
            seen[i] = c;
            // each bone matrix: 16 sensible numbers
            const float* m = reinterpret_cast<const float*>(mats + i * 64);
            for (int k = 0; k < 16; ++k)
                if (!(std::fabs(m[k]) < 1.0e5f)) return false;
        }
        countOut = count;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Every committed, writable block of reTHAWed's memory, 4 bytes at a time.
int ScanAll(Found* out, int max, unsigned long& bytesScanned)
{
    int n = 0;
    bytesScanned = 0;
    unsigned long a = 0x00010000u;
    MEMORY_BASIC_INFORMATION mbi;
    while (a < 0x7FFF0000u && VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const unsigned long base = reinterpret_cast<unsigned long>(mbi.BaseAddress);
        const unsigned long size = static_cast<unsigned long>(mbi.RegionSize);
        const DWORD prot = mbi.Protect & 0xFF;
        const bool rw = prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE;
        if (mbi.State == MEM_COMMIT && rw && !(mbi.Protect & PAGE_GUARD) && size >= 0x20) {
            bytesScanned += size;
            __try {
                for (unsigned long s = base; s + 0x20 <= base + size; s += 4) {
                    const int count = *reinterpret_cast<const int*>(s + 0x14);
                    if (count < 3 || count > 128) continue;
                    const unsigned long bones = *reinterpret_cast<const unsigned long*>(s + 0x18);
                    const unsigned long mats = *reinterpret_cast<const unsigned long*>(s + 0x04);
                    if (!Readable(bones) || !Readable(mats)) continue;
                    int c = 0;
                    if (LooksLikeSkeleton(s, c) && n < max) out[n++] = Found{s, c};
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        if (base + size <= a) break;
        a = base + size;
    }
    return n;
}

const char* NameOf(unsigned long c)
{
    const char* n = BoneName(c);
    return n ? n : "?";
}

void LogSkeleton(void (*log)(const char*, ...), int i, const Found& f, bool rider)
{
    __try {
        const unsigned long bones = *reinterpret_cast<const unsigned long*>(f.addr + 0x18);
        const unsigned long mats = *reinterpret_cast<const unsigned long*>(f.addr + 0x04);
        log("bike find: skeleton %d at 0x%08lX: %d bones%s", i, f.addr, f.count, rider ? " (the rider's)" : "");
        if (rider || f.count > 40) return; // the rider's we know; big ones are people / other models
        for (int b = 0; b < f.count; ++b) {
            const unsigned long c = *reinterpret_cast<const unsigned long*>(bones + b * 0x1C);
            const float* m = reinterpret_cast<const float*>(mats + b * 64);
            log("bike find:   #%d 0x%08lX %s: pos %.1f %.1f %.1f | right %.2f %.2f %.2f | up %.2f %.2f %.2f", b, c,
                NameOf(c), m[12], m[13], m[14], m[0], m[1], m[2], m[4], m[5], m[6]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("bike find: skeleton %d: read failed", i);
    }
}

float AngleOf(const float* a, const float* m4) // between rotation a (3x3) and m4's rotation (rows, normalised here)
{
    float b[9];
    for (int r = 0; r < 3; ++r) {
        const float* row = m4 + r * 4;
        const float l = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
        for (int c = 0; c < 3; ++c) b[r * 3 + c] = l > 1e-6f ? row[c] / l : 0.0f;
    }
    float tr = 0.0f;
    for (int r = 0; r < 3; ++r) tr += a[r * 3] * b[r * 3] + a[r * 3 + 1] * b[r * 3 + 1] + a[r * 3 + 2] * b[r * 3 + 2];
    float c = (tr - 1.0f) * 0.5f;
    if (c > 1.0f) c = 1.0f;
    if (c < -1.0f) c = -1.0f;
    return std::acos(c) * 57.29578f;
}

void Track(Tracked& t)
{
    if (t.dead) return;
    __try {
        if (*reinterpret_cast<const int*>(t.addr + 0x14) != t.count) { t.dead = true; return; }
        const unsigned long mats = *reinterpret_cast<const unsigned long*>(t.addr + 0x04);
        const int n = t.count < kMaxTrackBones ? t.count : kMaxTrackBones;
        for (int b = 0; b < n; ++b) {
            const float* m = reinterpret_cast<const float*>(mats + b * 64);
            if (!t.haveFirst) {
                for (int r = 0; r < 3; ++r) {
                    const float* row = m + r * 4;
                    const float l = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
                    for (int c = 0; c < 3; ++c) t.first[b][r * 3 + c] = l > 1e-6f ? row[c] / l : 0.0f;
                }
                continue;
            }
            const float a = AngleOf(t.first[b], m);
            if (a > t.maxAngle[b]) t.maxAngle[b] = a;
            if (a > t.allMax[b]) t.allMax[b] = a;
        }
        t.haveFirst = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        t.dead = true;
    }
}

} // namespace

void BikeFindTick(bool onBike, unsigned long riderSkeleton, void (*log)(const char*, ...))
{
    const DWORD now = GetTickCount();
    if (!onBike) {
        g_on = false;
        g_scanned = false;
        g_trCount = 0;
        return;
    }
    if (!g_on) {
        g_on = true;
        g_onSince = now;
    }
    if (!g_scanned && now - g_onSince >= kScanAfterMs) {
        g_scanned = true;
        static Found found[kMaxFound];
        unsigned long bytes = 0;
        const DWORD t0 = GetTickCount();
        const int n = ScanAll(found, kMaxFound, bytes);
        log("bike find: scanned %lu MB of reTHAWed's memory in %lu ms: %d skeleton(s)%s", bytes >> 20,
            GetTickCount() - t0, n, n >= kMaxFound ? " (list full)" : "");
        g_trCount = 0;
        for (int i = 0; i < n; ++i) {
            const bool rider = found[i].addr == riderSkeleton;
            LogSkeleton(log, i, found[i], rider);
            if (!rider && found[i].count <= 40 && g_trCount < kMaxTracked) {
                Tracked& t = g_tr[g_trCount++];
                t = Tracked{};
                t.addr = found[i].addr;
                t.count = found[i].count;
            }
        }
        log("bike find: following %d small skeleton(s) that aren't the rider's; do bar spins, crank flips, "
            "tailwhips now", g_trCount);
    }
    for (int i = 0; i < g_trCount; ++i) Track(g_tr[i]);
    ++g_passes;
}

void BikeFindSecond(void (*log)(const char*, ...))
{
    if (!g_on || !g_scanned) return;
    for (int i = 0; i < g_trCount; ++i) {
        Tracked& t = g_tr[i];
        if (t.dead) {
            log("bike find: skeleton 0x%08lX gone", t.addr);
            continue;
        }
        // the bones that turned most this second (from their first sample)
        int best[4] = {-1, -1, -1, -1};
        const int n = t.count < kMaxTrackBones ? t.count : kMaxTrackBones;
        for (int b = 0; b < n; ++b)
            for (int k = 0; k < 4; ++k)
                if (best[k] < 0 || t.maxAngle[b] > t.maxAngle[best[k]]) {
                    for (int j = 3; j > k; --j) best[j] = best[j - 1];
                    best[k] = b;
                    break;
                }
        if (best[0] >= 0 && t.maxAngle[best[0]] >= 20.0f)
            log("bike find: skeleton 0x%08lX (%d bones) most turned this second: #%d %.0f deg, #%d %.0f, #%d %.0f, "
                "#%d %.0f", t.addr, t.count, best[0], t.maxAngle[best[0]], best[1], best[1] >= 0 ? t.maxAngle[best[1]] : 0.0f,
                best[2], best[2] >= 0 ? t.maxAngle[best[2]] : 0.0f, best[3], best[3] >= 0 ? t.maxAngle[best[3]] : 0.0f);
        for (int b = 0; b < n; ++b) t.maxAngle[b] = 0.0f;
    }
    g_passes = 0;
}

} // namespace mp::thaw
