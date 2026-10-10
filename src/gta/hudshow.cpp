// my-passthrough: reTHAWed's HUD shown in GTA. See hudshow.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "RenderWare.h"
#include <windows.h>

#include "hudshow.h"
#include "../common/hudlink.h"

#include <cstring>

namespace mp::gta {
namespace {

constexpr DWORD kMaxAgeMs = 250; // older picture = reTHAWed isn't sending; show nothing

LogFn g_log = nullptr;
HANDLE g_map = nullptr;
HudLinkHeader* g_hl = nullptr;
DWORD g_nextTry = 0;
RwRaster* g_raster = nullptr;
int g_rw = 0, g_rh = 0;
LONG g_lastFrame = -1;
bool g_haveImage = false;
bool g_loggedFirst = false;

// numbers for the 10-second log line
unsigned g_shown = 0, g_uploads = 0, g_torn = 0, g_errors = 0;
double g_uploadMs = 0.0, g_uploadMsMax = 0.0;
DWORD g_nextLog = 0;

bool HaveLink()
{
    if (g_hl) return true;
    const DWORD now = GetTickCount();
    if (now < g_nextTry) return false;
    g_nextTry = now + 2000;
    DWORD err = 0;
    g_hl = OpenHudLink(g_map, &err);
    if (!g_hl && g_log) g_log("hud show: could not open the shared HUD block (Windows error %lu)", err);
    return g_hl != nullptr;
}

// Copy the newest picture into our texture when it changed.
void Upload()
{
    const LONG frame = g_hl->frame;
    if (frame == g_lastFrame) return;
    const LONG slot = g_hl->latest;
    if (slot != 0 && slot != 1) return;
    const LONG seq = g_hl->slotSeq[slot];
    if (seq & 1) { ++g_torn; return; } // being written right now; try next frame
    const int w = g_hl->slotW[slot], h = g_hl->slotH[slot];
    if (w <= 0 || h <= 0 || w > kHudMaxW || h > kHudMaxH) return;

    LARGE_INTEGER t0, t1, f;
    QueryPerformanceCounter(&t0);
    if (!g_raster || g_rw != w || g_rh != h) {
        if (g_raster) RwRasterDestroy(g_raster);
        g_raster = RwRasterCreate(w, h, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
        g_rw = w;
        g_rh = h;
        g_haveImage = false;
        if (!g_raster) { ++g_errors; return; }
    }
    RwUInt8* dst = RwRasterLock(g_raster, 0, rwRASTERLOCKWRITE | rwRASTERLOCKNOFETCH);
    if (!dst) { ++g_errors; return; }
    const int stride = RwRasterGetStride(g_raster);
    const BYTE* src = HudSlot(g_hl, slot);
    const size_t row = static_cast<size_t>(w) * 4;
    for (int y = 0; y < h; ++y) memcpy(dst + static_cast<size_t>(y) * stride, src + y * row, row);
    RwRasterUnlock(g_raster);
    if (g_hl->slotSeq[slot] != seq) { ++g_torn; return; } // overwritten while copying; next frame
    g_lastFrame = frame;
    g_haveImage = true;
    ++g_uploads;
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&f);
    const double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / (f.QuadPart ? f.QuadPart : 1);
    g_uploadMs += ms;
    if (ms > g_uploadMsMax) g_uploadMsMax = ms;
    if (!g_loggedFirst && g_log) {
        g_loggedFirst = true;
        g_log("hud show: showing reTHAWed's HUD (%dx%d picture) over GTA's screen", w, h);
    }
}

void Draw()
{
    const float sw = static_cast<float>(RsGlobal.maximumWidth), sh = static_cast<float>(RsGlobal.maximumHeight);
    const float z = RwIm2DGetNearScreenZ();
    RwIm2DVertex v[4] = {};
    const float xs[4] = {0.0f, sw, 0.0f, sw}, ys[4] = {0.0f, 0.0f, sh, sh};
    const float us[4] = {0.0f, 1.0f, 0.0f, 1.0f}, vs[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
        RwIm2DVertexSetScreenX(&v[i], xs[i] - 0.5f);
        RwIm2DVertexSetScreenY(&v[i], ys[i] - 0.5f);
        RwIm2DVertexSetScreenZ(&v[i], z);
        RwIm2DVertexSetRecipCameraZ(&v[i], 1.0f);
        RwIm2DVertexSetIntRGBA(&v[i], 255, 255, 255, 255);
        RwIm2DVertexSetU(&v[i], us[i], 1.0f);
        RwIm2DVertexSetV(&v[i], vs[i], 1.0f);
    }

    void *raster, *zTest, *zWrite, *vAlpha, *src, *dst, *filter, *addr, *fog;
    RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &raster);
    RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &zTest);
    RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &zWrite);
    RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &vAlpha);
    RwRenderStateGet(rwRENDERSTATESRCBLEND, &src);
    RwRenderStateGet(rwRENDERSTATEDESTBLEND, &dst);
    RwRenderStateGet(rwRENDERSTATETEXTUREFILTER, &filter);
    RwRenderStateGet(rwRENDERSTATETEXTUREADDRESS, &addr);
    RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fog);

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, g_raster);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, reinterpret_cast<void*>(FALSE));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, reinterpret_cast<void*>(FALSE));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, reinterpret_cast<void*>(TRUE));
    RwRenderStateSet(rwRENDERSTATESRCBLEND, reinterpret_cast<void*>(rwBLENDONE)); // colours are pre-multiplied
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, reinterpret_cast<void*>(rwBLENDINVSRCALPHA));
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, reinterpret_cast<void*>(rwFILTERLINEAR));
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, reinterpret_cast<void*>(rwTEXTUREADDRESSCLAMP));
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, reinterpret_cast<void*>(FALSE));

    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, v, 4);

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, zTest);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, zWrite);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, vAlpha);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, src);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, dst);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, filter);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, addr);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, fog);
    ++g_shown;
}

void Frame()
{
    const DWORD now = GetTickCount();
    if (g_log && now >= g_nextLog) {
        if (g_nextLog && (g_shown || g_uploads || g_torn || g_errors))
            g_log("hud show: last 10 s: shown in %u frames, %u new pictures (%.2f ms each on average, %.2f ms at most), "
                  "%u half-written skipped, %u errors",
                  g_shown, g_uploads, g_uploads ? g_uploadMs / g_uploads : 0.0, g_uploadMsMax, g_torn, g_errors);
        g_nextLog = now + 10000;
        g_shown = g_uploads = g_torn = g_errors = 0;
        g_uploadMs = g_uploadMsMax = 0.0;
    }
    if (!RideModeIsOn() || !HaveLink() || g_hl->magic != kHudMagic) return;
    if (now - static_cast<DWORD>(g_hl->tickMs) > kMaxAgeMs) return; // reTHAWed not sending
    Upload();
    if (g_haveImage && g_raster) Draw();
}

void FrameSafe()
{
    __try {
        Frame();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++g_errors;
    }
}

} // namespace

void HudShowAddHook(LogFn log)
{
    g_log = log;
    // Right after GTA drew its own HUD (call at 0x53E4FF): ours goes on top.
    plugin::Events::drawHudEvent.Add([] { FrameSafe(); });
}

} // namespace mp::gta
