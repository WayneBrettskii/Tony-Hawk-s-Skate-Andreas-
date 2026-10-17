// SkateAndreas: 8.51 skitching route A - find the map's reTHAWed vehicles in memory (step 2),
// and let one follow the nearest moving GTA car (step 4). See vehscan.h.

#include <windows.h>
#include <tlhelp32.h>

#include "vehscan.h"

#include "../common/link.h"
#include "../common/paths.h"
#include "skater.h" // ReadGameState: is our level still loaded?

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace mp::thaw {
namespace {

constexpr DWORD kWaitInLevelS = 25;   // after the level has been up this long (the rail scan runs at 15 s)
constexpr int kMaxCars = 16;
constexpr int kMaxHits = 48;          // copies of one car's position we keep
constexpr float kTolXZ = 40.0f;       // ~1 m: a parked car settles a little
constexpr float kTolY = 400.0f;       // ~10 m
constexpr float kApplyTol = 4.0f;     // a copy must still hold what we last wrote (or the start place)
constexpr int kFollowKey = VK_F2;     // follow mode on / off
constexpr int kNoseKey = VK_F1;       // turn the car's nose by 90 degrees (which row is the nose is not known yet)
constexpr float kCarDrop = 24.0f;     // GTA gives the car's middle; a reTHAWed vehicle stands on its origin (~0.6 m lower)
constexpr DWORD kNoCarParkMs = 400;   // no GTA car this long: the proxy goes back to its own spot (1500 until test 25: "grabbed the air")
constexpr float kKeepNear = 400.0f;   // ~10 m: the skater this close to the test car keeps it on the same GTA car
constexpr float kParkWaitAlong = 140.0f; // test 34: inches from the proxy's centre along its length: he is on / at it
constexpr float kParkWaitSide = 80.0f;   //   ... to either side of its centre line
constexpr float kParkWaitBelow = 20.0f;  //   ... and no more than this below its base
constexpr unsigned long kOurLevel = 0x9F014B98ul; // load_MPGTALSE

struct Copy {
    unsigned long a = 0;              // address of the x, y, z triple
    int rows = 0;                     // 0 = position only; -1 = a rotation (3 rows) before it; +1 = after it;
                                      // 2 = the object's own turn, 0x74 bytes on (test 8, see FindRows);
                                      // 3 = the turn that one is copied from, 0x20 bytes on (test 9)
    float r0[3][3] = {};              // that rotation as found
};

struct Car {
    float p[3] = {};                  // where the map tool put it (THAW units)
    Copy c[kMaxHits];
    int hits = 0;
    float cur[3] = {};                // where its copies are now (what we last wrote)
    float yaw = 0.0f;                 // the turn we last wrote (radians, from the start rotation)
};

Car g_cars[kMaxCars];
int g_carCount = 0;
int g_fc = 0;                         // the test car: 2 when the map has 3 (its turn values are easy to tell apart)
Car& TestCar() { return g_cars[g_fc]; }

// 8.51 turn test 4 (2026-10-15): our turned rotation matrices were put back by reTHAWed within
// a second (6 of 7), so it rebuilds them from a turn kept elsewhere. Candidates near the test
// car's copies: a quaternion (two of four floats about 0, the other two the car's half-angle
// sine and cosine, |0.924| and |0.383| for car 2, which stands 225 or 135 degrees round) and a
// plain angle in radians. F1 then turns these too, and the 1 s check shows whether it sticks.
constexpr int kMaxSrc = 32;
struct TurnSrc {
    unsigned long a = 0;  // address of the first float
    int kind = 0;         // 1 = quaternion (4 floats), 2 = angle (1 float)
    int sSlot = 0, cSlot = 0; // quaternion: which floats hold the sine (y) and cosine (w) of the half angle
    float v0[4] = {};     // as found
};
TurnSrc g_src[kMaxSrc];
int g_srcCount = 0;
volatile LONG g_scanState = 0;        // 0 = not yet, 1 = running, 2 = done
volatile LONG g_levelGen = 0;         // +1 each time the level goes
LONG g_scanGen = 0;
DWORD g_inLevelFor = 0;
unsigned long g_selfLo = 0, g_selfHi = 0;
void (*g_log)(const char*, ...) = nullptr;
void CopyMem(void* to, DWORD from, size_t n, bool& ok); // below (watchdog)
void CompareTurns();                                    // below
bool g_followKeyWas = false, g_noseKeyWas = false;
bool g_followPending = false;         // F2 pressed before the scan was done (test 22)

// Follow mode: our thread decides where the test car goes; reTHAWed's game thread writes it.
SRWLOCK g_lock = SRWLOCK_INIT;
volatile LONG g_follow = 0;
int g_noseQuarter = 0;                // 0..3: extra turn of 0, 90, 180, 270 degrees
struct Target { float pos[3]; float yaw; LONG version; LONG gen; };
volatile LONG g_applied = 0, g_skippedCopies = 0, g_refused = 0;
ULONGLONG g_nextLog = 0;
DWORD g_turnCheckAt = 0;              // our thread: check the rotations this long after a test move

// 8.51 step 5, smooth following (user 2026-10-16: "proceed to next steps"). Before: each new GTA
// sample was written once, so the car stood still between samples and jumped to the next one.
// Now our thread keeps the last sample (position, velocity in THAW units per second, heading,
// the time it arrived), and reTHAWed's game thread places the car every frame where that sample
// says it is now: position + velocity x time since (at most kPredictMax ahead). A new sample's
// difference from where we had the car is not snapped but faded out over kBlendS (position,
// and the heading the short way round); a big jump (> kSnapDist, or > 45 degrees, or another
// car) snaps. Under g_lock.
constexpr double kPredictMax = 0.10;  // s: no further ahead than this (0.25 until the test-26 video: a braking car
                                      // was overrun by up to a car length and CJ ended up inside it)
constexpr double kBlendS = 0.10;      // s: a new sample's difference fades out over this
constexpr float kSnapDist = 80.0f;    // inches (2 m): more than this jumps
constexpr double kWriteEvery = 0.004; // s: the controller hook can run several times a frame
// 8.51 step 6 (test 18: the hold was inside a limo, a metre beside a motorbike, and traffic
// braked for CJ): reTHAWed holds the skater at the proxy's back corner, measured at kHoldBack
// behind its centre and kHoldSide to one side (tests 15-16, this proxy model). The proxy's centre
// is put kHoldBack - (the GTA car's rear, gtaSkitchBack) along the nose from the GTA car's
// centre, so the hold is on the GTA car's rear bumper; once he holds, it slides kHoldSide the
// other way (over kSlideS) so he is on the car's centre line. While he holds, thawSkitching = 1.
constexpr float kHoldBack = 93.0f;    // inches, measured
constexpr float kHoldSide = 42.0f;    // inches, measured
constexpr double kSlideS = 0.25;      // s for the full sideways slide
constexpr float kDefaultBack = 87.0f; // 2.2 m when the GTA side has no number
float g_shift = 0.0f;                 // (step 6 slide, replaced by the steer below; kept for the log)
volatile LONG g_holdSide = 0;         // -1 left, +1 right, 0 not holding (our thread)
volatile LONG g_heldSlot = -1;        // 8.52: the proxy he holds (index into g_cars / g_slots), -1 = none
// 8.51 step 7 (test 20 + user: "even when I put the meter back to the center he would stay on the
// side"): in a turn reTHAWed keeps the skater where he was instead of turning him with the car, so
// after a 25-35 degree turn he hangs 40-55 in to the side. Steer: while he holds, our thread
// compares where he is with a point kGap behind the GTA car's rear bumper, on its centre line,
// and moves the proxy (and him with it) towards it: g_steer += error x kSteerGain x dt, at most
// kSteerMax. The game thread eases the proxy to it (kSteerRate). Error over kLoseDist for 0.5 s
// = he has let go / fallen: the steer goes back to 0.
constexpr float kGap = 20.0f;         // inches behind the bumper (10 until the test-26 video)
constexpr float kSteerGain = 6.0f;    // 1/s (4 in test 21: ~0.5 s at the corner after the grab)
constexpr float kSteerMax = 200.0f;   // inches
constexpr float kSteerRate = 150.0f;  // inches per second, game thread (400 until test 26: the big pulls stuttered)
constexpr float kSteerAlongMax = 40.0f; // test 26: along the nose the steer only trims (reTHAWed drops him back now and then)
constexpr float kSteerDead = 4.0f;    // inches: errors smaller than this are left alone
constexpr float kLoseDist = 120.0f;   // inches
constexpr float kOwnSpeedLetGo = 40.0f; // in/s: test 28: his own speed reads 0 while he skitches; above this twice = let go
constexpr float kSkitchMoveMin = 60.0f; // in/s: his position must move at least this fast for speed 0 to mean a grab
volatile float g_steer[2] = {};       // world x / z, inches (our thread)
// 8.51 step 8 (test 23: "still got turned a weird direction when I found a limo"): like his place,
// reTHAWed does not turn the held skater when the proxy turns, so after a turn he faces the old
// way. While he holds, the game thread turns the skater object's rows (+0x88, the same layout as
// the vehicle object: row 0 right, row 1 up, row 2 nose) about the up axis so his nose is the GTA
// car's. Logged: the largest correction in each half second (0 would mean reTHAWed turns him itself).
constexpr unsigned long kSkaterRows = 0x88;
volatile unsigned long g_skater = 0;
volatile float g_faceFixMax = 0.0f;   // degrees, the largest correction since the last log
volatile LONG g_faceFixes = 0;
volatile float g_faceFixLast = 0.0f;  // test 32: the last correction (degrees, signed), for the stall log
float g_steerApplied[2] = {};         // game thread
struct Track {
    bool valid;
    LONG id;
    float fwd[3], back;               // GTA car's nose (horizontal, unit) and rear bumper behind its centre (inches)
    float rise;                       // test 24: inches up per inch along the nose (the car's slope; uphill > 0)
    float pos[3], vel[3], yaw;        // yaw: relative to the car's start heading, as SetTarget takes it
    float err[3], errYaw;             // where we had the car minus the new sample, faded out over kBlendS
    double t;                         // when the sample arrived
};
// 8.52 step 2: one slot per proxy (g_cars[i] <-> g_slots[i]): its track, its target, its GTA car.
// Track and target under g_lock; the rest is our thread's.
struct Slot {
    Track track;
    Target target;
    LONG appliedVersion;
    LONG carId;                       // the GTA car it follows, -1 = parked (or never used)
    DWORD lastSeen;                   // when its car was last wanted (GetTickCount)
    float lastCp[3];                  // the last sample handed to the track (to tell a new one)
    LONG lastId;
    bool waitLogged;                  // test 34: "would park, but the skater is on it" logged
};
Slot g_slots[kMaxCars];
void ResetSlots()
{
    for (int i = 0; i < kMaxCars; ++i) {
        g_slots[i] = Slot{};
        g_slots[i].carId = -1;
        g_slots[i].lastId = -2;
    }
}
double g_lastWrite = 0.0;
volatile LONG g_samples = 0, g_snaps = 0;
float g_errSum = 0.0f;                // inches, summed over the samples of the last second
LONG g_errN = 0;

double NowSec()
{
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart);
}

float WrapPi(float a)
{
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

// Where a track puts its car at time now (lock held).
void TrackAt(const Track& tr, double now, float* p, float& yaw)
{
    double dt = now - tr.t;
    if (dt < 0.0) dt = 0.0;
    if (dt > kPredictMax) dt = kPredictMax;
    const float w = dt >= kBlendS ? 0.0f : static_cast<float>(1.0 - dt / kBlendS);
    for (int k = 0; k < 3; ++k) p[k] = tr.pos[k] + tr.vel[k] * static_cast<float>(dt) + tr.err[k] * w;
    yaw = tr.yaw + tr.errYaw * w;
}

// 8.51 step 4, first test (2026-10-15): reTHAWed froze about 2 s into a skitch on a following
// car (its game thread stopped; no crash report). Watchdog: when reTHAWed's game thread has
// not been through the controller hook for kStuckMs while our level is up, look at where it
// is (suspend it for a moment, copy its registers and the top of its stack, resume it; only
// then log, so nothing it might hold is needed while it is stopped). A few samples show the
// loop it is stuck in, by module + offset.
constexpr DWORD kStuckMs = 2500;
constexpr int kStuckSamples = 4;
volatile LONG g_gameThreadId = 0;
volatile LONG g_lastGameTick = 0;
int g_stuckSamples = 0;
DWORD g_nextStuckSample = 0;

// 8.51 turn test 5 (2026-10-15): the turn candidates changed nothing; reTHAWed still put 6 of 7
// turned matrices back. Write watch: after the 1 s check, up to four of the copies that were put
// back get a hardware write watch (the CPU's debug registers, on every reTHAWed thread but ours)
// for kWatchMs. Each write there stops the writer for a moment: an exception handler notes the
// code address that wrote and its registers (no logging inside it), and lets it go on. Then the
// watches are cleared and the writers logged: the code that rebuilds the turn, and from where.
// Test 6 (2026-10-15): code at THAW.exe+0x62FA wrote copy 1 every frame (from a turn on the stack),
// but the hits on copies 5-7 (THAW.exe+0x2376A) look like reads. Test 7: the second F1 arms the
// watch at once, on the copies the first one saw put back, so a one-off put-back is caught too;
// each different writer (code address + registers) is kept, with the code bytes around it as
// they are in memory and the raw debug status.
constexpr DWORD kWatchMs = 2000;
constexpr int kMaxWatchHits = 24;
struct WatchHit { DWORD eip, tid; int which; DWORD r[8]; DWORD stack[40]; BYTE code[40]; DWORD dr6, dr7, atMs; volatile LONG count; };
WatchHit g_watchHits[kMaxWatchHits];
volatile LONG g_watchN = 0, g_watchOwn = 0, g_watchArmed = 0;
unsigned long g_watchAddr[4] = {};
int g_watchCopy[4] = {};
int g_watchCount = 0;
DWORD g_watchUntil = 0, g_watchFrom = 0;
int g_lastFailed[4] = {}, g_lastFailedN = 0;

// 8.51 test 16 (2026-10-15): every freeze so far reads the name at THAW.exe 0x00719AE8 (a direct
// reference from reTHAWed's skater animation script) as a call, and loops. The user's first
// proxy test skitched and let go with no freeze, so something changes that entry. Symbol watch:
// once the vehicle scan is done, its first 0x40 bytes are logged, and 16 bytes from 0x719AE8 get
// the 4 write watches for the rest of the level; each new writer is logged within a second with
// the entry's bytes as they are then. (The turn watches stay off meanwhile: all turns hold now.)
constexpr unsigned long kSymAddr = 0x00719AE8;
volatile LONG g_symWatch = 0;   // 1 = the 4 watches are on the symbol
LONG g_symLogged = 0;           // writers already logged
bool g_symDone = false;         // armed once per level
constexpr bool kSymWatchOn = false; // test 16 answered it (see FindScriptHangs); kept for later use

// 8.51 test 16 (2026-10-16): the entry at THAW.exe 0x719AE8 never changed, and its bytes (a name
// checksum, then the address 0x0042EDB0) show it is one of reTHAWed's built-in C functions, not a
// value. The freeze is a line in reTHAWed's skater animation script, `IF (<BED84CA4> = 1) <that
// function> = 1`: the function name is used as a setting. When the line runs, the reader takes
// "= 1" as the function's parameters and never moves past the '=' (THAW.exe+0xEF50 / +0xE340), so
// the game thread hangs. It runs only when that parameter is set, which is why letting go froze
// only sometimes. Fix: in reTHAWed's memory (never on disk), every line that is a direct function
// reference (0x46 + an address in THAW.exe's table at 0x700000-0x7FFFFF) followed straight by '='
// and a 5-byte value is overwritten with new-line bytes (0x01), which the reader skips. Such a
// line can only hang, so nothing is lost. Found on the vehicle scan's thread, written on the
// game thread, logged with each address.
constexpr int kMaxHangs = 16;
unsigned long g_hangAt[kMaxHangs];
DWORD g_hangRef[kMaxHangs];
int g_hangCount = 0;
volatile LONG g_hangPending = 0;  // 1 = found, waiting for the game thread
volatile LONG g_hangPatched = -1; // how many the game thread blanked (-1 = not yet)
bool g_hangsSearched = false;     // once per run of reTHAWed (the scripts load once)
constexpr int kHangLen = 11;      // 0x46 + 4-byte address + '=' + a 5-byte value
PVOID g_watchHandler = nullptr;

std::string ProxyPath()
{
    const std::string extracted = mp::ExtractedFolderA();
    if (extracted.empty()) return std::string();
    return extracted + "\\thaw_maps\\_railpool\\proxies.csv";
}

bool LoadCars()
{
    g_carCount = 0;
    AcquireSRWLockExclusive(&g_lock);
    ResetSlots();
    ReleaseSRWLockExclusive(&g_lock);
    const std::string path = ProxyPath();
    FILE* f = nullptr;
    if (path.empty() || fopen_s(&f, path.c_str(), "r") != 0 || !f) {
        g_log("vehicles: no proxies.csv (%s); build the map with --proxy-cars N --thaw-dir <reTHAWed>",
              path.empty() ? "no extracted folder" : path.c_str());
        return false;
    }
    char line[256];
    while (fgets(line, sizeof(line), f) && g_carCount < kMaxCars) {
        int i = 0;
        Car c;
        if (sscanf_s(line, "%d,%f,%f,%f", &i, &c.p[0], &c.p[1], &c.p[2]) != 4) continue; // header
        memcpy(c.cur, c.p, sizeof(c.cur));
        g_cars[g_carCount++] = c;
    }
    fclose(f);
    if (!g_carCount) g_log("vehicles: proxies.csv lists no vehicles (this map has none)");
    return g_carCount > 0;
}

bool Readable(const MEMORY_BASIC_INFORMATION& m)
{
    if (m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD p = m.Protect & 0xFF;
    return p == PAGE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

bool Near(const float* q, const float* p, float tolXZ, float tolY)
{
    return fabsf(q[0] - p[0]) < tolXZ && fabsf(q[2] - p[2]) < tolXZ && fabsf(q[1] - p[1]) < tolY;
}

void ScanRegion(unsigned long base, unsigned long end)
{
    __try {
        for (unsigned long a = base; a + 12 <= end; a += 4) {
            if (a >= g_selfLo && a < g_selfHi) continue;
            const float* q = reinterpret_cast<const float*>(a);
            for (int c = 0; c < g_carCount; ++c) {
                Car& car = g_cars[c];
                if (fabsf(q[0] - car.p[0]) >= kTolXZ) continue; // cheap first test
                if (Near(q, car.p, kTolXZ, kTolY) && car.hits < kMaxHits) car.c[car.hits++].a = a;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

const char* RegionKind(unsigned long a)
{
    MEMORY_BASIC_INFORMATION m;
    if (VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof(m)) != sizeof(m)) return "?";
    return m.Type == MEM_IMAGE ? "image" : m.Type == MEM_MAPPED ? "mapped" : "private";
}

// Three rows of 4 floats (x, y, z, 0) that are unit length and at right angles: a rotation.
bool IsRotation(const float* m)
{
    for (int r = 0; r < 3; ++r) {
        const float* v = m + 4 * r;
        const float len = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (!(fabsf(len - 1.0f) < 0.01f) || fabsf(v[3]) > 0.001f) return false;
    }
    for (int r = 0; r < 3; ++r)
        for (int s = r + 1; s < 3; ++s) {
            const float* a = m + 4 * r;
            const float* b = m + 4 * s;
            if (fabsf(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) > 0.01f) return false;
        }
    return true;
}

// Where a copy's rotation rows are.
unsigned long RowsAddr(const Copy& cp)
{
    return cp.rows < 0 ? cp.a - 48 : cp.rows == 1 ? cp.a + 16 : cp.rows == 3 ? cp.a + 0x20 : cp.a + 0x74;
}

// 8.51 test 8 (2026-10-15): the code that rebuilds the car's matrices every frame (THAW.exe+0x4CD60,
// read from memory) takes the object's position at object+0x68 and its turn at object+0xDC (three
// rows of 4 floats), and gives each part of the car local turn x object turn. That turn is 0x74
// bytes after a position copy, so it was never found or written: rows = 2.
void FindRows(Copy& cp)
{
    float before[12] = {}, after[12] = {}, obj[12] = {};
    __try {
        memcpy(before, reinterpret_cast<const void*>(cp.a - 48), sizeof(before));
        memcpy(after, reinterpret_cast<const void*>(cp.a + 16), sizeof(after));
        memcpy(obj, reinterpret_cast<const void*>(cp.a + 0x74), sizeof(obj));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    const float* m = IsRotation(before) ? before : IsRotation(after) ? after : IsRotation(obj) ? obj : nullptr;
    if (!m) return;
    cp.rows = m == before ? -1 : m == after ? 1 : 2;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) cp.r0[r][k] = m[4 * r + k];
}

void LogCopy(int c, int h)
{
    const Copy& cp = g_cars[c].c[h];
    float v[16] = {};
    __try {
        memcpy(v, reinterpret_cast<const void*>(cp.a - 16), sizeof(v));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_log("vehicles: car %d copy %d at 0x%08lX: unreadable now", c, h, cp.a);
        return;
    }
    g_log("vehicles: car %d copy %d at 0x%08lX (%s): %.2f %.2f %.2f | rotation %s%s", c, h, cp.a, RegionKind(cp.a),
          v[4], v[5], v[6], cp.rows < 0 ? "before it" : cp.rows == 1 ? "after it" : cp.rows == 2 ? "0x74 on (the object's own turn)" : cp.rows == 3 ? "0x20 on (the turn the object's is copied from)" : "none",
          cp.rows ? "" : "");
    if (cp.rows)
        g_log("vehicles:   rows %.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f", cp.r0[0][0], cp.r0[0][1], cp.r0[0][2],
              cp.r0[1][0], cp.r0[1][1], cp.r0[1][2], cp.r0[2][0], cp.r0[2][1], cp.r0[2][2]);
}

bool NearAbs(float v, float want) { return fabsf(fabsf(v) - want) < 0.01f; }

void AddSrc(unsigned long a, int kind, int sSlot, int cSlot, const float* v)
{
    for (int i = 0; i < g_srcCount; ++i)
        if (g_src[i].a == a) return;
    if (g_srcCount >= kMaxSrc) return;
    TurnSrc& t = g_src[g_srcCount++];
    t.a = a;
    t.kind = kind;
    t.sSlot = sSlot;
    t.cSlot = cSlot;
    memcpy(t.v0, v, sizeof(t.v0));
}

void FindTurnSources()
{
    g_srcCount = 0;
    if (g_carCount < 3) return; // needs car 2's distinctive turn
    const Car& car = TestCar();
    constexpr int kWin = 0x4000;
    static float w[2 * kWin / 4 + 4];
    for (int h = 0; h < car.hits; ++h) {
        bool ok = false;
        const unsigned long from = car.c[h].a - kWin;
        CopyMem(w, from, sizeof(w), ok);
        if (!ok) continue;
        for (int i = 0; i + 4 <= 2 * kWin / 4 + 4; ++i) {
            const float* v = w + i;
            // quaternion: two slots ~0, the other two |0.924| (sine, the bigger) and |0.383| (cosine)
            int zeros = 0, sinK = -1, cosK = -1;
            for (int k = 0; k < 4; ++k) {
                if (fabsf(v[k]) < 0.002f) ++zeros;
                else if (NearAbs(v[k], 0.9239f)) sinK = k;
                else if (NearAbs(v[k], 0.3827f)) cosK = k;
            }
            if (zeros == 2 && sinK >= 0 && cosK >= 0) AddSrc(from + 4 * i, 1, sinK, cosK, v);
            // an angle in radians: 225 / 135 degrees, either way round
            for (float want : {3.92699f, -2.35619f, 2.35619f, -3.92699f})
                if (fabsf(v[0] - want) < 0.0005f) AddSrc(from + 4 * i, 2, 0, 0, v);
        }
    }
}

void FindHangRegion(unsigned long base, unsigned long end)
{
    __try {
        for (unsigned long a = base; a + kHangLen + 1 <= end && g_hangCount < kMaxHangs; ++a) {
            const BYTE* b = reinterpret_cast<const BYTE*>(a);
            if (b[0] != 0x01 || b[1] != 0x46 || b[6] != 0x07) continue;
            const DWORD ref = static_cast<DWORD>(b[2]) | static_cast<DWORD>(b[3]) << 8 | static_cast<DWORD>(b[4]) << 16 |
                              static_cast<DWORD>(b[5]) << 24;
            if (ref < 0x700000 || ref >= 0x800000) continue;
            if (b[7] != 0x16 && b[7] != 0x17 && b[7] != 0x1A) continue; // a name, a whole number or a decimal
            g_hangAt[g_hangCount] = a + 1;
            g_hangRef[g_hangCount] = ref;
            ++g_hangCount;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void FindScriptHangs()
{
    g_hangCount = 0;
    if (reinterpret_cast<unsigned long>(GetModuleHandleW(nullptr)) != 0x400000) return; // THAW.exe 1.0 addresses only
    for (unsigned long a = 0x10000; a < 0x7FFF0000 && g_hangCount < kMaxHangs;) {
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof(m)) != sizeof(m)) break;
        const unsigned long b = reinterpret_cast<unsigned long>(m.BaseAddress);
        const unsigned long e = b + static_cast<unsigned long>(m.RegionSize);
        if (m.Type == MEM_PRIVATE && Readable(m) && !(b >= g_selfLo && b < g_selfHi)) FindHangRegion(b, e);
        if (e <= a) break;
        a = e;
    }
    if (g_hangCount) InterlockedExchange(&g_hangPending, 1);
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
    for (int c = 0; c < g_carCount; ++c) g_cars[c].hits = 0;
    for (unsigned long a = 0x10000; a < 0x7FFF0000;) {
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof(m)) != sizeof(m)) break;
        const unsigned long b = reinterpret_cast<unsigned long>(m.BaseAddress);
        const unsigned long e = b + static_cast<unsigned long>(m.RegionSize);
        if (Readable(m)) ScanRegion(b, e);
        if (e <= a) break;
        a = e;
    }
    if (g_scanGen != g_levelGen) {
        g_log("vehicles: the level went away during the scan; not using it");
        InterlockedExchange(&g_scanState, 0);
        return 0;
    }
    for (int c = 0; c < g_carCount; ++c)
        for (int h = 0; h < g_cars[c].hits; ++h) FindRows(g_cars[c].c[h]);
    // 8.51 test 9 (2026-10-15): the object's own turn (object+0xDC) is copied every frame from
    // object+0x88 (THAW.exe+0x5B960: the object's matrix set from its +0x88, in a component update
    // that also moves object+0x6C, its height). That is 0x20 after the same position copy: it gets
    // its own entry (rows = 3), so it is turned, checked and watched like the others.
    for (int c = 0; c < g_carCount; ++c) {
        Car& car = g_cars[c];
        const int n = car.hits;
        for (int h = 0; h < n && car.hits < kMaxHits; ++h) {
            if (car.c[h].rows != 2) continue;
            float m[12] = {};
            bool ok = false;
            CopyMem(m, car.c[h].a + 0x20, sizeof(m), ok);
            if (!ok || !IsRotation(m)) continue;
            Copy& x = car.c[car.hits++];
            x = Copy{};
            x.a = car.c[h].a;
            x.rows = 3;
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 3; ++k) x.r0[r][k] = m[4 * r + k];
        }
    }
    g_fc = g_carCount >= 3 ? 2 : 0;
    FindTurnSources();
    g_log("vehicles: scan done in %lu ms (read only)", GetTickCount() - t0);
    for (int c = 0; c < g_carCount; ++c) {
        int rot = 0;
        for (int h = 0; h < g_cars[c].hits; ++h)
            if (g_cars[c].c[h].rows) ++rot;
        g_log("vehicles: car %d (placed at %.1f %.1f %.1f): %d copies of its position, %d with a rotation next to them%s",
              c, g_cars[c].p[0], g_cars[c].p[1], g_cars[c].p[2], g_cars[c].hits, rot,
              g_cars[c].hits >= kMaxHits ? " (list full)" : "");
        if (c == g_fc)
            for (int h = 0; h < g_cars[c].hits; ++h) LogCopy(c, h);
    }
    CompareTurns();
    g_log("vehicles: the test car is car %d; %d turn source candidate(s) near its copies:", g_fc, g_srcCount);
    for (int i = 0; i < g_srcCount; ++i) {
        const TurnSrc& t = g_src[i];
        if (t.kind == 1)
            g_log("vehicles:   quaternion at 0x%08lX: %.4f %.4f %.4f %.4f (sine in slot %d, cosine in slot %d)", t.a, t.v0[0],
                  t.v0[1], t.v0[2], t.v0[3], t.sSlot, t.cSlot);
        else
            g_log("vehicles:   angle at 0x%08lX: %.5f", t.a, t.v0[0]);
    }
    g_log("vehicles: ready: F2 = the test car follows the nearest moving GTA car (on / off); F1 = turn its nose 90 degrees");
    InterlockedExchange(&g_scanState, 2);
    // test 22 (2026-10-16): this search took 14 s and held the cars back from "ready"; it now runs after
    // (follow mode can start meanwhile; the hang line is only reached when the skater lets go)
    if (!g_hangsSearched) {
        g_hangsSearched = true;
        const DWORD th = GetTickCount();
        FindScriptHangs();
        g_log("script fix: %d line(s) in reTHAWed's scripts that can only hang the game (a built-in function used as a setting) "
              "found in %lu ms%s", g_hangCount, GetTickCount() - th, g_hangCount ? "; blanking them on the game thread" : "");
        for (int i = 0; i < g_hangCount; ++i) {
            BYTE b[kHangLen + 4] = {};
            bool ok = false;
            CopyMem(b, g_hangAt[i] - 1, sizeof(b), ok);
            DWORD sym[3] = {};
            bool oks = false;
            CopyMem(sym, g_hangRef[i], sizeof(sym), oks);
            g_log("script fix:   at 0x%08lX: function entry 0x%08lX (name %08lX, code 0x%08lX); bytes %02X %02X %02X %02X %02X %02X %02X "
                  "%02X %02X %02X %02X %02X", g_hangAt[i], g_hangRef[i], oks ? sym[0] : 0UL, oks ? sym[2] : 0UL, b[0], b[1], b[2], b[3],
                  b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11]);
        }
    }
    return 0;
}

// 8.51: the map tool stands car 1 a quarter turn and car 2 an eighth turn further than the test car.
// Around each copy of the position, numbers that differ between the cars and stay within -1..1
// are what holds a car's turn (a matrix, or a quaternion). Logs them, offset from the copy.
void CompareTurns()
{
    if (g_carCount < 3) return;
    constexpr int kWin = 0x200; // bytes either side
    static float w[3][2 * kWin / 4];
    const int n = g_cars[0].hits < g_cars[1].hits ? g_cars[0].hits : g_cars[1].hits;
    for (int h = 0; h < n && h < g_cars[2].hits; ++h) {
        bool ok[3];
        for (int c = 0; c < 3; ++c) CopyMem(w[c], g_cars[c].c[h].a - kWin, sizeof(w[c]), ok[c]);
        if (!ok[0] || !ok[1] || !ok[2]) {
            g_log("vehicles: turn check, copy %d: not readable", h);
            continue;
        }
        char line[1000];
        int l = _snprintf_s(line, sizeof(line), _TRUNCATE, "vehicles: turn check, copy %d (0x%08lX / 0x%08lX / 0x%08lX):", h,
                            g_cars[0].c[h].a, g_cars[1].c[h].a, g_cars[2].c[h].a);
        int found = 0;
        for (int i = 0; i < 2 * kWin / 4 && found < 28 && l > 0 && l < static_cast<int>(sizeof(line)) - 48; ++i) {
            const float a = w[0][i], b = w[1][i], c = w[2][i];
            if (!(fabsf(a) <= 1.0001f && fabsf(b) <= 1.0001f && fabsf(c) <= 1.0001f)) continue;
            if (fabsf(a - b) < 0.01f && fabsf(a - c) < 0.01f) continue;
            const int off = i * 4 - kWin;
            const int m = _snprintf_s(line + l, sizeof(line) - l, _TRUNCATE, " %s0x%X: %.2f/%.2f/%.2f", off < 0 ? "-" : "+",
                                      off < 0 ? -off : off, a, b, c);
            if (m < 0) break;
            l += m;
            ++found;
        }
        if (!found) _snprintf_s(line + l, sizeof(line) - l, _TRUNCATE, " nothing differs");
        g_log("%s", line);
    }
}

// The car's start heading: the third row of its first rotation copy (radians, from +z towards +x).
float StartHeading(const Car& car)
{
    for (int h = 0; h < car.hits; ++h)
        if (car.c[h].rows) return atan2f(car.c[h].r0[2][0], car.c[h].r0[2][2]);
    return 0.0f;
}

void TurnRow(const float* v, float yaw, float* out)
{
    const float c = cosf(yaw), s = sinf(yaw);
    out[0] = v[0] * c + v[2] * s;
    out[1] = v[1];
    out[2] = -v[0] * s + v[2] * c;
}

void SetTarget(int slot, const float* pos, float yaw)
{
    AcquireSRWLockExclusive(&g_lock);
    Target& tg = g_slots[slot].target;
    for (int k = 0; k < 3; ++k) tg.pos[k] = pos[k];
    tg.yaw = yaw;
    tg.gen = g_levelGen;
    ++tg.version;
    ReleaseSRWLockExclusive(&g_lock);
}

// reTHAWed's game state allows writing the proxies now (our level, not the menus, the scan done)
bool WriteAllowed()
{
    const GameState st = ReadGameState();
    return st.readOk && st.inFrontend == 0 && st.levelId == kOurLevel && g_scanState == 2 && g_carCount > 0;
}

// Game thread, lock held: write a proxy's copies to its target. A copy that no longer holds
// what we last wrote is skipped (its memory may have been reused). 8.52: any proxy, not only
// the test car; `allowed` is WriteAllowed(), read once per pass.
void ApplySlotLocked(int slot, bool allowed)
{
    Slot& sl = g_slots[slot];
    const Target& tg = sl.target;
    if (tg.version == sl.appliedVersion) return;
    sl.appliedVersion = tg.version;
    if (!allowed || tg.gen != g_levelGen) {
        InterlockedIncrement(&g_refused);
        return;
    }
    Car& car = g_cars[slot];
    int written = 0;
    for (int h = 0; h < car.hits; ++h) {
        Copy& cp = car.c[h];
        __try {
            float* w = reinterpret_cast<float*>(cp.a);
            if (!Near(w, car.cur, kApplyTol, kApplyTol)) { InterlockedIncrement(&g_skippedCopies); continue; }
            for (int k = 0; k < 3; ++k) w[k] = tg.pos[k];
            if (cp.rows) {
                float* m = reinterpret_cast<float*>(RowsAddr(cp));
                for (int r = 0; r < 3; ++r) TurnRow(cp.r0[r], tg.yaw, m + 4 * r);
            }
            ++written;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            InterlockedIncrement(&g_skippedCopies);
        }
    }
    // the turn sources (candidates, test car only): turned by the same amount from how they were found
    for (int i = 0; slot == g_fc && i < g_srcCount; ++i) {
        TurnSrc& t = g_src[i];
        __try {
            float* v = reinterpret_cast<float*>(t.a);
            if (t.kind == 1) {
                const float h0 = atan2f(t.v0[t.sSlot], t.v0[t.cSlot]);
                v[t.sSlot] = sinf(h0 + 0.5f * tg.yaw);
                v[t.cSlot] = cosf(h0 + 0.5f * tg.yaw);
            } else {
                v[0] = t.v0[0] + tg.yaw;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (written) {
        for (int k = 0; k < 3; ++k) car.cur[k] = tg.pos[k];
        car.yaw = tg.yaw;
        InterlockedIncrement(&g_applied);
    }
}

struct ModRange { const char* name; unsigned long lo, hi; };
ModRange g_mods[3] = {{"THAW.exe", 0, 0}, {"rthw.dll", 0, 0}, {"SkateAndreas-thaw.dll", 0, 0}};

void FillMods()
{
    HMODULE m[3] = {GetModuleHandleW(nullptr), GetModuleHandleW(L"rthw.dll"), nullptr};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&FillMods), &m[2]);
    for (int i = 0; i < 3; ++i) {
        if (!m[i]) continue;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m[i]);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const char*>(m[i]) + dos->e_lfanew);
        g_mods[i].lo = reinterpret_cast<unsigned long>(m[i]);
        g_mods[i].hi = g_mods[i].lo + nt->OptionalHeader.SizeOfImage;
    }
}

int ModOf(unsigned long a)
{
    for (int i = 0; i < 3; ++i)
        if (g_mods[i].lo && a >= g_mods[i].lo && a < g_mods[i].hi) return i;
    return -1;
}

struct StuckShot {
    bool ok;
    DWORD eip, esp, ebp, eax, ebx, ecx, edx, esi, edi;
    DWORD stack[96];
    int words;
    // reTHAWed's script interpreter (THAW.exe 1.0): CScript::Update (0x413000) keeps the script it
    // runs in the global 0x6DCF84; a script's next instruction is at +0x10 and the object it
    // runs on at +0xC4. The test froze inside the interpreter, so: which script, doing what?
    DWORD script;
    BYTE scriptMem[256];
    bool scriptOk;
    DWORD pc;
    BYTE code[128];                   // from pc - 32
    bool codeOk;
    DWORD object;
    BYTE objectMem[96];
    bool objectOk;
};

void CopyMem(void* to, DWORD from, size_t n, bool& ok)
{
    ok = false;
    __try {
        memcpy(to, reinterpret_cast<const void*>(from), n);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Stop the game thread for a moment and copy what we need. Nothing here allocates or logs.
void TakeShot(DWORD tid, StuckShot& s)
{
    s.ok = false;
    s.words = 0;
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, tid);
    if (!h) return;
    if (SuspendThread(h) != static_cast<DWORD>(-1)) {
        CONTEXT ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(h, &ctx)) {
            s.eip = ctx.Eip; s.esp = ctx.Esp; s.ebp = ctx.Ebp; s.eax = ctx.Eax; s.ebx = ctx.Ebx;
            s.ecx = ctx.Ecx; s.edx = ctx.Edx; s.esi = ctx.Esi; s.edi = ctx.Edi;
            s.ok = true;
            // which script is running (THAW.exe 1.0 addresses; checked against the module's base)
            s.script = 0;
            s.scriptOk = s.codeOk = s.objectOk = false;
            if (g_mods[0].lo == 0x400000) {
                bool okp = false;
                CopyMem(&s.script, 0x6DCF84, 4, okp);
                if (okp && s.script) {
                    CopyMem(s.scriptMem, s.script, sizeof(s.scriptMem), s.scriptOk);
                    if (s.scriptOk) {
                        memcpy(&s.pc, s.scriptMem + 0x10, 4);
                        memcpy(&s.object, s.scriptMem + 0xC4, 4);
                        if (s.pc > 32) CopyMem(s.code, s.pc - 32, sizeof(s.code), s.codeOk);
                        if (s.object) CopyMem(s.objectMem, s.object, sizeof(s.objectMem), s.objectOk);
                    }
                }
            }
            for (int i = 0; i < 96; ++i) {
                __try {
                    s.stack[i] = *reinterpret_cast<const DWORD*>(ctx.Esp + 4 * i);
                    s.words = i + 1;
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    break;
                }
            }
        }
        ResumeThread(h);
    }
    CloseHandle(h);
}

void LogShot(const StuckShot& s, int n, void (*log)(const char*, ...))
{
    if (!s.ok) { log("watchdog: sample %d: could not read the game thread", n); return; }
    const int m = ModOf(s.eip);
    log("watchdog: sample %d: game thread at 0x%08lX (%s+0x%lX) | eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX "
        "edi %08lX ebp %08lX esp %08lX", n, s.eip, m >= 0 ? g_mods[m].name : "?", m >= 0 ? s.eip - g_mods[m].lo : 0UL,
        s.eax, s.ebx, s.ecx, s.edx, s.esi, s.edi, s.ebp, s.esp);
    char buf[900];
    int len = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "watchdog: sample %d: code addresses on its stack:", n);
    int found = 0;
    for (int i = 0; i < s.words && len > 0 && len < static_cast<int>(sizeof(buf)) - 40 && found < 24; ++i) {
        const int k = ModOf(s.stack[i]);
        if (k < 0) continue;
        const int w = _snprintf_s(buf + len, sizeof(buf) - len, _TRUNCATE, " %s+0x%lX", k == 0 ? "exe" : k == 1 ? "rthw" : "ours",
                                  s.stack[i] - g_mods[k].lo);
        if (w < 0) break;
        len += w;
        ++found;
    }
    log("%s", buf);
    // the running script, its code around the next instruction, and its object (hex, for reading offline)
    auto hex = [&](const char* what, const BYTE* b, int nb, DWORD at) {
        for (int row = 0; row < nb; row += 32) {
            char line[200];
            int l = _snprintf_s(line, sizeof(line), _TRUNCATE, "watchdog: sample %d: %s +0x%02X (0x%08lX):", n, what, row,
                                at + row);
            for (int i = row; i < row + 32 && i < nb && l > 0 && l < static_cast<int>(sizeof(line)) - 4; ++i)
                l += _snprintf_s(line + l, sizeof(line) - l, _TRUNCATE, "%s%02X", (i % 4) ? "" : " ", b[i]);
            log("%s", line);
        }
    };
    if (!s.script) { log("watchdog: sample %d: no current script (or not THAW.exe 1.0)", n); return; }
    log("watchdog: sample %d: current script object 0x%08lX, next instruction 0x%08lX, runs on object 0x%08lX", n,
        s.script, s.pc, s.object);
    if (n == 1 || n == 3) {
        if (s.scriptOk) hex("script", s.scriptMem, sizeof(s.scriptMem), s.script);
        if (s.codeOk) hex("code from pc-32", s.code, sizeof(s.code), s.pc - 32);
        if (s.objectOk) hex("object", s.objectMem, sizeof(s.objectMem), s.object);
    }
}

// 8.51 test 10 (2026-10-15): with object+0x88 turned too, the car turned on screen and all 9
// turns held, but reTHAWed's own skitch still grabbed the same side: what the skitch uses keeps
// its own copy of the car's turn. Twin scan (read only, its own thread, once per level): after a
// turn that held, look through all of reTHAWed's memory for the test car's START turn (each
// form its copies had: rows of 4 or packed rows of 3, and the same turned over), skipping the
// copies we know. What still holds the old turn is what was not told: logged with its offset
// from the car object.
constexpr int kMaxForms = 4, kMaxTwins = 40;
float g_forms[kMaxForms][3][3];
int g_formCount = 0;
unsigned long g_twin[kMaxTwins];
int g_twinKind[kMaxTwins];
int g_twinCount = 0;
volatile LONG g_twinState = 0; // 0 = not yet, 1 = running, 2 = done (this level)
unsigned long g_carObj = 0;

bool SameRows(const float* m, int stride, bool transposed, const float (*f)[3])
{
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) {
            const float v = transposed ? m[k * stride + r] : m[r * stride + k];
            if (!(fabsf(v - f[r][k]) <= 0.01f)) return false; // written so NaN never matches (test 11)
        }
    return true;
}

bool KnownRows(unsigned long a)
{
    const Car& car = TestCar();
    for (int h = 0; h < car.hits; ++h)
        if (car.c[h].rows && RowsAddr(car.c[h]) == a) return true;
    return false;
}

void TwinRegion(unsigned long base, unsigned long end)
{
    __try {
        for (unsigned long a = base; a + 48 <= end && g_twinCount < kMaxTwins; a += 4) {
            if (a >= g_selfLo && a < g_selfHi) continue;
            const float* m = reinterpret_cast<const float*>(a);
            const float x = m[0];
            for (int f = 0; f < g_formCount; ++f) {
                const float (*F)[3] = g_forms[f];
                if (!(fabsf(x - F[0][0]) <= 0.01f)) continue; // the same first number either way round; NaN never matches
                int kind = 0;
                if (SameRows(m, 4, false, F)) kind = 1;
                else if (SameRows(m, 3, false, F)) kind = 2;
                else if (SameRows(m, 4, true, F)) kind = 3;
                else if (SameRows(m, 3, true, F)) kind = 4;
                if (!kind) continue;
                // rows of 4 must have 0 in the 4th place (a real matrix row, not any 12 floats)
                if ((kind == 1 || kind == 3) && !(fabsf(m[3]) <= 0.001f && fabsf(m[7]) <= 0.001f)) continue;
                if (KnownRows(a)) break;
                g_twin[g_twinCount] = a;
                g_twinKind[g_twinCount] = kind * 10 + f;
                ++g_twinCount;
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// 8.51 test 12 (2026-10-15): nothing still holds the car's old turn, yet the skitch grabs the same
// side. Near-point scan (read only, same thread, right after the twin scan): every float triple
// within 4 m across (and 2.5 m up or down) of the car's position that is not one of its known
// copies, logged as an offset in the car's START frame (along its nose / to its side / up) and in
// the world, with a guess whether it sits in a rail record like the level's (0x60 bytes, the
// point at +0x20, links at +0x50 / +0x54). Points still lined up with the start frame after
// a turn are what was moved but not turned (the car's own rails, most likely).
constexpr int kMaxNear = 64;
unsigned long g_near[kMaxNear];
int g_nearCount = 0;
float g_nearCar[3] = {};

bool KnownPos(unsigned long a)
{
    const Car& car = TestCar();
    for (int h = 0; h < car.hits; ++h)
        if (car.c[h].a == a) return true;
    return false;
}

bool LooksPtr(DWORD v) { return v == 0 || (v >= 0x10000 && v < 0x7FFF0000 && (v & 3) == 0); }

void NearRegion(unsigned long base, unsigned long end)
{
    const float r = 160.0f, up = 100.0f; // THAW inches: 4 m across, 2.5 m up / down
    __try {
        for (unsigned long a = base; a + 12 <= end && g_nearCount < kMaxNear; a += 4) {
            if (a >= g_selfLo && a < g_selfHi) continue;
            const float* q = reinterpret_cast<const float*>(a);
            const float dx = q[0] - g_nearCar[0];
            if (!(fabsf(dx) <= r)) continue;
            const float dz = q[2] - g_nearCar[2], dy = q[1] - g_nearCar[1];
            if (!(fabsf(dz) <= r) || !(fabsf(dy) <= up) || !(dx * dx + dz * dz <= r * r)) continue;
            if (fabsf(dx) < 0.01f && fabsf(dz) < 0.01f && fabsf(dy) < 0.01f) {
                if (KnownPos(a)) continue; // one of the car's own position copies
            }
            // ground-level points (the level's own surface around the car) only if they sit in a rail-like record
            const DWORD* rec = reinterpret_cast<const DWORD*>(a - 0x20);
            const bool railish = LooksPtr(rec[0x50 / 4]) && LooksPtr(rec[0x54 / 4]) && (rec[0x50 / 4] || rec[0x54 / 4]);
            if (!railish && fabsf(dy) < 6.0f) continue;
            g_near[g_nearCount++] = a;
            a += 8; // not the same triple shifted by one float
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void NearScan()
{
    const Car& car = TestCar();
    memcpy(g_nearCar, car.cur, sizeof(g_nearCar));
    g_nearCount = 0;
    const DWORD t0 = GetTickCount();
    for (unsigned long a = 0x10000; a < 0x7FFF0000 && g_nearCount < kMaxNear;) {
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof(m)) != sizeof(m)) break;
        const unsigned long b = reinterpret_cast<unsigned long>(m.BaseAddress);
        const unsigned long e = b + static_cast<unsigned long>(m.RegionSize);
        if (Readable(m)) NearRegion(b, e);
        if (e <= a) break;
        a = e;
    }
    // the car's start frame: nose = the start heading (as StartHeading), side = nose turned 90 degrees
    float h0 = 0.0f;
    for (int h = 0; h < car.hits; ++h)
        if (car.c[h].rows) { h0 = atan2f(car.c[h].r0[2][0], car.c[h].r0[2][2]); break; }
    const float h1 = h0 + car.yaw;
    g_log("near scan: done in %lu ms: %d point(s) within 4 m of the test car (at %.0f %.0f %.0f, turned %.0f deg)%s. "
          "Offsets in inches: start frame (nose / side / up), then the turned frame", GetTickCount() - t0, g_nearCount, g_nearCar[0],
          g_nearCar[1], g_nearCar[2], car.yaw * 57.2958f, g_nearCount >= kMaxNear ? " (list full)" : "");
    for (int i = 0; i < g_nearCount; ++i) {
        const unsigned long a = g_near[i];
        float q[3] = {};
        DWORD rec[24] = {};
        bool ok = false, okr = false;
        CopyMem(q, a, sizeof(q), ok);
        CopyMem(rec, a - 0x20, sizeof(rec), okr);
        if (!ok) continue;
        const float dx = q[0] - g_nearCar[0], dy = q[1] - g_nearCar[1], dz = q[2] - g_nearCar[2];
        const float n0 = dx * sinf(h0) + dz * cosf(h0), s0 = dx * cosf(h0) - dz * sinf(h0);
        const float n1 = dx * sinf(h1) + dz * cosf(h1), s1 = dx * cosf(h1) - dz * sinf(h1);
        const bool railish = okr && LooksPtr(rec[0x50 / 4]) && LooksPtr(rec[0x54 / 4]) && (rec[0x50 / 4] || rec[0x54 / 4]);
        g_log("near scan:   0x%08lX (%s%s): start %.0f / %.0f / %.0f | turned %.0f / %.0f / %.0f | world %.0f %.0f %.0f", a, RegionKind(a),
              railish ? ", rail-like record" : "", n0, s0, dy, n1, s1, dy, q[0], q[1], q[2]);
    }
}

DWORD WINAPI TwinThread(LPVOID)
{
    const DWORD t0 = GetTickCount();
    g_twinCount = 0;
    for (unsigned long a = 0x10000; a < 0x7FFF0000 && g_twinCount < kMaxTwins;) {
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof(m)) != sizeof(m)) break;
        const unsigned long b = reinterpret_cast<unsigned long>(m.BaseAddress);
        const unsigned long e = b + static_cast<unsigned long>(m.RegionSize);
        if (Readable(m)) TwinRegion(b, e);
        if (e <= a) break;
        a = e;
    }
    static const char* kinds[] = {"?", "rows of 4", "rows of 3", "rows of 4, turned over", "rows of 3, turned over"};
    g_log("twin scan: done in %lu ms: %d place(s) still hold the test car's start turn (car object at 0x%08lX)%s", GetTickCount() - t0,
          g_twinCount, g_carObj, g_twinCount >= kMaxTwins ? " (list full)" : "");
    for (int i = 0; i < g_twinCount; ++i) {
        const unsigned long a = g_twin[i];
        float v[16] = {};
        bool ok = false;
        CopyMem(v, a, sizeof(v), ok);
        const long off = static_cast<long>(a - g_carObj);
        g_log("twin scan:   0x%08lX (%s, form %d, %s, %s0x%lX from the car object): %.2f %.2f %.2f %.2f / %.2f %.2f %.2f %.2f / %.2f %.2f %.2f "
              "%.2f / %.1f %.1f %.1f %.1f", a, kinds[g_twinKind[i] / 10], g_twinKind[i] % 10, RegionKind(a), off < 0 ? "-" : "+",
              static_cast<unsigned long>(off < 0 ? -off : off), v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10],
              v[11], v[12], v[13], v[14], v[15]);
    }
    NearScan();
    InterlockedExchange(&g_twinState, 2);
    return 0;
}

void StartTwinScan()
{
    if (g_twinState != 0) return;
    const Car& car = TestCar();
    g_formCount = 0;
    g_carObj = 0;
    for (int h = 0; h < car.hits && g_formCount < kMaxForms; ++h) {
        const Copy& cp = car.c[h];
        if (!cp.rows) continue;
        if (cp.rows == 2) g_carObj = cp.a - 0x68;
        bool have = false;
        for (int f = 0; f < g_formCount && !have; ++f) {
            bool same = true;
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 3; ++k)
                    if (fabsf(g_forms[f][r][k] - cp.r0[r][k]) > 0.01f) same = false;
            have = same;
        }
        if (!have) memcpy(g_forms[g_formCount++], cp.r0, sizeof(cp.r0));
    }
    if (!g_formCount) return;
    InterlockedExchange(&g_twinState, 1);
    g_log("twin scan: looking for %d form(s) of the test car's start turn in reTHAWed's memory (read only)", g_formCount);
    HANDLE h = CreateThread(nullptr, 0, TwinThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_twinState, 0);
}

// 8.51 test 10: the freeze came back (after a skitch on the turned car): reTHAWed's game thread
// stuck at THAW.exe+0xE343..0xE95D. The code there is saved as it is in memory (like the
// writers', read only) on the first sample, with the THAW.exe addresses on its stack.
void DumpCode(const DWORD* addrs, int n, const char* what, void (*log)(const char*, ...))
{
    const std::string ex = mp::ExtractedFolderA();
    if (ex.empty() || !g_mods[0].lo) return;
    const std::string dir = ex + "\\veh_code";
    CreateDirectoryA(dir.c_str(), nullptr);
    int saved = 0;
    for (int k = 0; k < n; ++k) {
        if (ModOf(addrs[k]) != 0) continue;
        bool seen = false;
        for (int j = 0; j < k; ++j)
            if (addrs[j] == addrs[k]) seen = true;
        if (seen) continue;
        static BYTE buf[0x700];
        const DWORD from = addrs[k] - 0x600;
        bool ok = false;
        CopyMem(buf, from, sizeof(buf), ok);
        if (!ok) continue;
        char name[64];
        _snprintf_s(name, sizeof(name), _TRUNCATE, "\\exe_%08lX.bin", from);
        FILE* f = nullptr;
        if (fopen_s(&f, (dir + name).c_str(), "wb") == 0 && f) {
            fwrite(buf, 1, sizeof(buf), f);
            fclose(f);
            ++saved;
        }
    }
    log("%s: saved %d piece(s) of reTHAWed's code to %s (read only)", what, saved, dir.c_str());
}

// 8.51 test 14 (2026-10-15): the freeze is in THAW.exe+0xEF50, a script bytecode reader stuck on
// one spot (esi). Saves raw memory (read only) to extracted\\veh_code\\<what>_<address>.bin.
void SaveMem(const char* what, DWORD from, DWORD n)
{
    const std::string ex = mp::ExtractedFolderA();
    if (ex.empty() || n > 0x2000) return;
    const std::string dir = ex + "\\veh_code";
    CreateDirectoryA(dir.c_str(), nullptr);
    static BYTE buf[0x2000];
    // copy page by page so an unreadable edge does not lose the rest
    memset(buf, 0, n);
    for (DWORD o = 0; o < n; o += 0x100) {
        bool ok = false;
        CopyMem(buf + o, from + o, (n - o) < 0x100 ? (n - o) : 0x100, ok);
    }
    char name[96];
    _snprintf_s(name, sizeof(name), _TRUNCATE, "\\%s_%08lX.bin", what, from);
    FILE* f = nullptr;
    if (fopen_s(&f, (dir + name).c_str(), "wb") == 0 && f) {
        fwrite(buf, 1, n, f);
        fclose(f);
    }
}

LONG CALLBACK WatchHandler(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    const DWORD dr6 = c->Dr6, hit = dr6 & 0xF;
    if (!hit) return EXCEPTION_CONTINUE_SEARCH; // not one of our watches
    c->Dr6 = 0;
    if (!g_watchArmed) { c->Dr7 = 0; return EXCEPTION_CONTINUE_EXECUTION; } // a late one: clear it
    const int which = (hit & 1) ? 0 : (hit & 2) ? 1 : (hit & 4) ? 2 : 3;
    const DWORD eip = c->Eip;
    if (eip >= g_selfLo && eip < g_selfHi) { InterlockedIncrement(&g_watchOwn); return EXCEPTION_CONTINUE_EXECUTION; }
    const LONG n = g_watchN < kMaxWatchHits ? g_watchN : kMaxWatchHits;
    for (LONG i = 0; i < n; ++i)
        if (g_watchHits[i].eip == eip && g_watchHits[i].which == which && g_watchHits[i].r[0] == c->Eax &&
            g_watchHits[i].r[2] == c->Ecx) {
            InterlockedIncrement(&g_watchHits[i].count);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    const LONG slot = InterlockedIncrement(&g_watchN) - 1;
    if (slot >= kMaxWatchHits) return EXCEPTION_CONTINUE_EXECUTION;
    WatchHit& w = g_watchHits[slot];
    w.which = which;
    w.tid = GetCurrentThreadId();
    const DWORD r[8] = {c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp};
    memcpy(w.r, r, sizeof(r));
    for (int i = 0; i < 40; ++i) {
        __try {
            w.stack[i] = *reinterpret_cast<const DWORD*>(c->Esp + 4 * i);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            w.stack[i] = 0;
        }
    }
    for (int i = 0; i < 40; ++i) {
        __try {
            w.code[i] = *reinterpret_cast<const BYTE*>(eip - 32 + i);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            w.code[i] = 0;
        }
    }
    w.dr6 = dr6;
    w.dr7 = c->Dr7;
    w.atMs = GetTickCount() - g_watchFrom;
    w.count = 1;
    w.eip = eip; // last: a filled slot is matched only once its address is in
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Set (n > 0) or clear (n == 0) the debug registers on every thread of reTHAWed except this one.
int SetWatches(int n)
{
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    DWORD dr7 = 0;
    for (int i = 0; i < n; ++i) dr7 |= (1u << (2 * i)) | (1u << (16 + 4 * i)) | (3u << (18 + 4 * i)); // on, write, 4 bytes
    int set = 0;
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
        if (!h) continue;
        if (SuspendThread(h) != static_cast<DWORD>(-1)) {
            CONTEXT ctx;
            memset(&ctx, 0, sizeof(ctx));
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(h, &ctx)) {
                ctx.Dr0 = n > 0 ? g_watchAddr[0] : 0;
                ctx.Dr1 = n > 1 ? g_watchAddr[1] : 0;
                ctx.Dr2 = n > 2 ? g_watchAddr[2] : 0;
                ctx.Dr3 = n > 3 ? g_watchAddr[3] : 0;
                ctx.Dr6 = 0;
                ctx.Dr7 = dr7;
                if (SetThreadContext(h, &ctx)) ++set;
            }
            ResumeThread(h);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return set;
}

// Test 7 (2026-10-15): both writers really write (dr6 / dr7 and the live code agree): THAW.exe's
// code in memory differs from the file on disk, so it cannot be read offline from the file. After
// a watch, the code around each THAW.exe return address on the writers' stacks (where the matrix
// on the stack is built) is saved as it is in memory, read only, to extracted\veh_code\ (outside
// the tracked files): exe_<start>.bin, 0x600 bytes before the address and 0x100 after.
void DumpCallers(void (*log)(const char*, ...))
{
    const std::string ex = mp::ExtractedFolderA();
    if (ex.empty() || !g_mods[0].lo) return;
    const std::string dir = ex + "\\veh_code";
    CreateDirectoryA(dir.c_str(), nullptr);
    DWORD done[64];
    int nDone = 0, saved = 0;
    const LONG n = g_watchN < kMaxWatchHits ? g_watchN : kMaxWatchHits;
    for (LONG i = 0; i < n; ++i) {
        const WatchHit& w = g_watchHits[i];
        DWORD addrs[41];
        int na = 0;
        addrs[na++] = w.eip;
        for (int k = 0; k < 40; ++k)
            if (ModOf(w.stack[k]) == 0) addrs[na++] = w.stack[k];
        for (int k = 0; k < na && nDone < 64; ++k) {
            bool seen = false;
            for (int j = 0; j < nDone; ++j)
                if (done[j] == addrs[k]) seen = true;
            if (seen) continue;
            done[nDone++] = addrs[k];
            static BYTE buf[0x700];
            const DWORD from = addrs[k] - 0x600;
            bool ok = false;
            CopyMem(buf, from, sizeof(buf), ok);
            if (!ok) continue;
            char name[64];
            _snprintf_s(name, sizeof(name), _TRUNCATE, "\\exe_%08lX.bin", from);
            FILE* f = nullptr;
            if (fopen_s(&f, (dir + name).c_str(), "wb") == 0 && f) {
                fwrite(buf, 1, sizeof(buf), f);
                fclose(f);
                ++saved;
            }
        }
    }
    log("write watch: saved %d piece(s) of reTHAWed's code around the writers' callers to %s (read only)", saved, dir.c_str());
}

void LogWatch(void (*log)(const char*, ...))
{
    const LONG n = g_watchN < kMaxWatchHits ? g_watchN : kMaxWatchHits;
    log("write watch: done; %ld writer(s) seen (and %ld writes by our own plugin)", static_cast<long>(g_watchN),
        static_cast<long>(g_watchOwn));
    for (LONG i = 0; i < n; ++i) {
        const WatchHit& w = g_watchHits[i];
        const int m = ModOf(w.eip);
        log("write watch: copy %d's rows (0x%08lX) written %ld time(s), first %lu ms after the watch went on, by code at 0x%08lX "
            "(%s+0x%lX, the instruction before), thread %lu | eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX ebp %08lX "
            "esp %08lX | dr6 %08lX dr7 %08lX",
            g_watchCopy[w.which], g_watchAddr[w.which], static_cast<long>(w.count), w.atMs, w.eip, m >= 0 ? g_mods[m].name : "?",
            m >= 0 ? w.eip - g_mods[m].lo : 0UL, w.tid, w.r[0], w.r[1], w.r[2], w.r[3], w.r[4], w.r[5], w.r[6], w.r[7], w.dr6,
            w.dr7);
        char buf[900];
        int len = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "write watch:   code from -32:");
        for (int k = 0; k < 40 && len > 0 && len < static_cast<int>(sizeof(buf)) - 4; ++k)
            len += _snprintf_s(buf + len, sizeof(buf) - len, _TRUNCATE, "%s%02X", (k % 4) ? "" : " ", w.code[k]);
        log("%s", buf);
        len = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "write watch:   stack:");
        for (int k = 0; k < 40 && len > 0 && len < static_cast<int>(sizeof(buf)) - 24; ++k) {
            const int mk = ModOf(w.stack[k]);
            const int a = mk >= 0 ? _snprintf_s(buf + len, sizeof(buf) - len, _TRUNCATE, " %s+0x%lX",
                                                mk == 0 ? "exe" : mk == 1 ? "rthw" : "ours", w.stack[k] - g_mods[mk].lo)
                                  : _snprintf_s(buf + len, sizeof(buf) - len, _TRUNCATE, " %08lX", w.stack[k]);
            if (a < 0) break;
            len += a;
        }
        log("%s", buf);
    }
    for (int i = 0; i < g_watchCount; ++i) {
        float v[12] = {};
        bool ok = false;
        CopyMem(v, g_watchAddr[i], sizeof(v), ok);
        if (ok)
            log("write watch: copy %d's rows now %.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f", g_watchCopy[i], v[0], v[1], v[2],
                v[4], v[5], v[6], v[8], v[9], v[10]);
    }
    DumpCallers(log);
}

void LogSymBytes(const char* when, void (*log)(const char*, ...))
{
    BYTE b[0x40] = {};
    bool ok = false;
    CopyMem(b, kSymAddr - 8, sizeof(b), ok);
    char line[300];
    int l = _snprintf_s(line, sizeof(line), _TRUNCATE, "symbol watch (%s): THAW.exe 0x%08lX-8:", when, kSymAddr);
    for (int i = 0; ok && i < 0x40 && l > 0 && l < static_cast<int>(sizeof(line)) - 4; ++i)
        l += _snprintf_s(line + l, sizeof(line) - l, _TRUNCATE, "%s%02X", (i % 4) ? "" : " ", b[i]);
    log("%s%s", line, ok ? "" : " (unreadable)");
}

void LogSymHits(void (*log)(const char*, ...))
{
    const LONG n = g_watchN < kMaxWatchHits ? g_watchN : kMaxWatchHits;
    bool any = false;
    for (LONG i = g_symLogged; i < n; ++i) {
        const WatchHit& w = g_watchHits[i];
        if (!w.eip) return; // still being filled; next pass
        const int m = ModOf(w.eip);
        log("symbol watch: 0x%08lX+%d written (%ld time(s) so far) by code at 0x%08lX (%s+0x%lX, the instruction before), thread %lu | "
            "eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX ebp %08lX esp %08lX", kSymAddr, 4 * w.which,
            static_cast<long>(w.count), w.eip, m >= 0 ? g_mods[m].name : "?", m >= 0 ? w.eip - g_mods[m].lo : 0UL, w.tid, w.r[0],
            w.r[1], w.r[2], w.r[3], w.r[4], w.r[5], w.r[6], w.r[7]);
        char buf[600];
        int len = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "symbol watch:   stack:");
        for (int k = 0; k < 40 && len > 0 && len < static_cast<int>(sizeof(buf)) - 24; ++k) {
            const int mk = ModOf(w.stack[k]);
            if (mk < 0) continue;
            const int a = _snprintf_s(buf + len, sizeof(buf) - len, _TRUNCATE, " %s+0x%lX", mk == 0 ? "exe" : mk == 1 ? "rthw" : "ours",
                                      w.stack[k] - g_mods[mk].lo);
            if (a < 0) break;
            len += a;
        }
        log("%s", buf);
        DWORD addrs[41];
        int na = 0;
        addrs[na++] = w.eip;
        for (int k = 0; k < 40 && na < 41; ++k) addrs[na++] = w.stack[k];
        DumpCode(addrs, na < 12 ? na : 12, "symbol watch", log);
        g_symLogged = i + 1;
        any = true;
    }
    if (any) LogSymBytes("after the write", log);
}

} // namespace

