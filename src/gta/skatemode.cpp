// my-passthrough: skate mode for the GTA plugin (phase 2-2). See skatemode.h.
//
// What it changes in GTA, and only while skate mode is on:
//   - CJ's position and heading (SetPosn / SetHeading / m_fHeadingCurrent+Goal)
//   - CJ's speed (m_vecMoveSpeed set to zero each frame)
//   - one bit of CPad::DisablePlayerControls (bit 4, marked unused in
//     plugin-sdk), so CJ ignores the player's buttons. Missions use other bits;
//     we only ever set and clear our own.
//   - CJ's animation: a skating pose while skate mode is on (phase 5-0,
//     skateanim.cpp; GTA's own roller-skating animations)
//   - a stand-in skateboard drawn under CJ (phase 5-1, board.cpp); CJ is
//     lifted by the deck's height so he stands on it
// Nothing else: no saves, missions, world or traffic.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CCamera.h"
#include "CCutsceneMgr.h"
#include "CMenuManager.h"
#include "CPad.h"
#include "CPlayerInfo.h"
#include "CPlayerPed.h"
#include "CStreaming.h"
#include "CTimer.h"
#include "CWorld.h"
#include "common.h"
#include <windows.h>

#include "skatemode.h"
#include "skateanim.h"
#include "board.h"
#include "body.h"
#include "padlink.h"
#include "../common/convert.h"

#include <cmath>

