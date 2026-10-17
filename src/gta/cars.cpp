// SkateAndreas: 8.33 C1, research (log only). See cars.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CPools.h"
#include "CVehicle.h"
#include "CPlayerPed.h"
#include "CGame.h"
#include "CModelInfo.h"
#include "CColModel.h"
#include "CBox.h"
#include <windows.h>

#include "cars.h"
#include "skatemode.h"
#include "../common/link.h"
#include "../common/convert.h"

namespace mp::gta {
namespace {

constexpr float kNear = 40.0f;   // m: cars within this of CJ are logged
constexpr float kRailNear = 22.0f; // m: cars within this give roof rails
constexpr float kSkitchNear = 8.0f; // m: a moving car within this can be skitched
constexpr float kSkitchHold = 16.0f; // m: a car the skater holds is followed out to this (8.42)
constexpr float kPoolNear = 20.0f;   // m: 8.52 step 1: cars within this go into the pool list (test 36: moving or not)
constexpr int kPoolCand = 32;        // candidates looked at per pass
constexpr float kGtaFps = 50.0f;   // GTA's move speed is metres per 50 Hz frame

// 8.41: a car gets a roof rail only when it is genuinely PARKED. The old test was
// `move speed <= 0.05`, and that is not the same thing: 0.05 m per frame is 2.5 m/s
// (9 km/h), so a car pulling away from a standstill spent a second or so inside that
// window with its rail rewritten every frame - a rail that MOVES under the skater,
// which blows reTHAWed's grind maths to NaN (the yellow screen the user hit on
// 2026-10-13 while "grinding a few moving cars"). So the test is now the car's own
// POSITION: it must be where it was on the last pass. A car that is moving never
// qualifies, whatever its speed field says.
constexpr float kStillMoved = 0.02f; // m: more than this since the last pass = not parked
constexpr int kLastPosMax = 512;     // vehicle pool indices we remember (GTA's pool is ~110)
CVector g_lastPos[kLastPosMax];
bool g_lastValid[kLastPosMax] = {};

// A sane THAW coordinate (rejects NaN/inf and absurd values from a bad box).
inline bool OkThaw(float f) { return f > -5.0e6f && f < 5.0e6f; }

} // namespace

// 8.51 step 6: metres from a car's centre back to its rear bumper (its collision box, -min.y),
// 2.2 m when the model has no box or a silly one.
float RearOf(CVehicle* v)
{
    CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs ? CModelInfo::ms_modelInfoPtrs[v->m_nModelIndex] : nullptr;
    CColModel* col = mi ? mi->m_pColModel : nullptr;
    const float back = col ? -col->m_boundBox.m_vecMin.y : 2.2f;
    return (back > 0.3f && back < 12.0f) ? back : 2.2f;
}

void CarTick(LinkBlock* link, void (*log)(const char*, ...))
{
    if (!link || link->gtaSkateMode != 1) return;
    CPlayerPed* ped = FindPlayerPed(0);
    if (!ped) return;
    const LevelAlignment* align = FindLevelAlignment(static_cast<unsigned long>(link->thawLevelId));
    if (!align) return;
    const CVector c = ped->GetPosition();
    static float pts[kCarRailCars * kCarRailPts * 3];
    int count = 0;
    float skPos[3] = {}, skVel[3] = {}; // 8.33 skitch: nearest moving car
    bool skOk = false;
    float skBest = kRailNear * kRailNear;
    float skSpeed = 0.0f;
    int skModel = 0;
    // 8.42: the car the skater holds (thawSkitchId, -1 = none), and the nearest moving car
    const LONG heldId = link->thawSkitchId;
    int skIdx = -1;
    float skFwd[3] = {};
    bool lockOk = false;
    float lockD2 = 0.0f, lockSpeed = 0.0f;
    int lockModel = 0;
    float lockPos[3] = {}, lockVel[3] = {}, lockFwd[3] = {};
    float skBack = 0.0f, lockBack = 0.0f; // 8.51 step 6: rear bumper behind the centre, THAW inches
    // 8.52 step 1: the pool list (cars within kPoolNear, moving or not since test 36; the held car always)
    struct Cand { PoolCar car; float d2; float dist; float speed; };
    Cand cand[kPoolCand];
    int candN = 0;
    __try {
        auto* pool = CPools::ms_pVehiclePool;
        if (!pool) return;
        const float railNear2 = kRailNear * kRailNear;
        for (int i = 0; i < pool->m_nSize; ++i) {
            CVehicle* v = pool->GetAt(i);
            if (!v) continue;
            const CVector p = v->GetPosition();
            const float ddx = p.x - c.x, ddy = p.y - c.y, ddz = p.z - c.z;
            const float dCj = ddx * ddx + ddy * ddy + ddz * ddz;
            if (dCj > railNear2) continue;
            const CVector vs = v->m_vecMoveSpeed;
            const float cspeed = vs.Magnitude();
            const bool isHeld = heldId >= 0 && i == heldId && dCj < kSkitchHold * kSkitchHold;
            if (candN < kPoolCand && (isHeld || dCj < kPoolNear * kPoolNear)) {
                Cand& k = cand[candN++];
                const CVector nose = v->GetForward();
                k.car.id = i;
                k.car.pos[0] = (p.x - align->offset.x) / align->scale;
                k.car.pos[1] = (p.z - align->offset.z) / align->scale;
                k.car.pos[2] = (p.y - align->offset.y) / (align->scale * align->ySign);
                k.car.vel[0] = vs.x * kGtaFps / align->scale;
                k.car.vel[1] = vs.z * kGtaFps / align->scale;
                k.car.vel[2] = vs.y * kGtaFps / (align->scale * align->ySign);
                k.car.fwd[0] = nose.x;
                k.car.fwd[1] = nose.z;
                k.car.fwd[2] = nose.y / align->ySign;
                k.car.back = RearOf(v) / align->scale;
                k.d2 = isHeld ? -1.0f : dCj; // the held car sorts first, so it is never left out
                k.dist = sqrtf(dCj);
                k.speed = cspeed;
            }
            // 8.42: the car the skater holds is followed by its pool index, out to kSkitchHold,
            // even when it is slow (the THAW side lets go when it stops). It is never swapped
            // for the nearest car while he holds it.
            if (heldId >= 0 && i == heldId && dCj < kSkitchHold * kSkitchHold) {
                const CVector nose = v->GetForward();
                lockOk = true;
                lockD2 = dCj;
                lockSpeed = cspeed;
                lockModel = static_cast<int>(v->m_nModelIndex);
                lockPos[0] = (p.x - align->offset.x) / align->scale;
                lockPos[1] = (p.z - align->offset.z) / align->scale;
                lockPos[2] = (p.y - align->offset.y) / (align->scale * align->ySign);
                lockVel[0] = vs.x * kGtaFps / align->scale;
                lockVel[1] = vs.z * kGtaFps / align->scale;
                lockVel[2] = vs.y * kGtaFps / (align->scale * align->ySign);
                lockFwd[0] = nose.x;
                lockFwd[1] = nose.z;
                lockFwd[2] = nose.y / align->ySign;
                lockBack = RearOf(v) / align->scale;
            }
            // 8.33 skitch: remember the nearest MOVING car (position + velocity, THAW units)
            // Only when it is close (the GTA side knows the metres, so the THAW side need not
            // guess a range in inches).
            if (cspeed > 0.1f && dCj < kSkitchNear * kSkitchNear && dCj < skBest) {
                skBest = dCj;
                skOk = true;
                skIdx = i;
                skSpeed = cspeed;
                skModel = static_cast<int>(v->m_nModelIndex);
                skPos[0] = (p.x - align->offset.x) / align->scale;
                skPos[1] = (p.z - align->offset.z) / align->scale;
                skPos[2] = (p.y - align->offset.y) / (align->scale * align->ySign);
                // THAW velocity is units (inches) per second: m per frame * 50 / scale
                skVel[0] = vs.x * kGtaFps / align->scale;
                skVel[1] = vs.z * kGtaFps / align->scale;
                skVel[2] = vs.y * kGtaFps / (align->scale * align->ySign);
                const CVector nose = v->GetForward();
                skFwd[0] = nose.x;
                skFwd[1] = nose.z;
                skFwd[2] = nose.y / align->ySign;
                skBack = RearOf(v) / align->scale;
            }
            // 8.41: rails on PARKED cars only, decided by the car not having moved since the
            // last pass - never by its speed (see the note above; the speed test let a slowly
            // moving rail through, which is what turns reTHAWed's grind maths to NaN).
            bool still = false;
            if (i < kLastPosMax) {
                if (g_lastValid[i] && (p - g_lastPos[i]).Magnitude() <= kStillMoved) still = true;
                g_lastPos[i] = p;
                g_lastValid[i] = true;
            }
            if (!still || count >= kCarRailCars) continue;
            const CVector right = v->GetRight(), fwd = v->GetForward();
            // roof box: from the model's bounding box, with sane fallbacks
            float hw = 0.9f, hl = 2.0f, roof = 1.35f;
            CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs ? CModelInfo::ms_modelInfoPtrs[v->m_nModelIndex] : nullptr;
            CColModel* col = mi ? mi->m_pColModel : nullptr;
            if (col) {
                hw = (col->m_boundBox.m_vecMax.x - col->m_boundBox.m_vecMin.x) * 0.5f * 0.7f;
                hl = (col->m_boundBox.m_vecMax.y - col->m_boundBox.m_vecMin.y) * 0.5f * 0.7f;
                roof = col->m_boundBox.m_vecMax.z * 0.55f;
            }
            if (hw < 0.35f) hw = 0.35f;
            if (hl < 0.7f) hl = 0.7f;
            if (roof < 0.8f) roof = 1.35f;
            const CVector mid(p.x, p.y, p.z + roof);
            float car[2 * 2 * 3];
            bool ok = true;
            for (int r = 0; r < 2 && ok; ++r) {
                const float s = (r == 0) ? hw : -hw;
                const CVector a = mid + right * s + fwd * hl;
                const CVector b = mid + right * s - fwd * hl;
                float* o = car + r * 2 * 3;
                // GTA world -> THAW (inverse of MapAbsolute in convert.h)
                o[0] = (a.x - align->offset.x) / align->scale;
                o[1] = (a.z - align->offset.z) / align->scale;
                o[2] = (a.y - align->offset.y) / (align->scale * align->ySign);
                o[0 + 3] = (b.x - align->offset.x) / align->scale;
                o[1 + 3] = (b.z - align->offset.z) / align->scale;
                o[2 + 3] = (b.y - align->offset.y) / (align->scale * align->ySign);
                for (int k = 0; k < 6; ++k)
                    if (!OkThaw(o[k])) ok = false;
            }
            if (!ok) {
                static DWORD lastLog = 0;
                const DWORD now = GetTickCount();
                if (now - lastLog > 1000) {
                    lastLog = now;
                    log("cars: rejected model %d (box %.1f %.1f %.1f .. %.1f %.1f %.1f) - not making a rail",
                        static_cast<int>(v->m_nModelIndex), col ? col->m_boundBox.m_vecMin.x : 0.0f,
                        col ? col->m_boundBox.m_vecMin.y : 0.0f, col ? col->m_boundBox.m_vecMin.z : 0.0f,
                        col ? col->m_boundBox.m_vecMax.x : 0.0f, col ? col->m_boundBox.m_vecMax.y : 0.0f,
                        col ? col->m_boundBox.m_vecMax.z : 0.0f);
                }
                continue; // a bad box / matrix: never write it
            }
            for (int k = 0; k < 12; ++k) pts[count * kCarRailPts * 3 + k] = car[k];
            ++count;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    PublishCarRails(link, count, pts);
    // 8.52 step 1: the nearest kPoolCars of the list (the held car first), for the proxy pool
    {
        for (int a = 1; a < candN; ++a) { // insertion sort: at most kPoolCand entries
            const Cand x = cand[a];
            int b = a - 1;
            while (b >= 0 && cand[b].d2 > x.d2) {
                cand[b + 1] = cand[b];
                --b;
            }
            cand[b + 1] = x;
        }
        const int n = candN < kPoolCars ? candN : kPoolCars;
        PoolCar pub[kPoolCars];
        bool ok = true;
        for (int a = 0; a < n; ++a) {
            pub[a] = cand[a].car;
            for (int k = 0; k < 3; ++k)
                if (!OkThaw(pub[a].pos[k])) ok = false;
        }
        PublishPoolCars(link, ok ? n : 0, pub);
        static DWORD lastPoolLog = 0;
        const DWORD pNow = GetTickCount();
        if (pNow - lastPoolLog > 1000 && (n > 0 || heldId >= 0)) {
            lastPoolLog = pNow;
            char buf[400];
            int len = snprintf(buf, sizeof(buf), "skitch pool: %d car(s) published of %d within %.0f m%s:", ok ? n : 0, candN,
                               kPoolNear, ok ? "" : " (NOT published: a bad position)");
            for (int a = 0; a < n && len > 0 && len < static_cast<int>(sizeof(buf)); ++a)
                len += snprintf(buf + len, sizeof(buf) - len, " car %ld (%s%.1f m, speed %.2f)", static_cast<long>(cand[a].car.id),
                                cand[a].d2 < 0.0f ? "HELD, " : "", cand[a].dist, cand[a].speed);
            log("%s", buf);
        }
    }
    // 8.33 / 8.42 skitch: the car the skater holds, if he holds one and it is still in
    // reach; otherwise the nearest moving car, as the candidate for a grab. Values first,
    // then the id and the flag.
    const bool pubLock = lockOk;
    const bool pubCand = !lockOk && skOk;
    const float* pp = pubLock ? lockPos : skPos;
    const float* pv = pubLock ? lockVel : skVel;
    const float* pf = pubLock ? lockFwd : skFwd;
    if (pubLock || pubCand) {
        for (int k = 0; k < 3; ++k) {
            link->gtaSkitchPos[k] = pp[k];
            link->gtaSkitchVel[k] = pv[k];
            link->gtaSkitchFwd[k] = pf[k];
        }
        link->gtaSkitchBack = pubLock ? lockBack : skBack;
    }
    link->gtaSkitchId = pubLock ? heldId : (pubCand ? skIdx : -1);
    link->gtaSkitchValid = (pubLock || pubCand) ? 1 : 0;
    static DWORD lastSkLog = 0;
    const DWORD skNow = GetTickCount();
    if (skNow - lastSkLog > 1000) {
        lastSkLog = skNow;
        if (pubLock) {
            log("skitch: holding car %ld (model %d, %.1f m, speed %.2f, rear %.0f in behind its centre) at thaw %.1f %.1f %.1f", heldId,
                lockModel, sqrtf(lockD2), lockSpeed, lockBack, pp[0], pp[1], pp[2]);
        } else {
            log("skitch: %s car %d (model %d, %.1f m, speed %.2f) at thaw %.1f %.1f %.1f", pubCand ? "publishing" : "no",
                skIdx, skModel, pubCand ? sqrtf(skBest) : -1.0f, skSpeed, pp[0], pp[1], pp[2]);
        }
    }
}

void CarScanSecond(void (*log)(const char*, ...))
{
    if (!SkateModeIsOn() && !BmxModeIsOn()) return;
    CPlayerPed* ped = FindPlayerPed(0);
    if (!ped) return;
    const CVector c = ped->GetPosition();
    int n = 0;
    __try {
        auto* pool = CPools::ms_pVehiclePool;
        if (!pool) return;
        for (int i = 0; i < pool->m_nSize; ++i) {
            CVehicle* v = pool->GetAt(i);
            if (!v) continue;
            const CVector p = v->GetPosition();
            const float d = (p - c).Magnitude();
            if (d > kNear) continue;
            CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs ? CModelInfo::ms_modelInfoPtrs[v->m_nModelIndex] : nullptr;
            CColModel* col = mi ? mi->m_pColModel : nullptr;
            const float boxTop = col ? col->m_boundBox.m_vecMax.z : -1.0f;
            // 8.41: say whether a rail would actually be published - the car must be parked
            // (its position unchanged since the last pass), or there is no rail at all.
            const bool parked = (i < kLastPosMax) && g_lastValid[i] && (p - g_lastPos[i]).Magnitude() <= kStillMoved;
            log("car %d: model %d pos %.1f %.1f %.1f | %.0f m from CJ | speed %.1f | driver %s | box top %.2f -> %s +%.2f",
                i, static_cast<int>(v->m_nModelIndex), p.x, p.y, p.z, d, v->m_vecMoveSpeed.Magnitude(),
                v->m_pDriver ? "yes" : "no", boxTop, parked ? "rail" : "NO RAIL (moving)",
                boxTop > 0.0f ? boxTop * 0.55f : 1.35f);
            ++n;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    log("cars: %d within %.0f m", n, kNear);
}

} // namespace mp::gta
