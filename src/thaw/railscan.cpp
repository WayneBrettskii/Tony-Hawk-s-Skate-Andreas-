// SkateAndreas: 8.32 rails: find reTHAWed's rail node records (R3a) and the F5 move-one-rail test (R3b). See railscan.h.

#include <windows.h>

#include "railscan.h"

#include "../common/link.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include <utility>

namespace mp::thaw {
namespace {

#ifndef MP_LOG_DIR
#error "MP_LOG_DIR must be set by CMakeLists.txt"
#endif

constexpr DWORD kWaitInLevelS = 15;   // after the level has been up this long
constexpr int kMaxTargets = 6;
constexpr int kMaxHitsPerTarget = 10;

struct Target {
    int node = 0, rail = 0, point = 0;
    float p[3] = {};
    DWORD bits[3] = {};
    unsigned long hits[kMaxHitsPerTarget] = {};
    int hitCount = 0;
};
Target g_t[kMaxTargets];
int g_targets = 0;

DWORD g_inLevelFor = 0;
unsigned long g_selfLo = 0, g_selfHi = 0; // our own DLL: its copies of the targets are not reTHAWed's
bool g_done = false;
void (*g_log)(const char*, ...) = nullptr;

bool Readable(const MEMORY_BASIC_INFORMATION& m)
{
    if (m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD p = m.Protect & 0xFF;
    return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READ ||
           p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

// rails_in_map.csv: node,rail,point,x,y,z. Takes rail 0 points 0..2, rails 1 and 2
// point 0, and the last node.
bool LoadTargets()
{
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\..\\extracted\\thaw_maps\\_railpool\\rails_in_map.csv", MP_LOG_DIR);
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        g_log("rail scan: can't open %s (error %lu); rebuild the map with the current map tool", path, GetLastError());
        return false;
    }
    static char buf[1 << 22]; // 4 MB is plenty for a few thousand rows; we only need the first ones and the last
    DWORD got = 0;
    LARGE_INTEGER size = {};
    GetFileSizeEx(f, &size);
    // first part
    ReadFile(f, buf, static_cast<DWORD>(sizeof(buf) / 2 - 1), &got, nullptr);
    buf[got] = 0;
    // last line: read the file's last 4 KB
    char tail[4096] = {};
    if (size.QuadPart > 4000) {
        LARGE_INTEGER at;
        at.QuadPart = size.QuadPart - 4000;
        SetFilePointerEx(f, at, nullptr, FILE_BEGIN);
        DWORD t = 0;
        ReadFile(f, tail, 4000, &t, nullptr);
        tail[t] = 0;
    }
    CloseHandle(f);

    auto parse = [](const char* line, Target& t) {
        float x, y, z;
        if (sscanf_s(line, "%d,%d,%d,%f,%f,%f", &t.node, &t.rail, &t.point, &x, &y, &z) != 6) return false;
        t.p[0] = x; t.p[1] = y; t.p[2] = z;
        memcpy(t.bits, t.p, sizeof(t.bits));
        return true;
    };
    char* save = nullptr;
    for (char* line = strtok_s(buf, "\n", &save); line && g_targets < kMaxTargets - 1; line = strtok_s(nullptr, "\n", &save)) {
        Target t;
        if (!parse(line, t)) continue; // header
        const bool want = (t.rail == 0 && t.point <= 2) || ((t.rail == 1 || t.rail == 2) && t.point == 0);
        if (want) g_t[g_targets++] = t;
    }
    // last complete line of the tail
    char* last = nullptr;
    for (char* line = strtok_s(tail, "\n", &save); line; line = strtok_s(nullptr, "\n", &save)) {
        Target t;
        if (parse(line, t)) last = line;
    }
    if (last) parse(last, g_t[g_targets++]);
    for (int i = 0; i < g_targets; ++i)
        g_log("rail scan: target %d = node %d (rail %d point %d) at %.3f %.3f %.3f (%08lX %08lX %08lX)", i, g_t[i].node,
              g_t[i].rail, g_t[i].point, g_t[i].p[0], g_t[i].p[1], g_t[i].p[2], g_t[i].bits[0], g_t[i].bits[1],
              g_t[i].bits[2]);
    return g_targets > 0;
}

// Pass 1: where are the target positions (x, y, z one after the other)?
void ScanRegionForTargets(unsigned long base, unsigned long end)
{
    __try {
        for (unsigned long a = base; a + 12 <= end; a += 4) {
            const DWORD v = *reinterpret_cast<const DWORD*>(a);
            for (int i = 0; i < g_targets; ++i) {
                Target& t = g_t[i];
                if (v != t.bits[0]) continue;
                const DWORD* q = reinterpret_cast<const DWORD*>(a);
                if (q[1] == t.bits[1] && q[2] == t.bits[2] && t.hitCount < kMaxHitsPerTarget &&
                    (a < g_selfLo || a >= g_selfHi))
                    t.hits[t.hitCount++] = a;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

template <class F> void ForEachRegion(F f)
{
    for (unsigned long a = 0x10000; a < 0x7FFF0000;) {
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof(m)) != sizeof(m)) break;
        const unsigned long base = reinterpret_cast<unsigned long>(m.BaseAddress);
        const unsigned long end = base + static_cast<unsigned long>(m.RegionSize);
        if (Readable(m)) f(base, end);
        if (end <= a) break;
        a = end;
    }
}

const char* RegionKind(unsigned long a)
{
    MEMORY_BASIC_INFORMATION m;
    if (VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof(m)) != sizeof(m)) return "?";
    return m.Type == MEM_IMAGE ? "image" : m.Type == MEM_MAPPED ? "mapped" : "private";
}

// ---- R3a-2 result (test 2, 20:55): every rail node is one 0x60-byte record:
//   +0x00 box min, +0x10 box max = min / max of this node's and the next node's
//         position (w = 1.0; the last node of a rail: both = its own position)
//   +0x20 position (w = 1.0)
//   +0x40 4, +0x44 the node name's checksum, +0x48 0x2A
//   +0x50 next node's record, +0x54 previous node's record (0 at the rail's ends)
//   +0x30..+0x3F, +0x4C, +0x58, +0x5C: left-over bytes, nothing uses them
// The block is sorted by the name checksum (for looking nodes up by name), so
// moving a rail = new positions + boxes; the order and the names stay. One
// object holds {block, count, count}. Nothing else points into the block.
//
// R3b test: F5 in our level moves one far-away 2-node rail to just in front of
// the skater (records only, nothing else is changed), then logs how far the
// skater is from it for 20 s: if the skater can grind it, the rail pool works.

bool LooksLikeRecord(unsigned long s)
{
    __try {
        if (s < 0x10000) return false;
        const float* f = reinterpret_cast<const float*>(s);
        const DWORD* d = reinterpret_cast<const DWORD*>(s);
        return f[3] == 1.0f && f[7] == 1.0f && f[11] == 1.0f && d[16] < 256 && d[18] == 0x2A;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

volatile LONG g_blockBase = 0, g_blockCount = 0; // set by the scan thread, used by the test
volatile LONG g_gen = 0;        // +1 each time the level goes away (its records are freed then)
volatile LONG g_scanGen = 0;    // g_gen when the running scan started
volatile LONG g_scanRunning = 0;

void FindArray()
{
    unsigned long rec = 0;
    for (int h = 0; h < g_t[0].hitCount && !rec; ++h) {
        const unsigned long hit = g_t[0].hits[h];
        for (unsigned long s : {hit - 0x20, hit, hit - 0x10})
            if (LooksLikeRecord(s) && *reinterpret_cast<const DWORD*>(s + 0x20) == g_t[0].bits[0]) {
                rec = s;
                break;
            }
    }
    if (!rec) {
        g_log("rail scan: no rail node record found for target 0");
        return;
    }
    unsigned long base = rec, end = rec;
    while (LooksLikeRecord(base - 0x60)) base -= 0x60;
    while (LooksLikeRecord(end)) end += 0x60;
    const unsigned long count = (end - base) / 0x60;
    g_log("rail scan: the block of rail node records runs 0x%08lX..0x%08lX = %lu records (the map has %d rail nodes)",
          base, end, count, g_t[g_targets - 1].node);
    if (g_scanGen != g_gen) {
        g_log("rail scan: the level went away during the scan; not using it");
        return;
    }
    InterlockedExchange(&g_blockCount, static_cast<LONG>(count));
    InterlockedExchange(&g_blockBase, static_cast<LONG>(base));
    g_log("rail test: ready: press F5 in the level to put a test rail in front of the skater");
}

DWORD WINAPI ScanThread(LPVOID)
{
    const DWORD t0 = GetTickCount();
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ScanThread), &self) && self) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(self);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const char*>(self) + dos->e_lfanew);
        g_selfLo = reinterpret_cast<unsigned long>(self);
        g_selfHi = g_selfLo + nt->OptionalHeader.SizeOfImage;
    }
    g_targets = 0;
    for (Target& t : g_t) t = Target();
    if (!LoadTargets()) {
        InterlockedExchange(&g_scanRunning, 0);
        return 0;
    }
    ForEachRegion([](unsigned long b, unsigned long e) { ScanRegionForTargets(b, e); });
    const DWORD t1 = GetTickCount();
    for (int i = 0; i < g_targets; ++i) {
        Target& t = g_t[i];
        g_log("rail scan: target %d (node %d): found %d time(s)", i, t.node, t.hitCount);
        for (int h = 0; h < t.hitCount; ++h)
            g_log("rail scan:  hit %d at 0x%08lX (%s memory)", h, t.hits[h], RegionKind(t.hits[h]));
    }
    FindArray(); // R3a-2
    g_log("rail scan: done (%lu ms)", t1 - t0);
    InterlockedExchange(&g_scanRunning, 0);
    return 0;
}


// ---- R3b test: move one rail ----------------------------------------------

constexpr int kTestKey = VK_F5;   // place the 25 m test rail
constexpr int kPointKey = VK_F4;  // place a degenerate "point" rail (Natas spin test)
constexpr float kAhead = 250.0f;   // the rail starts this far in front of the skater (about 6 m)
constexpr float kLength = 1000.0f; // and is this long (about 25 m)
constexpr float kHeight = 20.0f;   // this high above the skater's feet (about 50 cm, higher than kerbs)
constexpr DWORD kWatchMs = 20000;  // log the skater's distance to it this long

unsigned long g_testA = 0, g_testB = 0; // the moved rail's two records
float g_railA[3] = {}, g_railB[3] = {};
ULONGLONG g_watchUntil = 0;
int g_moves = 0;
int g_onSamples = 0, g_samples = 0; // while watching: samples with the skater on the test rail

float* F(unsigned long rec, int off) { return reinterpret_cast<float*>(rec + off); }
unsigned long P(unsigned long rec, int off) { return *reinterpret_cast<const unsigned long*>(rec + off); }

// A 2-node rail (first node: no previous, its next node has no next) at least
// 5000 units from the skater.
bool PickRail(unsigned long base, unsigned long count, const float* at, unsigned long& a, unsigned long& b)
{
    __try {
        for (unsigned long i = 0; i < count; ++i) {
            const unsigned long r = base + i * 0x60;
            const unsigned long n = P(r, 0x50);
            if (P(r, 0x54) || !n || n < base || n >= base + count * 0x60 || P(n, 0x50) || P(n, 0x54) != r) continue;
            const float* p = F(r, 0x20);
            const float dx = p[0] - at[0], dz = p[2] - at[2];
            if (dx * dx + dz * dz < 5000.0f * 5000.0f) continue;
            a = r;
            b = n;
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

void SetVec(float* d, const float* v) { d[0] = v[0]; d[1] = v[1]; d[2] = v[2]; d[3] = 1.0f; }

bool MoveRail(unsigned long a, unsigned long b, const float* pa, const float* pb)
{
    float mn[3], mx[3];
    for (int k = 0; k < 3; ++k) {
        mn[k] = pa[k] < pb[k] ? pa[k] : pb[k];
        mx[k] = pa[k] < pb[k] ? pb[k] : pa[k];
    }
    __try {
        SetVec(F(a, 0x20), pa);
        SetVec(F(a, 0x00), mn);
        SetVec(F(a, 0x10), mx);
        SetVec(F(b, 0x20), pb);
        SetVec(F(b, 0x00), pb);
        SetVec(F(b, 0x10), pb);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}


// ---- 8.32 R3c: the rail pool -------------------------------------------------
//
// A pool map's level holds only parked spare rails (the map tool's --rail-pool).
// Here we copy the GTA rails near the skater — read once from
// _railpool\all_rails.csv — into those records. Only positions and boxes are
// written (R3a-2); the record order, names and links stay. A rail is only ever
// moved into a slot the skater is far from, so a grind is never disturbed.

constexpr float kUnitsPerM = 39.3701f;    // THAW units per metre (the map tool's K)
constexpr float kPoolRadiusM = 150.0f;    // place the GTA rails within this of the skater
constexpr float kPoolMoveM = 40.0f;       // re-fill once the skater has moved this far
constexpr float kPoolParkX = -30000.0f;   // spare rails park here, below the map
constexpr float kPoolParkY = -6000.0f;

struct RealRail { float a[3], b[3]; };
struct PoolSlot { unsigned long a = 0, b = 0; int rail = -1; bool reserved = false; };

std::vector<RealRail> g_realRails;
int g_realSkipped = 0;
bool g_realTried = false, g_realOk = false;

std::vector<PoolSlot> g_slots;
int g_carFirst = 0; // first pool slot reserved for GTA car roof rails (8.33)
std::vector<int> g_mark, g_held;
int g_fillId = 0;
bool g_poolBuilt = false, g_poolOff = false;
unsigned long g_builtBase = 0;
LONG g_poolRailCount = 0;
float g_lastFill[3] = {};
bool g_haveLastFill = false;
int g_onRail = -1; // the placed rail the skater is on (index into g_realRails), or -1

bool LoadRealRails(void (*log)(const char*, ...))
{
    g_realTried = true;
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\..\\extracted\\thaw_maps\\_railpool\\all_rails.csv", MP_LOG_DIR);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) {
        log("rail pool: can't open %s; the pool stays off (build a pool map with --rail-pool)", path);
        return false;
    }
    char path2[MAX_PATH];
    _snprintf_s(path2, sizeof(path2), _TRUNCATE, "%s\\..\\extracted\\thaw_maps\\_railpool\\rails_in_map.csv", MP_LOG_DIR);
    FILE* g = nullptr;
    if (fopen_s(&g, path2, "r") == 0 && g) {
        LONG rows = -1; // header only
        char line[256];
        while (fgets(line, sizeof(line), g)) ++rows;
        if (rows > 0) g_poolRailCount = rows / 2;
        fclose(g);
    }
    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return false; } // header
    // 8.32 R3c: a long GTA rail (3+ nodes) is placed as consecutive two-node
    // pieces that share their endpoints, so the grind carries along the whole
    // rail the way a Tony Hawk rail chain does (if reTHAWed connects rails whose
    // ends touch). The CSV lists a rail's points in order, so consecutive rows
    // are adjacent points.
    int curRail = -1, rails = 0;
    float prev[3] = {};
    bool havePrev = false;
    while (fgets(line, sizeof(line), f)) {
        int node = 0, rail = 0, point = 0;
        float x = 0, y = 0, z = 0;
        if (sscanf_s(line, "%d,%d,%d,%f,%f,%f", &node, &rail, &point, &x, &y, &z) != 6) continue;
        if (rail != curRail) { curRail = rail; havePrev = false; ++rails; }
        if (havePrev) {
            RealRail r;
            for (int k = 0; k < 3; ++k) r.a[k] = prev[k];
            r.b[0] = x; r.b[1] = y; r.b[2] = z;
            g_realRails.push_back(r);
        }
        prev[0] = x; prev[1] = y; prev[2] = z;
        havePrev = true;
    }
    fclose(f);
    g_realOk = !g_realRails.empty();
    log("rail pool: %d GTA rails -> %zu two-node pieces (long rails split into pieces that share their ends); "
        "the pool map file lists %ld pool rails", rails, g_realRails.size(), (long)g_poolRailCount);
    return g_realOk;
}

bool BuildSlots(void (*log)(const char*, ...))
{
    g_slots.clear();
    const unsigned long base = static_cast<unsigned long>(g_blockBase);
    const unsigned long count = static_cast<unsigned long>(g_blockCount);
    if (!base || count < 2 || count > 0x1000000) return false;
    for (unsigned long i = 0; i < count; ++i) {
        const unsigned long r = base + i * 0x60;
        if (P(r, 0x54)) continue;                 // has a previous node: not a rail start
        const unsigned long n = P(r, 0x50);
        if (!n || n < base || n >= base + count * 0x60) continue;
        if (P(n, 0x54) != r || P(n, 0x50)) continue;  // must be exactly two nodes
        PoolSlot s;
        s.a = r;
        s.b = n;
        g_slots.push_back(s);
    }
    log("rail pool: %zu pool rails in the block (0x%08lX..0x%08lX)", g_slots.size(), base, base + count * 0x60);
    // 8.33: reserve the last few slots for GTA car roof rails (the fill must not touch them)
    g_carFirst = static_cast<int>(g_slots.size()) - kCarRailCars * 2;
    if (g_carFirst < 0) g_carFirst = 0;
    for (size_t s = static_cast<size_t>(g_carFirst); s < g_slots.size(); ++s) g_slots[s].reserved = true;
    return !g_slots.empty();
}

void WriteRail(unsigned long a, unsigned long b, const float* pa, const float* pb)
{
    float mn[3], mx[3];
    for (int k = 0; k < 3; ++k) { mn[k] = pa[k] < pb[k] ? pa[k] : pb[k]; mx[k] = pa[k] < pb[k] ? pb[k] : pa[k]; }
    __try {
        SetVec(F(a, 0x20), pa); SetVec(F(a, 0x00), mn); SetVec(F(a, 0x10), mx);
        SetVec(F(b, 0x20), pb); SetVec(F(b, 0x00), pb); SetVec(F(b, 0x10), pb);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void ParkRail(unsigned long a, unsigned long b, int slot)
{
    const float p[3] = {kPoolParkX, kPoolParkY, 50.0f * slot};
    WriteRail(a, b, p, p);
}

void PoolFill(const float* pos, void (*log)(const char*, ...))
{
    const size_t slots = g_slots.size();
    const float R = kPoolRadiusM * kUnitsPerM, R2 = R * R;
    static std::vector<std::pair<float, int>> cand;
    cand.clear();
    for (int i = 0; i < static_cast<int>(g_realRails.size()); ++i) {
        const RealRail& r = g_realRails[i];
        const float mx = (r.a[0] + r.b[0]) * 0.5f, mz = (r.a[2] + r.b[2]) * 0.5f;
        const float dx = pos[0] - mx, dz = pos[2] - mz;
        const float d2 = dx * dx + dz * dz;
        if (d2 <= R2) cand.push_back(std::make_pair(d2, i));
    }
    std::sort(cand.begin(), cand.end());
    const size_t keep = cand.size() < slots ? cand.size() : slots;
    ++g_fillId;
    if (g_mark.size() != g_realRails.size()) { g_mark.assign(g_realRails.size(), 0); g_held.assign(g_realRails.size(), 0); }
    for (size_t i = 0; i < keep; ++i) g_mark[cand[i].second] = g_fillId;
    // free the slots whose rail is no longer wanted (they are over the radius away, so never
    // under the skater: the wanted set is the nearest rails, which always contains his own)
    for (size_t s = 0; s < slots; ++s) {
        PoolSlot& sl = g_slots[s];
        if (sl.rail >= 0 && g_mark[sl.rail] != g_fillId) {
            ParkRail(sl.a, sl.b, static_cast<int>(s));
            sl.rail = -1;
        }
    }
    for (size_t s = 0; s < slots; ++s)
        if (g_slots[s].rail >= 0) g_held[g_slots[s].rail] = g_fillId; // still held
    // place the wanted rails that no slot holds yet
    size_t freeAt = 0;
    int moved = 0;
    for (size_t i = 0; i < keep; ++i) {
        const int idx = cand[i].second;
        if (g_held[idx] == g_fillId) continue;
        while (freeAt < slots && (g_slots[freeAt].rail >= 0 || g_slots[freeAt].reserved)) ++freeAt;
        if (freeAt >= slots) break;
        PoolSlot& sl = g_slots[freeAt++];
        WriteRail(sl.a, sl.b, g_realRails[idx].a, g_realRails[idx].b);
        sl.rail = idx;
        g_held[idx] = g_fillId;
        ++moved;
    }
    int placed = 0;
    for (size_t s = 0; s < slots; ++s)
        if (g_slots[s].rail >= 0) ++placed;
    log("rail pool: %d rails placed of %zu slots (%zu within %.0f m; %d changed)", placed, slots, cand.size(),
        kPoolRadiusM, moved);
}

} // namespace

void RailTestTick(LinkBlock* link, bool inOurLevel, const float* pos, const float* at, const float* vel,
                  bool logLine, void (*log)(const char*, ...))
{
    static bool wasDown = false, wasDown2 = false;
    const bool down = (GetAsyncKeyState(kTestKey) & 0x8000) != 0;
    const bool pressed = down && !wasDown;
    wasDown = down;
    const bool down2 = (GetAsyncKeyState(kPointKey) & 0x8000) != 0;
    const bool pressed2 = down2 && !wasDown2;
    wasDown2 = down2;
    if (!inOurLevel) {
        // the level's records are freed when it goes: forget them at once
        if (g_blockBase) {
            InterlockedExchange(&g_blockBase, 0);
            InterlockedIncrement(&g_gen);
            g_done = false;
        }
        g_testA = g_testB = 0;
        g_watchUntil = 0;
        if (link) InterlockedExchange(&link->thawTestRailOn, 0);
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (pressed || pressed2) {
        // F4 = a degenerate "point" rail (Natas spin test): ~10 cm long, at hydrant /
        // pole-top height, just in front of the skater.
        const float ahead = pressed2 ? 60.0f : kAhead;
        const float rlen = pressed2 ? 4.0f : kLength;
        const float rhgt = pressed2 ? 25.0f : kHeight;
        const unsigned long base = static_cast<unsigned long>(g_blockBase);
        const unsigned long count = static_cast<unsigned long>(g_blockCount);
        if (!base || !pos) {
            log("rail test (F5): not ready yet (the rail scan needs about 20 s in the level)");
            return;
        }
        // flat direction: the way the skater is moving, else the way he faces
        float fx = vel ? vel[0] : 0.0f, fz = vel ? vel[2] : 0.0f;
        if (fx * fx + fz * fz < 100.0f * 100.0f && at) {
            fx = at[0];
            fz = at[2];
        }
        const float len = sqrtf(fx * fx + fz * fz);
        if (len < 0.001f) {
            log("rail test (F5): can't tell which way the skater faces");
            return;
        }
        fx /= len;
        fz /= len;
        if (!g_testA && !PickRail(base, count, pos, g_testA, g_testB)) {
            log("rail test (F5): found no far-away 2-node rail to move");
            return;
        }
        const float y = pos[1] + rhgt;
        const float pa[3] = {pos[0] + fx * ahead, y, pos[2] + fz * ahead};
        const float pb[3] = {pa[0] + fx * rlen, y, pa[2] + fz * rlen};
        const float* old = F(g_testA, 0x20);
        const float oldA[3] = {old[0], old[1], old[2]};
        if (!MoveRail(g_testA, g_testB, pa, pb)) {
            log("rail test (F5): could not write the rail records");
            g_testA = g_testB = 0;
            return;
        }
        memcpy(g_railA, pa, sizeof(pa));
        memcpy(g_railB, pb, sizeof(pb));
        if (link) { // GTA shows it (a row of lights) while skate mode is on
            InterlockedExchange(&link->thawTestRailOn, 0);
            for (int k = 0; k < 3; ++k) {
                link->thawTestRail[k] = pa[k];
                link->thawTestRail[3 + k] = pb[k];
            }
            InterlockedExchange(&link->thawTestRailOn, 1);
        }
        g_watchUntil = now + kWatchMs;
        g_onSamples = g_samples = 0;
        ++g_moves;
        log("rail test #%d (F5): moved the rail with records 0x%08lX, 0x%08lX (checksum %08lX) from %.1f %.1f %.1f "
            "to %.1f %.1f %.1f .. %.1f %.1f %.1f | skater at %.1f %.1f %.1f, heading %.2f %.2f",
            g_moves, g_testA, g_testB, P(g_testA, 0x44), oldA[0], oldA[1], oldA[2], pa[0], pa[1], pa[2], pb[0], pb[1],
            pb[2], pos[0], pos[1], pos[2], fx, fz);
        return;
    }
    if (!g_watchUntil || !logLine || !pos) return;
    if (now > g_watchUntil) {
        log("rail test #%d: stopped watching (the rail stays where it is): skater ON THE RAIL in %d of %d samples%s",
            g_moves, g_onSamples, g_samples, g_onSamples >= 3 ? " = GROUND IT" : "");
        g_watchUntil = 0;
        return;
    }
    // distance from the skater to the rail line, and how far along it (0..1)
    float d[3], s[3];
    for (int k = 0; k < 3; ++k) {
        d[k] = g_railB[k] - g_railA[k];
        s[k] = pos[k] - g_railA[k];
    }
    const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    float t = dd > 0 ? (s[0] * d[0] + s[1] * d[1] + s[2] * d[2]) / dd : 0.0f;
    const float tc = t < 0 ? 0 : t > 1 ? 1 : t;
    float off[3];
    for (int k = 0; k < 3; ++k) off[k] = pos[k] - (g_railA[k] + d[k] * tc);
    const float dist = sqrtf(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]);
    const float speed = vel ? sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]) : 0.0f;
    // grinding: the skater on the line (sideways within ~25 cm, not below it), part way along, moving
    const float side = sqrtf(off[0] * off[0] + off[2] * off[2]);
    const bool on = side < 10.0f && off[1] > -4.0f && off[1] < 12.0f && t > 0.0f && t < 1.0f && speed > 50.0f;
    ++g_samples;
    if (on) ++g_onSamples;
    if (dist > 600.0f) return; // far away: no line
    log("rail test #%d: skater %.1f from the rail (%.1f above it), %.0f%% along it, speed %.0f%s", g_moves, dist,
        off[1], t * 100.0f, speed, on ? " | ON THE TEST RAIL" : "");
}


void RailPoolTick(bool inOurLevel, const float* pos, const float* vel, void (*log)(const char*, ...))
{
    if (!inOurLevel) {
        if (g_poolBuilt || !g_slots.empty()) {
            g_slots.clear();
            g_poolBuilt = false;
            g_poolOff = false;
            g_haveLastFill = false;
            g_onRail = -1;
        }
        return;
    }
    if (g_poolOff) return;
    if (!g_realTried) {
        if (!LoadRealRails(log)) { g_poolOff = true; return; }
    }
    if (g_realRails.empty() || !pos) return;
    const unsigned long base = static_cast<unsigned long>(g_blockBase);
    if (!base) return; // the scan has not found the block yet
    if (!g_poolBuilt || g_builtBase != base) {
        if (!BuildSlots(log)) return;
        if (g_poolRailCount > 0 && static_cast<LONG>(g_slots.size()) != g_poolRailCount) {
            log("rail pool: the level has %zu two-node rails but the pool map file lists %ld; pool off (load the "
                "matching pool map)", g_slots.size(), static_cast<long>(g_poolRailCount));
            g_slots.clear();
            g_poolBuilt = true;
            g_poolOff = true;
            return;
        }
        g_builtBase = base;
        g_poolBuilt = true;
        g_haveLastFill = false;
    }
    if (g_slots.empty()) return;
    float d2 = 0.0f;
    for (int k = 0; k < 3; ++k) { const float e = pos[k] - g_lastFill[k]; d2 += e * e; }
    const float move = kPoolMoveM * kUnitsPerM;
    if (!g_haveLastFill || d2 > move * move) {
        PoolFill(pos, log);
        memcpy(g_lastFill, pos, sizeof(g_lastFill));
        g_haveLastFill = true;
    }
    // 8.32: which placed rail is the skater on? (same test as the R3b probe: close
    // sideways, not below the rail, part way along, moving) - logs on / off, so a
    // grind that stops (e.g. at a palm in the middle of a hedge) can be seen.
    if (vel && !g_slots.empty()) {
        const float speed = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
        int on = -1;
        float best = 1e9f;
        for (size_t s = 0; s < g_slots.size(); ++s) {
            const int ri = g_slots[s].rail;
            if (ri < 0) continue;
            const RealRail& r = g_realRails[ri];
            float d[3], sv[3];
            for (int k = 0; k < 3; ++k) { d[k] = r.b[k] - r.a[k]; sv[k] = pos[k] - r.a[k]; }
            const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            const float t = dd > 0.0f ? (sv[0] * d[0] + sv[1] * d[1] + sv[2] * d[2]) / dd : 0.0f;
            if (t <= 0.0f || t >= 1.0f) continue;
            float off[3];
            for (int k = 0; k < 3; ++k) off[k] = pos[k] - (r.a[k] + d[k] * t);
            const float side = sqrtf(off[0] * off[0] + off[2] * off[2]);
            if (side < 10.0f && off[1] > -4.0f && off[1] < 12.0f && speed > 50.0f) {
                const float dist = side + fabsf(off[1]);
                if (dist < best) { best = dist; on = ri; }
            }
        }
        if (on != g_onRail) {
            if (on >= 0)
                log("rail pool: ON a placed rail (%.0f speed, at %.0f %.0f %.0f)", speed, pos[0], pos[1], pos[2]);
            else
                log("rail pool: left the placed rail");
            g_onRail = on;
        }
    }
}


void CarRailTick(LinkBlock* link, bool inOurLevel, const float* pos, void (*log)(const char*, ...))
{
    (void)log;
    if (!inOurLevel || !link || !g_poolBuilt) return;
    if (g_slots.empty() || g_carFirst >= static_cast<int>(g_slots.size())) return;
    static float pts[kCarRailCars * kCarRailPts * 3];
    int cars = ReadCarRails(link, pts, static_cast<int>(sizeof(pts) / sizeof(pts[0])));
    if (cars < 0) cars = 0;
    if (cars > kCarRailCars) cars = kCarRailCars;
    int used = 0;
    for (int c = 0; c < cars; ++c) {
        // reject a bad car (NaN / absurd coordinates) before touching the pool
        bool ok = true;
        for (int k = 0; k < kCarRailPts * 3; ++k) {
            const float f = pts[c * kCarRailPts * 3 + k];
            if (!(f > -5.0e6f && f < 5.0e6f)) ok = false;
        }
        if (!ok) continue;
        for (int r = 0; r < 2; ++r) {
            const int slot = g_carFirst + c * 2 + r;
            if (slot >= static_cast<int>(g_slots.size())) break;
            const float* a = pts + (c * kCarRailPts + r * 2) * 3;
            WriteRail(g_slots[slot].a, g_slots[slot].b, a, a + 3);
            ++used;
        }
    }
    // park the reserved slots that have no car this pass - but never move one the
    // skater is near (that teleport is what turns his physics to NaN)
    for (int slot = g_carFirst + used; slot < static_cast<int>(g_slots.size()); ++slot) {
        if (pos) {
            __try {
                const float* cur = reinterpret_cast<const float*>(g_slots[slot].a + 0x20);
                const float dx = pos[0] - cur[0], dy = pos[1] - cur[1], dz = pos[2] - cur[2];
                if (dx * dx + dy * dy + dz * dz < 4.0f * 4.0f) continue;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        ParkRail(g_slots[slot].a, g_slots[slot].b, slot);
    }
}


void RailScanSecond(bool inOurLevel, void (*log)(const char*, ...))
{
    if (!inOurLevel) {
        g_inLevelFor = 0;
        if (g_done || g_blockBase) { // level gone: scan again next time it is up
            InterlockedExchange(&g_blockBase, 0);
            InterlockedIncrement(&g_gen);
            g_done = false;
        }
        return;
    }
    if (g_done || g_scanRunning) return;
    if (++g_inLevelFor < kWaitInLevelS) return;
    g_done = true;
    g_log = log;
    InterlockedExchange(&g_scanGen, g_gen);
    InterlockedExchange(&g_scanRunning, 1);
    log("rail scan: GTA map up for %lu s: looking for its rail node records in reTHAWed's memory (read only)",
        g_inLevelFor);
    HANDLE h = CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else {
        InterlockedExchange(&g_scanRunning, 0);
        log("rail scan: could not start its thread (error %lu)", GetLastError());
    }
}

} // namespace mp::thaw