namespace mp::gta {
namespace {

// ---- settings (change and rebuild) ----
constexpr int kToggleKey = VK_F7;
constexpr unsigned short kOurControlBit = 1u << 4; // plugin-sdk calls it "unk3, unused"
constexpr float kMinHeadingSpeed = 20.0f;          // THAW units/s; slower = keep facing
constexpr LONG kStaleMs = 1000;                     // no new skater sample for 1 s = link lost
constexpr LONG kMaxExtrapolateMs = 50;              // move ahead by velocity x age, at most 50 ms
// ySign -1 (changed 2026-10-07): with +1, a left turn in reTHAWed came out as a
// right turn in GTA (first skate test), so THAW needs the mirror.
const Mapping kMapping{0.0254f, -1.0f};             // scale (m per THAW unit), ySign
constexpr float kPi = 3.14159265f;
// Phase 5-3b: the board bone sits this far above the wheels' contact (m)
// (reTHAWed: 3.7 units above the skater's feet when riding normally).
constexpr float kBoardBoneHeight = 0.09f;
constexpr float kMinFacingFlat = 0.3f; // facing must point at least this much sideways (not up a wall)
// Phase 5-3c: CJ's whole body turns and tilts with the skater (ramps, vert,
// airs), around his feet. false = CJ stays upright as before.
constexpr bool kTiltCj = true;

struct State {
    bool on = false;
    // Phase 4-1: set while reTHAWed is in a level that is a copy of GTA's world
    // (Los Santos East): CJ goes straight to the matching GTA spot, no anchor.
    const LevelAlignment* align = nullptr;
    LONG levelId = 0;            // reTHAWed's level when skate mode went on
    Vec3 thawAnchor;
    Vec3 gtaAnchor;
    float feetOffset = 1.0f;     // CJ's position height above the ground at the anchor
    bool keyWasDown = false;
    bool comboWasDown = false;
    unsigned short lastControls = 0xFFFF; // for logging changes
    const char* lastHold = nullptr;
    // once-per-second report
    ULONGLONG nextReport = 0;
    unsigned frames = 0, moved = 0, held = 0, clamps = 0;
    unsigned facingFrames = 0, boardFrames = 0; // phase 5-3b: frames that used reTHAWed's facing / board
    unsigned tiltFrames = 0;                     // phase 5-3c: frames CJ was turned with the skater
    float maxTiltDeg = 0.0f;                     // phase 5-3c: most CJ leaned this second
    unsigned unreadableRun = 0, unreadableSkipped = 0; // frames skipped: sample kept changing
    float maxDrift = 0.0f;       // how far GTA moved CJ between our frames (m)
    bool haveLastTarget = false;
    Vec3 lastTarget;
    float lastHeading = 0.0f;
    bool headingSet = false;
    LONG lastAge = 0;
};
State g;

Vec3 ToVec(const CVector& v) { return Vec3{v.x, v.y, v.z}; }

float Distance(const Vec3& a, const Vec3& b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool GtaIsForeground()
{
    HWND w = GetForegroundWindow();
    if (!w) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

// A toggle press while GTA isn't the front window: say which window is
// (found 2026-10-07: F7 sometimes did nothing until an extra Alt+Tab).
void LogFrontWindow(LogFn log, const char* how)
{
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    char cls[96] = {}, title[96] = {};
    if (w) {
        GetWindowThreadProcessId(w, &pid);
        GetClassNameA(w, cls, sizeof(cls));
        GetWindowTextA(w, title, sizeof(title));
    }
    log("skate mode: %s pressed but GTA isn't the front window; ignored (front window: 0x%08lX, process %lu%s, class \"%s\", title \"%s\")",
        how, static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(w)), pid,
        pid == GetCurrentProcessId() ? " = GTA" : "", cls, title);
}

// Why CJ can't be in skate mode right now (nullptr = he can).
const char* GtaBlocker(CPlayerPed* ped, CPad* pad)
{
    if (!ped) return "no player";
    if (ped->bInVehicle) return "CJ is in a vehicle";
    if (ped->m_fHealth <= 0.0f) return "CJ is dead";
    if (CWorld::Players[0].m_nPlayerState != PLAYERSTATE_PLAYING)
        return "player not in normal play (died / arrested / mission ended)";
    if (CCutsceneMgr::ms_running) return "cutscene running";
    if (TheCamera.m_bWideScreenOn) return "scripted scene (black bars)";
    if (pad && (pad->DisablePlayerControls & ~kOurControlBit)) return "a script disabled player controls";
    return nullptr;
}

// Reason text for "the sample kept changing while we read it" (the THAW side
// writes ~95 times a second; rarely 8 tries in a row all land mid-write).
// Found 2026-10-08: this once turned skate mode off. Now it only skips the frame.
const char* const kUnreadable = "skater sample unreadable";
constexpr unsigned kMaxUnreadableFrames = 60; // ~1 s in a row = really broken

// Latest skater sample; false + reason if skate mode can't use it.
bool ReadSample(LinkBlock* link, SkaterShared& s, LONG& ageMs, const char*& why)
{
    if (!link || link->thawPid == 0) { why = "reTHAWed not connected"; return false; }
    int retries = 0;
    if (!ReadSkaterShared(link, s, retries)) { why = kUnreadable; return false; }
    ageMs = static_cast<LONG>(GetTickCount64() & 0xFFFFFFFFu) - s.sampleTick;
    if (ageMs > kStaleMs) { why = "skater samples stopped (link lost)"; return false; }
    if (!(s.flags & kSkaterValid)) { why = "no skater in reTHAWed"; return false; }
    if (!(s.flags & kStateValid) || !(s.flags & kInLevel)) { why = "reTHAWed not in a level"; return false; }
    return true;
}

// GTA ground height below (x, y), searching down from a little above z.
// Returns false if no ground was found.
bool GroundZ(float x, float y, float z, float& groundZ)
{
    bool found = false;
    groundZ = CWorld::FindGroundZFor3DCoord(x, y, z + 2.0f, &found, nullptr);
    return found;
}

void ResetStats()
{
    g.frames = g.moved = g.held = g.clamps = 0;
    g.facingFrames = g.boardFrames = g.tiltFrames = 0;
    g.maxTiltDeg = 0.0f;
    g.maxDrift = 0.0f;
}

void TurnOff(CPlayerPed* ped, CPad* pad, LinkBlock* link, LogFn log, const char* reason)
{
    g.on = false;
    g.haveLastTarget = false;
    g.lastHold = nullptr;
    if (pad) pad->DisablePlayerControls &= static_cast<unsigned short>(~kOurControlBit);
    if (link) InterlockedExchange(&link->gtaSkateMode, 0);
    SkateAnimStop(ped, log);
    BoardHide(log);
    BodyClear();
    if (ped) ped->SetHeading(g.lastHeading); // phase 5-3c: stand CJ upright again
    if (ped) {
        const CVector& p = ped->GetPosition();
        log("skate mode: OFF (%s) | CJ left at %.2f %.2f %.2f | control flags now 0x%04X", reason, p.x,
            p.y, p.z, pad ? pad->DisablePlayerControls : 0);
    } else {
        log("skate mode: OFF (%s)", reason);
    }
}

void TurnOn(CPlayerPed* ped, CPad* pad, LinkBlock* link, LogFn log, const char* how)
{
    if (const char* b = GtaBlocker(ped, pad)) {
        log("skate mode: can't turn on (%s): %s", how, b);
        return;
    }
    if (FrontEndMenuManager.m_bMenuActive || CTimer::m_UserPause || CTimer::m_CodePause) {
        log("skate mode: can't turn on (%s): GTA is paused or its menu is open", how);
        return;
    }
    SkaterShared s;
    LONG age = 0;
    const char* why = nullptr;
    if (!ReadSample(link, s, age, why)) {
        log("skate mode: can't turn on (%s): %s", how, why);
        return;
    }

    g.thawAnchor = Vec3{s.pos[0], s.pos[1], s.pos[2]};
    g.gtaAnchor = ToVec(ped->GetPosition());
    float gz = 0.0f;
    if (GroundZ(g.gtaAnchor.x, g.gtaAnchor.y, g.gtaAnchor.z, gz)) {
        g.feetOffset = g.gtaAnchor.z - gz;
        if (g.feetOffset < 0.3f || g.feetOffset > 2.0f) g.feetOffset = 1.0f; // implausible: use 1 m
    } else {
        g.feetOffset = 1.0f;
    }
    g.levelId = s.levelId;
    g.align = FindLevelAlignment(static_cast<unsigned long>(s.levelId));
    if (g.align) {
        // Phase 4-1: CJ goes to the GTA spot matching the skater (loading that area first).
        Vec3 t = MapAbsolute(g.thawAnchor, *g.align);
        t.z += g.feetOffset;
        CVector to(t.x, t.y, t.z);
        const Vec3 from = g.gtaAnchor;
        CStreaming::LoadScene(&to);
        ped->Teleport(to, false);
        log("skate mode: %s level: CJ moved %.1f m to the skater's spot (%.2f %.2f %.2f -> %.2f %.2f %.2f)",
            g.align->name, Distance(from, t), from.x, from.y, from.z, t.x, t.y, t.z);
    }
    pad->DisablePlayerControls |= kOurControlBit;
    InterlockedExchange(&link->gtaSkateMode, 1);
    g.on = true;
    g.unreadableRun = 0;
    g.haveLastTarget = false;
    g.lastHold = nullptr;
    ResetStats();
    g.nextReport = GetTickCount64() + 1000;
    log("skate mode: ON (%s) | %s | CJ anchor %.2f %.2f %.2f | skater anchor %.2f %.2f %.2f | "
        "CJ height above ground %.2f m | scale %.4f m/unit, y sign %+.0f | level 0x%08lX | control flags 0x%04X",
        how, g.align ? "ABSOLUTE (level lined up with GTA)" : "relative (from where CJ stands)",
        g.gtaAnchor.x, g.gtaAnchor.y, g.gtaAnchor.z, g.thawAnchor.x, g.thawAnchor.y, g.thawAnchor.z,
        g.feetOffset, g.align ? g.align->scale : kMapping.scale, kMapping.ySign,
        static_cast<unsigned long>(s.levelId), pad->DisablePlayerControls);
}

void ReportIfDue(CPlayerPed* ped, LogFn log)
{
    const ULONGLONG now = GetTickCount64();
    if (now < g.nextReport) return;
    g.nextReport = now + 1000;
    const CVector& p = ped->GetPosition();
    // Check of GTA's heading convention: what CJ's own matrix says vs what we set.
    const CVector& f = ped->GetForward();
    const float fromMatrix = GtaHeadingFor(f.x, f.y) * 180.0f / kPi;
    log("skate mode: %u frames (%u moved, %u held) | CJ at %.2f %.2f %.2f, target %.2f %.2f %.2f | "
        "GTA moved him up to %.3f m between frames | ground clamps %u | sample %ld ms old | "
        "heading set %s%.1f deg, CJ faces %.1f deg | reTHAWed facing used %u, board %u, CJ turned %u (lean up to %.0f deg)",
        g.frames, g.moved, g.held, p.x, p.y, p.z, g.lastTarget.x, g.lastTarget.y, g.lastTarget.z,
        g.maxDrift, g.clamps, g.lastAge, g.headingSet ? "" : "(not yet) ", g.lastHeading * 180.0f / kPi,
        fromMatrix, g.facingFrames, g.boardFrames, g.tiltFrames, g.maxTiltDeg);
    // Phase 5-3d: how often CJ's body was copied from the skater.
    unsigned bodySkipped = 0;
    const unsigned bodyFrames = BodyFramesAndReset(&bodySkipped);
    if (const char* info = BodyTakeInfo()) log("skate body: %s", info);
    log("skate body: copied from the skater in %u frames, %u half-updated samples skipped", bodyFrames, bodySkipped);
    ResetStats();
}

// Phase 5-3c: turn CJ's whole body like the skater: his matrix gets the
// skater's up and facing (mapped to GTA), and he is moved so his feet stay at
// the skater's feet. `t` is the upright position worked out above. Runs after
// SetHeading (which would make him upright again); GTA rebuilds his upright
// matrix next frame, and we turn him again.
void TiltCj(CPlayerPed* ped, const SkaterShared& s, const Vec3& t)
{
    if (!ped->m_matrix) return;
    const Vec3 a = ThawDeltaToGta(Vec3{s.orient[6], s.orient[7], s.orient[8]}, kMapping); // facing
    const Vec3 u = ThawDeltaToGta(Vec3{s.orient[3], s.orient[4], s.orient[5]}, kMapping); // up
    const float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    if (ul < 1e-6f) return;
    const CVector up(u.x / ul, u.y / ul, u.z / ul);
    // forward: the facing with any up part removed, so the matrix stays square
    const float d = a.x * up.x + a.y * up.y + a.z * up.z;
    CVector fwd(a.x - up.x * d, a.y - up.y * d, a.z - up.z * d);
    const float fl = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (fl < 1e-6f) return;
    fwd = CVector(fwd.x / fl, fwd.y / fl, fwd.z / fl);
    const CVector right(fwd.y * up.z - fwd.z * up.y, fwd.z * up.x - fwd.x * up.z, fwd.x * up.y - fwd.y * up.x);
    const float h = g.feetOffset + kBoardLift; // feet to CJ's position
    CMatrixLink& m = *ped->m_matrix;
    m.right = right;
    m.up = fwd;  // GTA's "up" row is the forward direction
    m.at = up;   // GTA's "at" row is the up direction
    m.pos = CVector(t.x, t.y, t.z - h + up.z * h);
    m.pos.x += up.x * h;
    m.pos.y += up.y * h;
    const float lean = std::acos(up.z > 1.0f ? 1.0f : (up.z < -1.0f ? -1.0f : up.z)) * 180.0f / kPi;
    if (lean > g.maxTiltDeg) g.maxTiltDeg = lean;
}

// Phase 5-3b: put the board where reTHAWed's board bone is. The bone is given
// in the skater's own space; the skater's orientation turns it into THAW world
// directions, which map to GTA like any THAW movement. `t` is CJ's position
// this frame; his feet are the skater's origin.
void PlaceBoardFromThaw(const SkaterShared& s, const Vec3& t, float scale, LogFn log)
{
    const float* o = s.orient; // rows right, up, at
    const float* b = s.board;  // rows right, up (to the tail), at (deck normal), position
    auto toWorld = [o](const float* v) {
        return Vec3{v[0] * o[0] + v[1] * o[3] + v[2] * o[6], v[0] * o[1] + v[1] * o[4] + v[2] * o[7],
                    v[0] * o[2] + v[1] * o[5] + v[2] * o[8]};
    };
    const Mapping m{scale, kMapping.ySign};
    auto dir = [&](const float* v, float sign) {
        Vec3 d = ThawDeltaToGta(toWorld(v), m);
        const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len > 1e-6f) { d.x *= sign / len; d.y *= sign / len; d.z *= sign / len; }
        return d;
    };
    const Vec3 right = dir(b + 0, 1.0f);
    const Vec3 fwd = dir(b + 3, -1.0f); // bone "up" points to the tail
    const Vec3 up = dir(b + 6, 1.0f);
    const Vec3 off = ThawDeltaToGta(toWorld(b + 9), m);
    const float feetZ = t.z - g.feetOffset - kBoardLift;
    const float origin[3] = {t.x + off.x - up.x * kBoardBoneHeight, t.y + off.y - up.y * kBoardBoneHeight,
                             feetZ + off.z - up.z * kBoardBoneHeight};
    const float r[3] = {right.x, right.y, right.z}, f[3] = {fwd.x, fwd.y, fwd.z}, u[3] = {up.x, up.y, up.z};
    BoardUpdatePose(origin, r, f, u, log);
}

void SkateModeFrame(LinkBlock* link, LogFn log)
{
    CPlayerPed* ped = FindPlayerPed();
    CPad* pad = CPad::GetPad(0);
    if (!pad) return;

    // Log any change in the control flags (ours is bit 4 = 0x0010).
    if (pad->DisablePlayerControls != g.lastControls) {
        log("control flags: 0x%04X -> 0x%04X", g.lastControls == 0xFFFF ? 0 : g.lastControls,
            pad->DisablePlayerControls);
        g.lastControls = pad->DisablePlayerControls;
    }

    // Toggle: F7 or L3 + R3, counted once per press, only with GTA in front.
    const bool foreground = GtaIsForeground();
    const bool keyDown = (GetAsyncKeyState(kToggleKey) & 0x8000) != 0;
    // Phase 3-4: during skate mode GTA's pad is blanked, so read L3 + R3 from the
    // controller frame copied just before blanking (padlink.cpp) when we have it.
    LONG raw = 0;
    const bool comboDown = PadLinkRawButtons(raw)
        ? ((raw & kBtnL3) != 0 && (raw & kBtnR3) != 0)
        : (pad->NewState.ShockButtonL != 0 && pad->NewState.ShockButtonR != 0);
    const char* how = nullptr;
    if (foreground && keyDown && !g.keyWasDown) how = "F7";
    if (foreground && comboDown && !g.comboWasDown) how = "L3 + R3";
    if (!foreground && ((keyDown && !g.keyWasDown) || (comboDown && !g.comboWasDown)))
        LogFrontWindow(log, keyDown && !g.keyWasDown ? "F7" : "L3 + R3");
    g.keyWasDown = keyDown;
    g.comboWasDown = comboDown;
    if (how) {
        if (g.on) {
            TurnOff(ped, pad, link, log, how);
            return;
        }
        TurnOn(ped, pad, link, log, how);
    }
    if (!g.on) {
        // GTA's pause menu saves the control flags when it opens and puts them
        // back when it closes (seen in the first skate test). If our bit comes
        // back while skate mode is off, clear it so CJ isn't stuck.
        if (pad->DisablePlayerControls & kOurControlBit) {
            pad->DisablePlayerControls &= static_cast<unsigned short>(~kOurControlBit);
            log("skate mode: cleared our leftover control flag (now 0x%04X)", pad->DisablePlayerControls);
        }
        return;
    }

    // GTA's pause menu or map open: hold (checked before the automatic-off list,
    // because the menu sets its own control flag, which looks like a script).
    // Happens when Start/pause in reTHAWed also reaches GTA through the controller.
    if (FrontEndMenuManager.m_bMenuActive || CTimer::m_UserPause || CTimer::m_CodePause) {
        const char* hold = FrontEndMenuManager.m_bMenuActive ? "GTA menu open" : "GTA paused";
        if (hold != g.lastHold) {
            log("skate mode: holding (%s)", hold);
            g.lastHold = hold;
        }
        ++g.frames;
        ++g.held;
        g.haveLastTarget = false;
        ReportIfDue(ped, log);
        return;
    }

    // Automatic off.
    if (const char* b = GtaBlocker(ped, pad)) {
        TurnOff(ped, pad, link, log, b);
        return;
    }
    SkaterShared s;
    LONG age = 0;
    const char* why = nullptr;
    if (!ReadSample(link, s, age, why)) {
        if (why == kUnreadable && ++g.unreadableRun <= kMaxUnreadableFrames) {
            ++g.unreadableSkipped; // try again next frame; CJ stays put this frame
            if (g.unreadableSkipped == 1 || g.unreadableSkipped % 100 == 0)
                log("skate mode: skipped %u frame(s) so far: skater sample kept changing while read", g.unreadableSkipped);
            return;
        }
        TurnOff(ped, pad, link, log, why);
        return;
    }
    g.unreadableRun = 0;
    if (s.levelId != g.levelId) {
        TurnOff(ped, pad, link, log, "reTHAWed changed level");
        return;
    }
    pad->DisablePlayerControls |= kOurControlBit; // keep ours set
    ++g.frames;
    g.lastAge = age;

    // Hold (stay on, don't move CJ) while either game is paused.
    const char* hold = nullptr;
    if (s.flags & kPaused) hold = "reTHAWed paused";
    if (hold != g.lastHold) {
        if (hold) log("skate mode: holding (%s)", hold);
        else log("skate mode: moving again");
        g.lastHold = hold;
    }
    if (hold) {
        ++g.held;
        g.haveLastTarget = false; // don't count the jump after a pause as drift
        ReportIfDue(ped, log);
        return;
    }

    // How far did GTA's own physics move CJ since our last placement?
    if (g.haveLastTarget) {
        const float drift = Distance(ToVec(ped->GetPosition()), g.lastTarget);
        if (drift > g.maxDrift) g.maxDrift = drift;
    }

    // Target: skater position, nudged forward by velocity x sample age (max 50 ms).
    const LONG ex = age < 0 ? 0 : (age > kMaxExtrapolateMs ? kMaxExtrapolateMs : age);
    const Vec3 p{s.pos[0] + s.vel[0] * ex / 1000.0f, s.pos[1] + s.vel[1] * ex / 1000.0f,
                 s.pos[2] + s.vel[2] * ex / 1000.0f};
    Vec3 t;
    if (g.align) {
        t = MapAbsolute(p, *g.align); // phase 4-1: the matching GTA spot
        t.z += g.feetOffset;
    } else {
        t = MapPosition(p, g.thawAnchor, g.gtaAnchor, kMapping);
    }
    t.z += kBoardLift; // phase 5-1: CJ stands on the board's deck

    // Never below GTA's ground (THAW's slopes don't exist in GTA).
    float gz = 0.0f;
    const bool haveGround = GroundZ(t.x, t.y, t.z, gz);
    if (haveGround && t.z < gz + g.feetOffset + kBoardLift) {
        t.z = gz + g.feetOffset + kBoardLift;
        ++g.clamps;
    }

    ped->SetPosn(CVector(t.x, t.y, t.z));
    ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    float heading = 0.0f;
    // Phase 5-3b: face where reTHAWed's skater faces (fixes the turn-around at
    // rail ends); from the speed only if the facing isn't there or points up a wall.
    bool faced = false;
    if (s.flags & kPoseValid) {
        const Vec3 at = ThawDeltaToGta(Vec3{s.orient[6], s.orient[7], s.orient[8]}, kMapping);
        const float flat = std::sqrt(at.x * at.x + at.y * at.y);
        if (flat > kMinFacingFlat * kMapping.scale) {
            heading = GtaHeadingFor(at.x, at.y);
            faced = true;
            ++g.facingFrames;
        }
    }
    if (faced || GtaHeadingFromThawVelocity(Vec3{s.vel[0], s.vel[1], s.vel[2]}, kMapping, kMinHeadingSpeed, heading)) {
        ped->m_fHeadingCurrent = heading;
        ped->m_fHeadingGoal = heading;
        ped->SetHeading(heading);
        g.lastHeading = heading;
        g.headingSet = true;
    }
    if (kTiltCj && (s.flags & kPoseValid)) {
        TiltCj(ped, s, t);
        ++g.tiltFrames;
    }
    // Phase 5-0: skating pose from speed along the ground and height above it.
    const float scale = g.align ? g.align->scale : kMapping.scale;
    const float speed = std::sqrt(s.vel[0] * s.vel[0] + s.vel[2] * s.vel[2]) * scale; // THAW: y is up
    const float height = haveGround ? t.z - g.feetOffset - kBoardLift - gz : 0.0f;
    // Phase 5-3d: while CJ's body copies the skater's, keep GTA's animation
    // underneath steady (standing pose): switching poses made the shoulders
    // shimmy and the body jump at take-off (first test).
    if ((s.flags & kBodyValid) && (s.flags & kPoseValid)) SkateAnimUpdate(ped, 0.0f, 0.0f, log);
    else SkateAnimUpdate(ped, speed, height, log);
    // Phase 5-1: the board, wheels at CJ's feet minus the deck height.
    // Phase 5-3b: if reTHAWed's board bone came along, place it exactly as that.
    if (s.flags & kPoseValid) {
        PlaceBoardFromThaw(s, t, scale, log);
        ++g.boardFrames;
    } else {
        BoardUpdate(ped, t.x, t.y, t.z - g.feetOffset - kBoardLift, haveGround && height < 0.3f, log);
    }
    // Phase 5-3d: CJ's body copies the skater's (applied when GTA draws him).
    if ((s.flags & kBodyValid) && (s.flags & kPoseValid))
        BodySetTarget(s.body, s.orient, scale, kMapping.ySign, t.x, t.y, t.z - g.feetOffset - kBoardLift);
    else
        BodyClear();
    g.lastTarget = ToVec(ped->GetPosition()); // where CJ really is (turned or not)
    g.haveLastTarget = true;
    ++g.moved;
    ReportIfDue(ped, log);
}

} // namespace

bool SkateModeIsOn() { return g.on; }

void SkateModeFrameSafe(LinkBlock* link, LogFn log)
{
    __try {
        SkateModeFrame(link, log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Something went wrong in our code: give GTA its controls back and stop.
        const DWORD code = GetExceptionCode();
        g.on = false;
        __try {
            CPad* pad = CPad::GetPad(0);
            if (pad) pad->DisablePlayerControls &= static_cast<unsigned short>(~kOurControlBit);
            if (link) InterlockedExchange(&link->gtaSkateMode, 0);
            SkateAnimStop(FindPlayerPed(), log);
            BoardHide(log);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        log("skate mode: ERROR (exception 0x%08lX) - turned off, controls restored", code);
    }
}

} // namespace mp::gta