bool VehFollowOn() { return g_follow != 0; }

void VehSetSkater(unsigned long skater) { g_skater = skater; }

void VehGameThreadTick()
{
    InterlockedExchange(&g_lastGameTick, static_cast<LONG>(GetTickCount()));
    if (!g_gameThreadId) InterlockedExchange(&g_gameThreadId, static_cast<LONG>(GetCurrentThreadId()));
    if (g_hangPending) {
        // blank the hang lines between frames, when no script is being read
        int done = 0;
        for (int i = 0; i < g_hangCount; ++i) {
            __try {
                BYTE* b = reinterpret_cast<BYTE*>(g_hangAt[i]);
                const DWORD ref = static_cast<DWORD>(b[1]) | static_cast<DWORD>(b[2]) << 8 | static_cast<DWORD>(b[3]) << 16 |
                                  static_cast<DWORD>(b[4]) << 24;
                if (b[0] == 0x46 && b[5] == 0x07 && ref == g_hangRef[i]) {
                    for (int k = 0; k < kHangLen; ++k) b[k] = 0x01;
                    ++done;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        InterlockedExchange(&g_hangPatched, done);
        InterlockedExchange(&g_hangPending, 0);
    }
    // 8.52 step 2: every proxy with a GTA car is placed each frame (one pass under the lock);
    // parked ones and the turn tests get their single target written once
    const double now = NowSec();
    const bool follow = g_follow != 0;
    if (follow && now - g_lastWrite < kWriteEvery) return;
    {   // nothing to place or write: done (a racy peek is fine: the lock decides)
        bool any = false;
        for (int i = 0; i < g_carCount && !any; ++i)
            any = (follow && g_slots[i].track.valid) || g_slots[i].target.version != g_slots[i].appliedVersion;
        if (!any) return;
    }
    if (!TryAcquireSRWLockExclusive(&g_lock)) return;
    const bool allowed = WriteAllowed();
    const LONG held = g_heldSlot;
    double dtw = now - g_lastWrite;
    if (dtw < 0.0 || dtw > 0.1) dtw = 0.1;
    const float stepMax = static_cast<float>(kSteerRate * dtw);
    for (int k = 0; k < 2; ++k) { // the steer (step 7) belongs to the held proxy only
        const float want = (g_holdSide != 0 && held >= 0) ? g_steer[k] : 0.0f;
        const float d = want - g_steerApplied[k];
        g_steerApplied[k] += d > stepMax ? stepMax : d < -stepMax ? -stepMax : d;
    }
    static LONG steerSlot = -1;
    if (held >= 0 && held != steerSlot) { // a hold on another proxy: the old steer does not move over
        g_steerApplied[0] = g_steerApplied[1] = 0.0f;
        steerSlot = held;
    }
    for (int i = 0; i < g_carCount; ++i) {
        Slot& sl = g_slots[i];
        if (follow && sl.track.valid) {
            // smooth following: place the car where the last GTA sample says it is now
            float p[3], yaw;
            TrackAt(sl.track, now, p, yaw);
            // step 6: the hold on the rear bumper; step 7: steered onto it while he holds
            const float along = kHoldBack - sl.track.back;
            const float* f = sl.track.fwd;
            p[0] += f[0] * along;
            p[2] += f[2] * along;
            if (i == steerSlot) { // the steer fades out on the proxy it was on after a let-go
                p[0] += g_steerApplied[0];
                p[2] += g_steerApplied[1];
            }
            // test 24 ("floated in the air while going uphill"): the proxy stands level, so the hold
            // is at the GTA car's centre height; behind a car going uphill the road is lower. The
            // proxy is put at the height of the spot behind the bumper on the car's slope.
            p[1] -= sl.track.rise * (sl.track.back + kGap);
            for (int k = 0; k < 3; ++k) sl.target.pos[k] = p[k];
            sl.target.yaw = yaw;
            sl.target.gen = g_levelGen;
            ++sl.target.version;
        }
        ApplySlotLocked(i, allowed);
    }
    if (follow) g_lastWrite = now;
    // step 8: face the held skater the GTA car's way
    const unsigned long sk = g_skater;
    if (follow && g_holdSide != 0 && held >= 0 && held < g_carCount && sk && g_slots[held].track.valid) {
        __try {
            float* m = reinterpret_cast<float*>(sk + kSkaterRows);
            const float* f = g_slots[held].track.fwd;
            const float have = atan2f(m[8], m[10]), want = atan2f(f[0], f[2]);
            const float d = WrapPi(want - have);
            const float ad = fabsf(d) * 57.2958f;
            if (ad > 1.0f && ad < 90.0f) { // test 24: 177-178 deg at a grab turned him round; leave those
                const float c = cosf(d), sn = sinf(d);
                for (int r = 0; r < 3; r += 2) { // rows 0 and 2 turn about the up axis; row 1 (up) stays
                    float* v = m + 4 * r;
                    const float x = v[0] * c + v[2] * sn, z = -v[0] * sn + v[2] * c;
                    v[0] = x;
                    v[2] = z;
                }
                if (ad > g_faceFixMax) g_faceFixMax = ad;
                g_faceFixLast = d * 57.2958f;
                InterlockedIncrement(&g_faceFixes);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void VehScanSecond(bool inOurLevel, void (*log)(const char*, ...))
{
    g_log = log;
    if (!inOurLevel) {
        if (g_inLevelFor || g_scanState) {
            AcquireSRWLockExclusive(&g_lock);
            InterlockedIncrement(&g_levelGen);
            if (g_scanState == 2) InterlockedExchange(&g_scanState, 0);
            for (int c = 0; c < g_carCount; ++c) {
                g_cars[c].hits = 0;
                memcpy(g_cars[c].cur, g_cars[c].p, sizeof(g_cars[c].cur));
                g_cars[c].yaw = 0.0f;
            }
            InterlockedExchange(&g_follow, 0);
            ResetSlots();
            InterlockedExchange(&g_holdSide, 0);
            InterlockedExchange(&g_heldSlot, -1);
            g_srcCount = 0;
            g_lastFailedN = 0;
            g_followPending = false;
            if (g_twinState == 2) InterlockedExchange(&g_twinState, 0);
            ReleaseSRWLockExclusive(&g_lock);
        }
        g_inLevelFor = 0;
        return;
    }
    if (g_scanState != 0) return;
    if (++g_inLevelFor < kWaitInLevelS) return;
    if (!LoadCars()) { InterlockedExchange(&g_scanState, 2); return; }
    log("vehicles: GTA map up for %lu s: looking for the map's %d reTHAWed vehicle(s) in memory (read only)",
        g_inLevelFor, g_carCount);
    g_scanGen = g_levelGen;
    InterlockedExchange(&g_scanState, 1);
    HANDLE h = CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_scanState, 0);
}

// How many of the test car's rotation copies still hold the turn we wrote (the game may rebuild them).
static void ArmWatch(const int* copies, int n, const char* when, void (*log)(const char*, ...))
{
    if (!g_mods[0].lo) FillMods();
    if (!g_watchHandler) g_watchHandler = AddVectoredExceptionHandler(1, WatchHandler);
    if (!g_watchHandler) { log("write watch: could not add the exception handler"); return; }
    const Car& car = TestCar();
    if (n > 4) n = 4;
    g_watchCount = n;
    for (int i = 0; i < n; ++i) {
        const Copy& cp = car.c[copies[i]];
        g_watchCopy[i] = copies[i];
        g_watchAddr[i] = RowsAddr(cp);
    }
    for (int i = 0; i < kMaxWatchHits; ++i) g_watchHits[i] = WatchHit{};
    InterlockedExchange(&g_watchN, 0);
    InterlockedExchange(&g_watchOwn, 0);
    g_watchFrom = GetTickCount();
    InterlockedExchange(&g_watchArmed, 1);
    const int threads = SetWatches(n);
    g_watchUntil = GetTickCount() + kWatchMs;
    char list[64] = "";
    for (int i = 0; i < n; ++i) {
        const size_t l = strlen(list);
        _snprintf_s(list + l, sizeof(list) - l, _TRUNCATE, " %d", copies[i]);
    }
    log("write watch (%s): watching copies%s on %d thread(s) for %lu ms", when, list, threads, kWatchMs);
}

static int RotationsHeld(int& total, int* failed = nullptr, int* nFailed = nullptr, char* heldList = nullptr, size_t heldCap = 0)
{
    if (nFailed) *nFailed = 0;
    if (heldList && heldCap) heldList[0] = 0;
    const Car& car = TestCar();
    int held = 0;
    total = 0;
    for (int h = 0; h < car.hits; ++h) {
        const Copy& cp = car.c[h];
        if (!cp.rows) continue;
        ++total;
        float m[12];
        bool ok = false;
        CopyMem(m, RowsAddr(cp), sizeof(m), ok);
        if (!ok) continue;
        bool same = true;
        for (int r = 0; r < 3 && same; ++r) {
            float want[3];
            TurnRow(cp.r0[r], car.yaw, want);
            for (int k = 0; k < 3; ++k)
                if (fabsf(m[4 * r + k] - want[k]) > 0.01f) same = false;
        }
        if (same) {
            ++held;
            if (heldList) {
                const size_t l = strlen(heldList);
                _snprintf_s(heldList + l, heldCap - l, _TRUNCATE, " %d", h);
            }
        } else if (failed && nFailed) {
            // the turns the others are built from go first in the (4-place) watch list
            if (cp.rows == 3 || cp.rows == 2) {
                if (*nFailed == 4) --*nFailed;
                for (int i = *nFailed; i > 0; --i) failed[i] = failed[i - 1];
                failed[0] = h;
                ++*nFailed;
            } else if (*nFailed < 4) {
                failed[(*nFailed)++] = h;
            }
        }
    }
    return held;
}

void VehTestTick(LinkBlock* link, bool inOurLevel, const float* pos, const float* at, const float* vel,
                 void (*log)(const char*, ...))
{
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool followKey = (GetAsyncKeyState(kFollowKey) & 0x8000) != 0;
    const bool noseKey = (GetAsyncKeyState(kNoseKey) & 0x8000) != 0;
    const bool followPressed = followKey && !g_followKeyWas, nosePressed = noseKey && !g_noseKeyWas;
    g_followKeyWas = followKey;
    g_noseKeyWas = noseKey;
    if (!inOurLevel) {
        if (g_watchArmed) {
            SetWatches(0);
            InterlockedExchange(&g_watchArmed, 0);
            InterlockedExchange(&g_symWatch, 0);
            log("write watch: the level went; watches cleared");
        }
        g_symDone = false;
        return;
    }
    {   // watchdog: is reTHAWed's game thread stuck?
        const DWORD t = GetTickCount();
        const DWORD last = static_cast<DWORD>(g_lastGameTick);
        if (g_gameThreadId && last && t - last > kStuckMs && g_scanState == 2) {
            if (g_stuckSamples < kStuckSamples && t >= g_nextStuckSample) {
                if (!g_mods[0].lo) FillMods();
                if (g_stuckSamples == 0)
                    log("watchdog: reTHAWed's game thread has not run for %lu ms (follow mode %s): sampling where it is",
                        t - last, g_follow ? "on" : "off");
                StuckShot shot;
                TakeShot(static_cast<DWORD>(g_gameThreadId), shot);
                LogShot(shot, ++g_stuckSamples, log);
                if (shot.ok && (g_stuckSamples == 1 || g_stuckSamples == 3)) {
                    DWORD addrs[97];
                    int na = 0;
                    addrs[na++] = shot.eip;
                    for (int i = 0; i < shot.words && na < 97; ++i) addrs[na++] = shot.stack[i];
                    DumpCode(addrs, na < 25 ? na : 25, "watchdog", log);
                }
                if (shot.ok && g_stuckSamples == 1) {
                    // the script bytes being read (esi), and what the reader holds in ebx / edi / eax
                    SaveMem("script", shot.esi - 0x800, 0x1000);
                    SaveMem("ebx", shot.ebx - 0x100, 0x400);
                    SaveMem("edi", shot.edi - 0x100, 0x400);
                    SaveMem("eax", shot.eax - 0x100, 0x400);
                    BYTE b[64] = {};
                    bool ok = false;
                    CopyMem(b, shot.esi, sizeof(b), ok);
                    char line[300];
                    int l = _snprintf_s(line, sizeof(line), _TRUNCATE, "watchdog: script bytes at esi 0x%08lX:", shot.esi);
                    for (int i = 0; ok && i < 64 && l > 0 && l < static_cast<int>(sizeof(line)) - 4; ++i)
                        l += _snprintf_s(line + l, sizeof(line) - l, _TRUNCATE, "%s%02X", (i % 4) ? "" : " ", b[i]);
                    log("%s", line);
                    log("watchdog: saved the script bytes around esi and the memory at ebx, edi, eax to extracted\\veh_code (read only)");
                    if (g_mods[0].lo == 0x400000) LogSymBytes("at the freeze", log);
                }
                g_nextStuckSample = t + 400;
            }
        } else if (g_stuckSamples && t - last < 500) {
            log("watchdog: reTHAWed's game thread is running again");
            g_stuckSamples = 0;
        }
    }
    const bool ready = g_scanState == 2 && g_carCount > 0 && TestCar().hits > 0;
    if (g_hangPatched >= 0) {
        log("script fix: blanked %ld of %d hang line(s) in reTHAWed's memory (nothing on disk is changed)", static_cast<long>(g_hangPatched),
            g_hangCount);
        InterlockedExchange(&g_hangPatched, -1);
    }
    if (kSymWatchOn && ready && !g_symDone && !g_watchArmed) {
        g_symDone = true;
        if (!g_mods[0].lo) FillMods();
        if (g_mods[0].lo == 0x400000) {
            if (!g_watchHandler) g_watchHandler = AddVectoredExceptionHandler(1, WatchHandler);
            LogSymBytes("at the start", log);
            if (g_watchHandler) {
                for (int i = 0; i < 4; ++i) { g_watchAddr[i] = kSymAddr + 4 * i; g_watchCopy[i] = -1; }
                g_watchCount = 4;
                for (int i = 0; i < kMaxWatchHits; ++i) g_watchHits[i] = WatchHit{};
                InterlockedExchange(&g_watchN, 0);
                InterlockedExchange(&g_watchOwn, 0);
                g_symLogged = 0;
                g_watchFrom = GetTickCount();
                InterlockedExchange(&g_watchArmed, 1);
                InterlockedExchange(&g_symWatch, 1);
                g_watchUntil = 0xFFFFFFFFul;
                const int threads = SetWatches(4);
                log("symbol watch: watching 16 bytes at THAW.exe 0x%08lX on %d thread(s) until the level goes", kSymAddr, threads);
            }
        } else {
            log("symbol watch: THAW.exe is not at 0x400000; skipped");
        }
    }
    if (g_symWatch && g_watchN > g_symLogged) LogSymHits(log);
    if (followPressed && shift) {
        // Shift+F2: follow mode off, the test car put ~7.6 m in front of the skater (turn test, 2026-10-15)
        if (!ready || !pos) {
            log("vehicles (Shift+F2): not ready");
        } else {
            float fx = vel ? vel[0] : 0.0f, fz = vel ? vel[2] : 0.0f;
            if (fx * fx + fz * fz < 100.0f * 100.0f && at) { fx = at[0]; fz = at[2]; }
            const float len = sqrtf(fx * fx + fz * fz);
            if (len > 0.001f) {
                InterlockedExchange(&g_follow, 0);
                if (link) link->thawSkitchId = -1;
                const float t3[3] = {pos[0] + fx / len * 300.0f, pos[1], pos[2] + fz / len * 300.0f};
                SetTarget(g_fc, t3, TestCar().yaw);
                g_turnCheckAt = GetTickCount() + 1000;
                log("vehicles (Shift+F2): follow mode off; the test car put in front of the skater at %.0f %.0f %.0f, turned %.0f deg",
                    t3[0], t3[1], t3[2], TestCar().yaw * 57.2958f);
            }
        }
    } else if (followPressed || (g_followPending && ready)) {
        // test 22: F2 before the scan was done was dropped; now it is kept and follow mode starts when ready
        if (!ready) {
            g_followPending = !g_followPending;
            log("vehicles (F2): not ready yet (the vehicle scan runs %lu s into the level and takes about 10 s; needs a map built with "
                "--proxy-cars): follow mode %s", kWaitInLevelS, g_followPending ? "will turn ON when it is done" : "will stay off");
        } else {
            if (g_followPending) {
                g_followPending = false;
                if (g_follow) goto followDone; // already on
            }
            InterlockedExchange(&g_follow, g_follow ? 0 : 1);
            AcquireSRWLockExclusive(&g_lock);
            for (int i = 0; i < kMaxCars; ++i) { // 8.52: every proxy stops where it is and is free again
                g_slots[i].track.valid = false;
                g_slots[i].carId = -1;
                g_slots[i].lastId = -2;
            }
            ReleaseSRWLockExclusive(&g_lock);
            InterlockedExchange(&g_holdSide, 0);
            InterlockedExchange(&g_heldSlot, -1);
            if (link) link->thawSkitching = 0;
            if (!g_follow && link) link->thawSkitchId = -1;
            log("vehicles (F2): follow mode %s%s", g_follow ? "ON: the proxies follow the moving GTA cars near the skater (up to 4 published)"
                                                           : "OFF: the proxies stay where they are",
                g_follow ? "; our own skitch (8.42) is off meanwhile, so Up is reTHAWed's" : "");
        }
    }
followDone:
    if (nosePressed && ready && !g_follow) {
        // F1 with follow mode off: turn the test car a quarter turn where it stands. From the
        // second F1 on, the copies the last check saw put back are watched from now (test 7).
        if (g_lastFailedN && !g_watchArmed) ArmWatch(g_lastFailed, g_lastFailedN, "from F1", log);
        SetTarget(g_fc, TestCar().cur, TestCar().yaw + 1.5707963f);
        g_turnCheckAt = GetTickCount() + 1000;
        log("vehicles (F1): the test car turned a quarter turn where it stands (now %.0f deg from its start)",
            (TestCar().yaw + 1.5707963f) * 57.2958f);
    } else if (nosePressed && ready) {
        g_noseQuarter = (g_noseQuarter + 1) % 4;
        log("vehicles (F1): the test car's nose turned: now %d degrees from the start guess", g_noseQuarter * 90);
    }
    if (ready && g_turnCheckAt && GetTickCount() >= g_turnCheckAt) {
        g_turnCheckAt = 0;
        int total = 0, failed[4] = {}, nFailed = 0;
        char heldList[200];
        const int held = RotationsHeld(total, failed, &nFailed, heldList, sizeof(heldList));
        log("vehicles: 1 s after the move: the test car at %.0f %.0f %.0f, turned %.0f deg; %d of %d rotations next to its copies still "
            "hold that turn (copies:%s)", TestCar().cur[0], TestCar().cur[1], TestCar().cur[2], TestCar().yaw * 57.2958f, held, total,
            held ? heldList : " none");
        g_lastFailedN = nFailed;
        const float turned = fmodf(fabsf(TestCar().yaw), 6.2831853f);
        if (total && held == total && turned > 0.1f && turned < 6.18f) StartTwinScan();
        for (int i = 0; i < nFailed; ++i) g_lastFailed[i] = failed[i];
        if (nFailed && !g_watchArmed) ArmWatch(failed, nFailed, "1 s after the move", log);
    }
    if (g_watchArmed && GetTickCount() >= g_watchUntil) {
        SetWatches(0);
        InterlockedExchange(&g_watchArmed, 0);
        LogWatch(log);
    }
    // 8.51 test 15: where the skater is in the test car's own frame (nose / right / up), twice a
    // second while he is within 6 m and follow mode is off. The car's frame is its object turn
    // (object+0xDC: row 0 = right, row 2 = nose), read live. Settles where the grab point is.
    if (ready && pos && !g_follow) {
        static ULONGLONG nextRel = 0;
        const ULONGLONG t = GetTickCount64();
        const Car& car = TestCar();
        const float d[3] = {pos[0] - car.cur[0], pos[1] - car.cur[1], pos[2] - car.cur[2]};
        if (t >= nextRel && d[0] * d[0] + d[2] * d[2] < 240.0f * 240.0f) {
            nextRel = t + 500;
            float m[12] = {};
            bool ok = false;
            for (int h = 0; h < car.hits && !ok; ++h)
                if (car.c[h].rows == 2) CopyMem(m, RowsAddr(car.c[h]), sizeof(m), ok);
            if (ok) {
                const float right = d[0] * m[0] + d[1] * m[1] + d[2] * m[2];
                const float nose = d[0] * m[8] + d[1] * m[9] + d[2] * m[10];
                const float face = at ? at[0] * m[8] + at[2] * m[10] : 0.0f;
                log("vehicles: skater near the test car (turned %.0f deg): %.0f along its nose, %.0f to its right, %.0f up (inches); "
                    "he faces %s its nose (%.2f)", car.yaw * 57.2958f, nose, right, d[1], face > 0.5f ? "the same way as" :
                    face < -0.5f ? "against" : "across", face);
            }
        }
    }
    // 8.52 step 1 (read only): the cars the GTA side publishes for the proxy pool, once a second
    if (link) {
        static ULONGLONG nextPoolLog = 0;
        const ULONGLONG pt = GetTickCount64();
        if (pt >= nextPoolLog) {
            nextPoolLog = pt + 1000;
            PoolCar pc[kPoolCars];
            const int n = ReadPoolCars(link, pc);
            if (n > 0) {
                char buf[512];
                int len = snprintf(buf, sizeof(buf), "vehicles: pool list from GTA: %d car(s):", n);
                for (int i = 0; i < n && len > 0 && len < static_cast<int>(sizeof(buf)); ++i) {
                    const float dx = pos ? pc[i].pos[0] - pos[0] : 0.0f, dz = pos ? pc[i].pos[2] - pos[2] : 0.0f;
                    len += snprintf(buf + len, sizeof(buf) - len, " GTA car %ld (%.0f in from the skater, %.0f in/s, rear %.0f in)%s",
                                    static_cast<long>(pc[i].id), sqrtf(dx * dx + dz * dz),
                                    sqrtf(pc[i].vel[0] * pc[i].vel[0] + pc[i].vel[2] * pc[i].vel[2]), pc[i].back,
                                    pc[i].id == link->gtaSkitchId ? " = the one-car field" : "");
                }
                log("%s", buf);
            }
        }
    }
    if (!ready || !g_follow || !link) return;
    const ULONGLONG now = GetTickCount64();
    const DWORD t = GetTickCount();
    // 8.52 step 2: every proxy follows its own GTA car. The GTA side publishes up to kPoolCars
    // moving cars near CJ (the held one first); a car is wanted if it passes the test below.
    PoolCar pc[kPoolCars];
    const int pn = ReadPoolCars(link, pc);
    bool wanted[kPoolCars] = {};
    const LONG heldSlot = g_heldSlot;
    const LONG heldCar = (g_holdSide != 0 && heldSlot >= 0 && heldSlot < g_carCount) ? g_slots[heldSlot].carId : -1;
    bool heldInList = false;
    for (int i = 0; i < pn; ++i) {
        const float* cp = pc[i].pos;
        const float* cf = pc[i].fwd;
        const float* cv = pc[i].vel;
        bool ok = true;
        for (int k = 0; k < 3; ++k)
            if (!(cp[k] > -1.0e6f && cp[k] < 1.0e6f) || !(cf[k] > -2.0f && cf[k] < 2.0f) || !(cv[k] > -1.0e5f && cv[k] < 1.0e5f))
                ok = false; // nonsense
        // test 36 (test 35: the pool was idle most of the time): every car in the list is wanted. The
        // test-26 rule (only a car heading his way with him behind its centre) was made for the single
        // proxy, which stood at one car and was grabbed beside the wrong one; each proxy now stands
        // under its own car, and a proxy is solid in reTHAWed, so the nearest cars stop his skater.
        wanted[i] = ok;
        if (ok && pc[i].id == heldCar) heldInList = true;
    }
    // test 21: the held car must not vanish under him; if it is missing from the list for 1 s, the hold ends
    {
        static ULONGLONG goneSince = 0;
        if (heldCar >= 0 && !heldInList) {
            if (!goneSince) {
                goneSince = now;
                log("vehicles: the held car (GTA car %ld, proxy %ld) is not in the GTA list: proxy kept where it was", static_cast<long>(heldCar),
                    static_cast<long>(heldSlot));
            } else if (now - goneSince >= 1000) {
                InterlockedExchange(&g_holdSide, 0);
                InterlockedExchange(&g_heldSlot, -1);
                goneSince = 0;
                log("vehicles: GTA car %ld is gone for 1 s: the hold ends", static_cast<long>(heldCar));
            }
        } else {
            goneSince = 0;
        }
    }
    // each wanted car keeps its proxy, or takes a free one (parked); the samples go to the tracks
    int noRoom = 0;
    for (int i = 0; i < pn; ++i) {
        if (!wanted[i]) continue;
        const LONG id = pc[i].id;
        int s = -1;
        for (int k = 0; k < g_carCount; ++k)
            if (g_slots[k].carId == id) { s = k; break; }
        if (s < 0)
            for (int k = 0; k < g_carCount; ++k)
                if (g_slots[k].carId == -1) { s = k; break; }
        if (s < 0) { ++noRoom; continue; }
        Slot& sl = g_slots[s];
        sl.lastSeen = t;
        const float* cp = pc[i].pos;
        const float* cf = pc[i].fwd;
        const float* cv = pc[i].vel;
        const float target[3] = {cp[0], cp[1] - kCarDrop, cp[2]};
        const float heading = atan2f(cf[0], cf[2]) + g_noseQuarter * 1.5707963f;
        // a new sample (the GTA side writes about once a GTA frame): hand it to the track
        bool fresh = id != sl.lastId;
        for (int k = 0; k < 3; ++k)
            if (fabsf(cp[k] - sl.lastCp[k]) > 0.01f) fresh = true;
        if (fresh) {
            memcpy(sl.lastCp, cp, sizeof(sl.lastCp));
            sl.lastId = id;
            const double tnow = NowSec();
            const float base = StartHeading(g_cars[s]);
            AcquireSRWLockExclusive(&g_lock);
            Track& tr = sl.track;
            float was[3] = {}, wasYaw = 0.0f;
            const bool cont = tr.valid && tr.id == id;
            if (cont) TrackAt(tr, tnow, was, wasYaw);
            tr.id = id;
            for (int k = 0; k < 3; ++k) { tr.pos[k] = target[k]; tr.vel[k] = cv[k]; }
            {
                const float fl = sqrtf(cf[0] * cf[0] + cf[2] * cf[2]);
                tr.fwd[0] = fl > 0.1f ? cf[0] / fl : 0.0f;
                tr.fwd[1] = 0.0f;
                tr.fwd[2] = fl > 0.1f ? cf[2] / fl : 1.0f;
                float rise = fl > 0.3f ? cf[1] / fl : 0.0f;
                if (rise > 0.6f) rise = 0.6f;
                if (rise < -0.6f) rise = -0.6f;
                tr.rise = rise;
                const float b = pc[i].back;
                tr.back = (b > 10.0f && b < 500.0f) ? b : kDefaultBack;
            }
            tr.yaw = heading - base;
            tr.t = tnow;
            float e2 = 0.0f;
            for (int k = 0; k < 3; ++k) { tr.err[k] = cont ? was[k] - target[k] : 0.0f; e2 += tr.err[k] * tr.err[k]; }
            tr.errYaw = cont ? WrapPi(wasYaw - tr.yaw) : 0.0f;
            const float e = sqrtf(e2);
            if (!cont || e > kSnapDist || fabsf(tr.errYaw) > 0.785f) {
                for (int k = 0; k < 3; ++k) tr.err[k] = 0.0f;
                tr.errYaw = 0.0f;
                InterlockedIncrement(&g_snaps);
            } else {
                g_errSum += e;
                ++g_errN;
            }
            tr.valid = true;
            ReleaseSRWLockExclusive(&g_lock);
            InterlockedIncrement(&g_samples);
        }
        if (sl.carId != id) {
            log("vehicles: proxy %d now follows GTA car %ld (at %.0f %.0f %.0f)", s, static_cast<long>(id), cp[0], cp[1], cp[2]);
            sl.carId = id;
            sl.waitLogged = false;
        }
    }
    static int noRoomMax = 0;
    if (noRoom > noRoomMax) noRoomMax = noRoom;
    // a proxy whose car has not been wanted for kNoCarParkMs goes back to its own spot - but never
    // while he holds it (a car jumping away under him is the kind of thing that broke the rails,
    // 8.41). Test 25: it used to wait while he was merely near it, so an invisible proxy stood in
    // the road and he could grab the air. Test 34 (test 33: after a skitch jump he landed on the
    // proxy's roof; it parked 0.4 s later and carried him to its own spot, East Los Santos in GTA):
    // it also waits while he is on it or right next to it (the car's own box plus a hand's reach).
    for (int s = 0; s < g_carCount; ++s) {
        Slot& sl = g_slots[s];
        if (sl.carId == -1 || t - sl.lastSeen <= kNoCarParkMs) continue;
        if (g_holdSide != 0 && s == g_heldSlot) continue;
        bool onIt = false;
        float oa = 0.0f, orr = 0.0f, ou = 0.0f;
        if (pos) {
            float fw[3] = {};
            AcquireSRWLockShared(&g_lock);
            memcpy(fw, sl.track.fwd, sizeof(fw));
            ReleaseSRWLockShared(&g_lock);
            const Car& pcar = g_cars[s];
            const float e0 = pos[0] - pcar.cur[0], e1 = pos[1] - pcar.cur[1], e2 = pos[2] - pcar.cur[2];
            const float fl = sqrtf(fw[0] * fw[0] + fw[2] * fw[2]);
            if (fl > 0.1f) {
                oa = (e0 * fw[0] + e2 * fw[2]) / fl;
                orr = (e0 * fw[2] - e2 * fw[0]) / fl;
            } else { // no nose known: a round box
                oa = sqrtf(e0 * e0 + e2 * e2);
                orr = 0.0f;
            }
            ou = e1;
            onIt = fabsf(oa) < kParkWaitAlong && fabsf(orr) < kParkWaitSide && ou > -kParkWaitBelow;
        }
        if (onIt) {
            if (!sl.waitLogged) {
                sl.waitLogged = true;
                log("vehicles: proxy %d would park, but the skater is on it / next to it (%.0f in along, %.0f in right, %.0f in up): "
                    "it waits", s, oa, orr, ou);
            }
            continue;
        }
        sl.waitLogged = false;
        AcquireSRWLockExclusive(&g_lock);
        sl.track.valid = false;
        ReleaseSRWLockExclusive(&g_lock);
        SetTarget(s, g_cars[s].p, 0.0f);
        log("vehicles: proxy %d (GTA car %ld not wanted for %lu ms) parked at its own spot", s, static_cast<long>(sl.carId), kNoCarParkMs);
        sl.carId = -1;
        sl.lastId = -2;
    }
    // the focus proxy for the grab tests: the held one, else the nearest one that follows a car
    int fs = -1;
    if (g_holdSide != 0 && g_heldSlot >= 0 && g_heldSlot < g_carCount) {
        fs = g_heldSlot;
    } else if (pos) {
        float best = 1.0e18f;
        for (int s = 0; s < g_carCount; ++s) {
            if (g_slots[s].carId == -1) continue;
            const float dx = pos[0] - g_cars[s].cur[0], dz = pos[2] - g_cars[s].cur[2];
            if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; fs = s; }
        }
    }
    static int prevFs = -1;
    const bool focusChanged = fs != prevFs;
    prevFs = fs;
    const Car& car = g_cars[fs >= 0 ? fs : g_fc];
    const LONG focusCar = fs >= 0 ? g_slots[fs].carId : -1;
    // keep the GTA side publishing the car he holds or is at (8.42; with the pool it is also first in the list)
    {
        float e[3] = {};
        if (pos)
            for (int k = 0; k < 3; ++k) e[k] = pos[k] - car.cur[k];
        const bool close = pos && focusCar >= 0 && e[0] * e[0] + e[2] * e[2] < kKeepNear * kKeepNear;
        link->thawSkitchId = close ? focusCar : -1;
        // step 6: is he holding the proxy's back corner? (his offset in the proxy's frame: about
        // kHoldBack behind it and kHoldSide to one side, for 0.3 s; off again after 0.3 s away)
        static ULONGLONG holdSince = 0, awaySince = 0;
        const ULONGLONG tnow = GetTickCount64();
        float fw[3] = {};
        Track ftr = {};
        AcquireSRWLockShared(&g_lock);
        if (fs >= 0) ftr = g_slots[fs].track;
        ReleaseSRWLockShared(&g_lock);
        const bool tv = fs >= 0 && ftr.valid;
        memcpy(fw, ftr.fwd, sizeof(fw));
        if (focusChanged) holdSince = 0;
        int side = 0;
        if (pos && tv && g_holdSide == 0) {
            const float nose = e[0] * fw[0] + e[2] * fw[2];
            const float right = e[0] * fw[2] - e[2] * fw[0];
            // test 21: anywhere across the proxy's back; test 24 ("hung to the side" once, not seen as a
            // hold): wider still (up to 120 in to either side, 40-160 in behind), but only if he moves
            // with the car (within 150 in/s of its speed), so skating behind it does not count
            // test 25: the speed test never passed (reTHAWed does not update the skater's velocity
            // while he skitches), so no hold was seen at all. Instead: he stays put relative to the
            // proxy (within 20 in of where he was) for the 0.3 s before the hold starts.
            if (nose > -160.0f && nose < -40.0f && fabsf(right) < 120.0f && fabsf(e[1]) < 80.0f)
                side = right > 0.0f ? 1 : -1;
            static float startNose = 0.0f, startRight = 0.0f;
            static bool startSet = false;
            if (focusChanged) startSet = false; // 8.52: another proxy: start again
            if (side == 0) {
                startSet = false;
            } else if (!startSet) {
                startNose = nose;
                startRight = right;
                startSet = true;
            } else if (fabsf(nose - startNose) > 20.0f || fabsf(right - startRight) > 20.0f) {
                // moving about relative to it: not attached; start the 0.3 s again from here
                startNose = nose;
                startRight = right;
                side = 0;
            }
        }
        // step 6b (test 28): reTHAWed stops updating the skater's own speed while he skitches: it reads
        // exactly 0 from the frame he grabs until the frame he lets go, while his position moves with the
        // car. Seen in all four holds of test 28; the stay-put test above saw the grab 0.3-0.7 s late and
        // the let-go 0.8-2.9 s late (he rode next to the car meanwhile). So: speed 0 while he moves with
        // a moving car = holding; his own speed back = let go, at once.
        static float prevPos[3] = {};
        static double prevPosT = 0.0;
        static bool still = false, rolling = false;
        static double sigSince = 0.0;
        static int fastReads = 0;
        const double st = NowSec();
        const float fieldSpeed = vel ? sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]) : -1.0f;
        if (fieldSpeed > kOwnSpeedLetGo) ++fastReads; else fastReads = 0;
        if (pos && (prevPosT == 0.0 || st - prevPosT >= 0.05)) {
            if (prevPosT != 0.0 && st - prevPosT < 0.5) {
                const float dx = pos[0] - prevPos[0], dz = pos[2] - prevPos[2];
                const float moved = sqrtf(dx * dx + dz * dz) / static_cast<float>(st - prevPosT);
                still = fieldSpeed >= 0.0f && fieldSpeed < 1.0f;
                rolling = moved > kSkitchMoveMin;
            }
            memcpy(prevPos, pos, sizeof(prevPos));
            prevPosT = st;
        }
        const bool skitchSign = still && rolling;
        if (!skitchSign) sigSince = 0.0;
        else if (sigSince == 0.0) sigSince = st;
        int signSide = 0;
        if (pos && tv && g_holdSide == 0 && sigSince != 0.0 && st - sigSince >= 0.1) {
            const float nose = e[0] * fw[0] + e[2] * fw[2];
            const float right = e[0] * fw[2] - e[2] * fw[0];
            if (nose > -240.0f && nose < 40.0f && fabsf(right) < 160.0f && fabsf(e[1]) < 80.0f)
                signSide = right > 0.0f ? 1 : -1;
        }
        // step 7: the point he should be at (kGap behind the GTA car's bumper, on its centre line), and how far off he is
        static double lastSteer = 0.0;
        const double sn = NowSec();
        double sdt = sn - lastSteer;
        lastSteer = sn;
        if (sdt < 0.0 || sdt > 0.1) sdt = 0.1;
        float errAlong = 0.0f, errRight = 0.0f, errLen = 1.0e9f;
        if (pos && tv) {
            float c[3], yaw;
            float back;
            AcquireSRWLockShared(&g_lock);
            TrackAt(g_slots[fs].track, sn, c, yaw);
            back = g_slots[fs].track.back;
            // TrackAt includes the fading sample difference: fine for where the GTA car is now
            ReleaseSRWLockShared(&g_lock);
            const float want[2] = {c[0] - fw[0] * (back + kGap), c[2] - fw[2] * (back + kGap)};
            const float ex = want[0] - pos[0], ez = want[1] - pos[2];
            errLen = sqrtf(ex * ex + ez * ez);
            errAlong = -(ex * fw[0] + ez * fw[2]);  // + = he is ahead of the point
            errRight = -(ex * fw[2] - ez * fw[0]);  // + = he is to its right
            if (g_holdSide != 0 && errLen < kLoseDist && errLen > kSteerDead) {
                float s0 = g_steer[0] + ex * kSteerGain * static_cast<float>(sdt);
                float s1 = g_steer[1] + ez * kSteerGain * static_cast<float>(sdt);
                // test 26: split into along / across the car; along is only trimmed
                float sa = s0 * fw[0] + s1 * fw[2], sr = s0 * fw[2] - s1 * fw[0];
                if (sa > kSteerAlongMax) sa = kSteerAlongMax;
                if (sa < -kSteerAlongMax) sa = -kSteerAlongMax;
                if (sr > kSteerMax) sr = kSteerMax;
                if (sr < -kSteerMax) sr = -kSteerMax;
                g_steer[0] = sa * fw[0] + sr * fw[2];
                g_steer[1] = sa * fw[2] - sr * fw[0];
            }
        }
        // test 32 (test 31: uphill, the held skater stopped dead for 0.9 s at one spot while the car drove
        // on, then reTHAWed let go): the last 0.5 s of the hold is kept, and when he falls more than 60 in
        // off the spot it is written out, followed by every read for the next 2 s ("stall"), to find out
        // what stops him. At most 3 such logs per hold.
        {
            struct StallRow {
                double t;
                float pos[3], vel[3], at[3], errAlong, errRight, proxy[3], face;
            };
            static StallRow ring[32];
            static int ringN = 0, ringHead = 0, stallLogs = 0;
            static double stallUntil = 0.0, lastRow = 0.0, holdStart = 0.0;
            static int wasHold = 0;
            if (g_holdSide != 0 && wasHold == 0) holdStart = sn;
            if (g_holdSide == 0) {
                if (wasHold != 0 && stallUntil != 0.0) log("vehicles: stall log ends (he let go)");
                ringN = ringHead = stallLogs = 0;
                stallUntil = 0.0;
            } else if (pos && tv && sn - lastRow >= 0.015) {
                lastRow = sn;
                StallRow r = {};
                r.t = sn;
                for (int k = 0; k < 3; ++k) {
                    r.pos[k] = pos[k];
                    r.vel[k] = vel ? vel[k] : 0.0f;
                    r.at[k] = at ? at[k] : 0.0f;
                    r.proxy[k] = g_slots[fs].target.pos[k]; // a racy read: only for the log
                }
                r.errAlong = errAlong;
                r.errRight = errRight;
                r.face = g_faceFixLast;
                auto put = [&](const StallRow& w, const char* what) {
                    log("vehicles: stall %s %.3f s: skater %.1f %.1f %.1f | own speed %.1f %.1f %.1f | facing %.2f %.2f %.2f | "
                        "%.0f in ahead / %.0f in right of the spot | test car at %.1f %.1f %.1f | facing fix %.0f deg",
                        what, w.t - sn, w.pos[0], w.pos[1], w.pos[2], w.vel[0], w.vel[1], w.vel[2], w.at[0], w.at[1], w.at[2],
                        w.errAlong, w.errRight, w.proxy[0], w.proxy[1], w.proxy[2], w.face);
                };
                if (stallUntil != 0.0 && sn <= stallUntil) {
                    put(r, "now");
                } else {
                    if (stallUntil != 0.0) {
                        log("vehicles: stall log ends (2 s); he is %.0f in from the spot", errLen);
                        stallUntil = 0.0;
                    }
                    if (errLen > 60.0f && stallLogs < 3 && sn - holdStart >= 1.0) { // not while a grab is pulled in
                        ++stallLogs;
                        log("vehicles: STALL %d: the held skater is %.0f in from the spot behind GTA car %ld; the last %d reads, "
                            "then every read for 2 s (times from now):",
                            stallLogs, errLen, static_cast<long>(focusCar), ringN);
                        for (int i = 0; i < ringN; ++i) put(ring[(ringHead - ringN + i + 32) % 32], "before");
                        put(r, "now");
                        stallUntil = sn + 2.0;
                        ringN = 0;
                    }
                }
                ring[ringHead] = r;
                ringHead = (ringHead + 1) % 32;
                if (ringN < 32) ++ringN;
            }
            wasHold = g_holdSide;
        }
        // test 26 ("grabbed what looked like the air and wasn't skitching with it"): the skater was
        // 1-2.5 m from the proxy for 7 s before a hold was seen, so grabs outside the zone leave no
        // trace. Twice a second while he is within 5 m of it and no hold is seen: where he is.
        static ULONGLONG nextNearLog = 0;
        if (g_holdSide == 0 && pos && tv && tnow >= nextNearLog && e[0] * e[0] + e[2] * e[2] < 200.0f * 200.0f) {
            nextNearLog = tnow + 500;
            const float nn = e[0] * fw[0] + e[2] * fw[2], rr = e[0] * fw[2] - e[2] * fw[0];
            log("vehicles: near (no hold seen): skater %.0f along / %.0f right / %.0f up of proxy %d's centre (GTA car %ld)", nn, rr, e[1],
                fs, static_cast<long>(focusCar));
        }
        static ULONGLONG nextHoldLog = 0;
        if (g_holdSide != 0 && pos && tv && tnow >= nextHoldLog) {
            nextHoldLog = tnow + 500;
            log("vehicles: holding proxy %d: skater %.0f in ahead / %.0f in right of the spot behind GTA car %ld's bumper; steer %.0f in; "
                "facing turned %ld time(s), at most %.0f deg", fs, errAlong, errRight, static_cast<long>(focusCar),
                sqrtf(g_steer[0] * g_steer[0] + g_steer[1] * g_steer[1]), static_cast<long>(InterlockedExchange(&g_faceFixes, 0)),
                g_faceFixMax);
            g_faceFixMax = 0.0f;
        }
        if (g_holdSide == 0) {
            awaySince = 0;
            if (signSide != 0) side = signSide;
            if (side != 0) {
                if (!holdSince) holdSince = tnow;
                if (signSide != 0 || tnow - holdSince >= 300) {
                    g_steer[0] = 0.0f;
                    g_steer[1] = 0.0f;
                    InterlockedExchange(&g_heldSlot, fs);
                    InterlockedExchange(&g_holdSide, side);
                    log("vehicles: the skater holds proxy %d's back (%.0f along / %.0f right of its centre): steering him onto GTA car "
                        "%ld's centre line, %.0f in behind its bumper (rear %.0f in behind its centre)", fs, e[0] * fw[0] + e[2] * fw[2],
                        e[0] * fw[2] - e[2] * fw[0], static_cast<long>(focusCar), kGap, ftr.back);
                }
            } else {
                holdSince = 0;
            }
        } else if (fastReads >= 2) {
            InterlockedExchange(&g_holdSide, 0);
            InterlockedExchange(&g_heldSlot, -1);
            holdSince = 0;
            awaySince = 0;
            log("vehicles: the skater let go of the held proxy (reTHAWed gives him his own speed again, %.0f in/s; %.0f in from the "
                "spot): the steer goes back", fieldSpeed, errLen);
        } else if (errLen > kLoseDist) {
            if (!awaySince) awaySince = tnow;
            if (tnow - awaySince >= 500) {
                InterlockedExchange(&g_holdSide, 0);
                InterlockedExchange(&g_heldSlot, -1);
                holdSince = 0;
                log("vehicles: the skater let go of the held proxy (%.0f in from the spot): the steer goes back", errLen);
            }
        } else {
            awaySince = 0;
        }
        link->thawSkitching = g_holdSide != 0 ? 1 : 0; // GTA leaves CJ alone (no push-out, no car-hit bail)
    }
    if (now >= g_nextLog) {
        g_nextLog = now + 1000;
        // 8.52: one line for the whole pool: each proxy's GTA car (or parked) and how far he is from it
        char buf[600];
        int len = snprintf(buf, sizeof(buf), "vehicles: pool:");
        for (int s = 0; s < g_carCount && len > 0 && len < static_cast<int>(sizeof(buf)); ++s) {
            const Car& pcar = g_cars[s];
            float d = -1.0f;
            if (pos) {
                const float e0 = pos[0] - pcar.cur[0], e1 = pos[1] - pcar.cur[1], e2 = pos[2] - pcar.cur[2];
                d = sqrtf(e0 * e0 + e1 * e1 + e2 * e2);
            }
            if (g_slots[s].carId >= 0)
                len += snprintf(buf + len, sizeof(buf) - len, " proxy %d -> GTA car %ld (%.0f in from the skater)%s%s", s,
                                static_cast<long>(g_slots[s].carId), d, s == g_heldSlot && g_holdSide != 0 ? " HELD" : "",
                                g_slots[s].waitLogged ? " WAITING" : "");
            else
                len += snprintf(buf + len, sizeof(buf) - len, " proxy %d parked", s);
        }
        const float meanErr = g_errN ? g_errSum / g_errN : 0.0f;
        g_errSum = 0.0f;
        g_errN = 0;
        log("%s | %d wanted car(s) had no free proxy (most at once, last second) | last second: %ld moves written on reTHAWed's game "
            "thread, %ld copies skipped, %ld refused (level); %ld GTA samples, %ld snapped, a new sample was on average %.1f inches "
            "from where we had the car", buf, noRoomMax, static_cast<long>(InterlockedExchange(&g_applied, 0)),
            static_cast<long>(InterlockedExchange(&g_skippedCopies, 0)), static_cast<long>(InterlockedExchange(&g_refused, 0)),
            static_cast<long>(InterlockedExchange(&g_samples, 0)), static_cast<long>(InterlockedExchange(&g_snaps, 0)), meanErr);
        noRoomMax = 0;
    }
}

} // namespace mp::thaw
