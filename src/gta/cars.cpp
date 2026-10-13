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
            // 8.33 skitch: remember the nearest MOVING car (position + velocity, THAW units)
            // Only when it is close (the GTA side knows the metres, so the THAW side need not
            // guess a range in inches).
            if (cspeed > 0.1f && dCj < kSkitchNear * kSkitchNear && dCj < skBest) {
                skBest = dCj;
                skOk = true;
                skSpeed = cspeed;
                skModel = static_cast<int>(v->m_nModelIndex);
                skPos[0] = (p.x - align->offset.x) / align->scale;
                skPos[1] = (p.z - align->offset.z) / align->scale;
                skPos[2] = (p.y - align->offset.y) / (align->scale * align->ySign);
                // THAW velocity is units (inches) per second: m per frame * 50 / scale
                skVel[0] = vs.x * kGtaFps / align->scale;
                skVel[1] = vs.z * kGtaFps / align->scale;
                skVel[2] = vs.y * kGtaFps / (align->scale * align->ySign);
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
    // 8.33 skitch: the nearest moving car (values first, then the flag)
    if (skOk) {
        for (int k = 0; k < 3; ++k) {
            link->gtaSkitchPos[k] = skPos[k];
            link->gtaSkitchVel[k] = skVel[k];
        }
    }
    link->gtaSkitchValid = skOk ? 1 : 0;
    static DWORD lastSkLog = 0;
    const DWORD skNow = GetTickCount();
    if (skNow - lastSkLog > 1000) {
        lastSkLog = skNow;
        log("skitch: %s nearest moving car (model %d, %.1f m, speed %.2f) at thaw %.1f %.1f %.1f", skOk ? "publishing" : "no",
            skModel, skOk ? sqrtf(skBest) : -1.0f, skSpeed, skPos[0], skPos[1], skPos[2]);
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
