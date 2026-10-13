// my-passthrough: 8.33 C1, research (log only). See cars.h.

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
    __try {
        auto* pool = CPools::ms_pVehiclePool;
        if (!pool) return;
        for (int i = 0; i < pool->m_nSize && count < kCarRailCars; ++i) {
            CVehicle* v = pool->GetAt(i);
            if (!v) continue;
            const CVector p = v->GetPosition();
            if ((p - c).Magnitude() > kRailNear) continue;
            // 8.33: rails on STOPPED / slow cars only. A rail that moves under the
            // skater blows reTHAWed's grind maths to NaN, so a moving car gives no
            // rail (user 2026-10-12: option a).
            if (v->m_vecMoveSpeed.Magnitude() > 0.05f) continue;
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
            log("car %d: model %d pos %.1f %.1f %.1f | %.0f m from CJ | speed %.1f | driver %s | box top %.2f -> rail +%.2f",
                i, static_cast<int>(v->m_nModelIndex), p.x, p.y, p.z, d, v->m_vecMoveSpeed.Magnitude(),
                v->m_pDriver ? "yes" : "no", boxTop, boxTop > 0.0f ? boxTop * 0.55f : 1.35f);
            ++n;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    log("cars: %d within %.0f m", n, kNear);
}

} // namespace mp::gta
