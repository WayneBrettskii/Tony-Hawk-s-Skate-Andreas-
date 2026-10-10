// my-passthrough: radio while skating. See radio.h and docs/DESIGN.md 8.27.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CAudioEngine.h"
#include "CAERadioTrackManager.h"
#include "CFont.h"
#include "CTxdStore.h"
#include "RenderWare.h"
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

#include "radio.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

#ifndef MP_RETHAWED_DIR
#error "MP_RETHAWED_DIR must be set by CMakeLists.txt"
#endif

namespace mp::gta {
namespace {

constexpr DWORD kHoldMs = 400;       // Back held this long = the wheel, not reTHAWed
constexpr int kTapFrames = 4;        // a tap reaches reTHAWed as this many frames of Back
constexpr DWORD kShowMs = 2500;      // station name at the top after choosing
constexpr int kFirstGta = 1, kLastGta = 12; // GTA station ids: 1 Playback FM ... 11 WCTR, 12 User Track Player (log 20:45)
constexpr int kThaw = -2, kOff = -1;
constexpr float kStickDeadZone = 0.5f; // of full tilt; below that the pointed slot stays

// ---------------------------------------------------------------------------
// GTA patches that let a GTA station play on foot (8.27 fix 1 and fix 2)
// ---------------------------------------------------------------------------

// Fix 1: GTA's interior-radio code (0x4D6E60) stops the radio outside clubs and
// houses with these three calls to AudioEngine.StopRadio (0x506F70).
const uintptr_t kAmbienceStopCalls[3] = {0x4D71D2, 0x4D71FD, 0x4D7221};
constexpr uintptr_t kStopRadio = 0x506F70;
// Fix 2 (the one that mattered, 21:01): GTA's radio code (0x4EA590) asks "is an
// interior radio playing?" when CJ has no vehicle radio and stops the radio if
// not (call at 0x4EA5C3 to 0x507280).
constexpr uintptr_t kAmbienceRadioQuestion = 0x4EA5C3;
constexpr uintptr_t kIsAmbienceRadioActive = 0x507280;

// Volume (user 21:28: GTA's stations drowned out the skating sounds): GTA sets
// its music fader every frame from the screen fade (call at 0x50B815 to
// AudioEngine.SetMusicFaderScalingFactor 0x506E40). We multiply that value by
// our skate-radio level while our station plays. Music only; sound effects and
// reTHAWed are not touched.
constexpr uintptr_t kMusicFaderCall = 0x50B815;
constexpr uintptr_t kSetMusicFader = 0x506E40;
constexpr float kDefaultLevel = 0.5f;

bool g_ownRadio = false;      // a GTA station is playing for skate mode
bool g_patchTried = false;
float g_radioLevel = kDefaultLevel; // 0..1, changed with D-pad Up/Down while the wheel is open

void __fastcall AmbienceStopRadio(CAudioEngine* self, void*, tVehicleAudioSettings* settings, bool arg)
{
    if (g_ownRadio) return; // our station: keep playing
    self->StopRadio(settings, arg);
}

bool __fastcall OurIsAmbienceRadioActive(CAudioEngine* self, void*)
{
    if (g_ownRadio) return true; // GTA treats our station like an interior radio
    return plugin::CallMethodAndReturn<bool, kIsAmbienceRadioActive, CAudioEngine*>(self);
}

void __fastcall OurMusicFader(CAudioEngine* self, void*, float factor)
{
    if (g_ownRadio) factor *= g_radioLevel;
    plugin::CallMethod<kSetMusicFader, CAudioEngine*, float>(self, factor);
}

// Fix (test 21:35: no change in volume): GTA only calls the fader during a
// screen fade (CCamera 0x50B6D0 returns early otherwise), so we also set it
// ourselves every frame while our station plays, and back to 1 afterwards.
bool g_faderOurs = false;

void ApplyFader()
{
    __try {
        if (g_ownRadio) {
            plugin::CallMethod<kSetMusicFader, CAudioEngine*, float>(&AudioEngine, g_radioLevel);
            g_faderOurs = true;
        } else if (g_faderOurs) {
            plugin::CallMethod<kSetMusicFader, CAudioEngine*, float>(&AudioEngine, 1.0f);
            g_faderOurs = false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool CallsTo(uintptr_t site, uintptr_t target)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(site);
    return p[0] == 0xE8 && site + 5 + *reinterpret_cast<const int*>(p + 1) == target;
}

void PatchGta(LogFn log)
{
    if (g_patchTried) return;
    g_patchTried = true;
    if (CallsTo(kAmbienceRadioQuestion, kIsAmbienceRadioActive)) {
        plugin::patch::RedirectCall(kAmbienceRadioQuestion, reinterpret_cast<void*>(&OurIsAmbienceRadioActive));
        if (log) log("radio: GTA treats our skate-mode station like an interior radio (call at 0x4EA5C3 redirected)");
    } else if (log) {
        log("radio: call at 0x4EA5C3 is not GTA's original (another mod?); GTA stations will stop on foot");
    }
    if (CallsTo(kMusicFaderCall, kSetMusicFader)) {
        plugin::patch::RedirectCall(kMusicFaderCall, reinterpret_cast<void*>(&OurMusicFader));
        if (log) log("radio: skate-radio volume control on (call at 0x50B815 redirected), level %d%%",
                     static_cast<int>(g_radioLevel * 100.0f + 0.5f));
    } else if (log) {
        log("radio: call at 0x50B815 is not GTA's original (another mod?); skate-radio volume can't be changed");
    }
    bool all = true;
    for (uintptr_t a : kAmbienceStopCalls) all = all && CallsTo(a, kStopRadio);
    if (all) {
        for (uintptr_t a : kAmbienceStopCalls) plugin::patch::RedirectCall(a, reinterpret_cast<void*>(&AmbienceStopRadio));
        if (log) log("radio: GTA's interior-radio code doesn't stop our station (3 calls redirected)");
    } else if (log) {
        log("radio: GTA's interior-radio stop calls are not the original (another mod?); left alone");
    }
}

// ---------------------------------------------------------------------------
// Stations
// ---------------------------------------------------------------------------

int g_list[2 + (kLastGta - kFirstGta + 1)]; // reTHAWed, GTA 1..12, Off
LinkBlock* g_link = nullptr;   // for the wheel's reTHAWed volume (set by RadioFrame)
LONG g_thawVolumeWish = -1;    // reTHAWed music volume 0..100 asked for on the wheel; -1 = not changed
int g_count = 0;
int g_sel = 0;           // index in g_list; 0 = reTHAWed
int g_playingGta = -1;   // GTA station we started, -1 none
bool g_skateWas = false;
DWORD g_showUntil = 0;
char g_name[64] = "";
bool g_loggedNames = false;
DWORD g_nextCheck = 0;
int g_restarts = 0;      // restarts of the current station (max 1; each start loads it = a hitch)

void BuildList()
{
    if (g_count) return;
    g_list[g_count++] = kThaw;
    for (int id = kFirstGta; id <= kLastGta; ++id) g_list[g_count++] = id;
    g_list[g_count++] = kOff;
}

void NameOf(int station, char* out, size_t size)
{
    if (station == kThaw) { sprintf_s(out, size, "reTHAWed soundtrack"); return; }
    if (station == kOff) { sprintf_s(out, size, "Radio off"); return; }
    const char* n = nullptr;
    __try {
        n = AERadioTrackManager.GetRadioStationName(static_cast<signed char>(station));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        n = nullptr;
    }
    if (n && n[0]) sprintf_s(out, size, "%s", n);
    else sprintf_s(out, size, "Station %d", station);
}

void StopGta(LogFn log)
{
    g_ownRadio = false;
    if (g_playingGta < 0) return;
    __try {
        AudioEngine.StopRadio(nullptr, false);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (log) log("radio: ERROR stopping GTA's radio");
    }
    g_playingGta = -1;
}

void StartGta(int id, LogFn log)
{
    __try {
        g_ownRadio = true;
        AudioEngine.StartRadio(id, 0);
        g_playingGta = id;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (log) log("radio: ERROR starting GTA station %d", id);
        g_playingGta = -1;
        g_ownRadio = false;
    }
}

void Tell(LinkBlock* link, LONG wish)
{
    if (!link) return;
    InterlockedExchange(&link->reserved[1], wish);
    InterlockedExchange(&link->reserved[3], g_thawVolumeWish); // before the tick (THAW checks the tick)
    InterlockedExchange(&link->reserved[2], static_cast<LONG>(GetTickCount()));
}

// reTHAWed's music volume as the wheel shows it: what we asked for, else what
// reTHAWed reports (link reserved[4]).
int ThawVolume()
{
    if (g_thawVolumeWish >= 0) return g_thawVolumeWish;
    if (g_link) {
        const LONG v = g_link->reserved[4];
        if (v >= 0 && v <= 100) return v;
    }
    return 100;
}

// ---------------------------------------------------------------------------
// Icons (R2): GTA's own station logos from models\fronten1.txd, reTHAWed's
// logo from its install folder. Read at run time; nothing copied.
// ---------------------------------------------------------------------------

const char* const kGtaIconNames[kLastGta - kFirstGta + 1] = {
    "radio_playback", "radio_krose", "radio_KDST", "radio_bounce", "radio_SFUR", "radio_RLS",
    "radio_RADIOX", "radio_csr", "radio_kjah", "radio_mastersounds", "radio_WCTR", "radio_TPLAYER",
};
RwTexture* g_gtaIcons[kLastGta - kFirstGta + 1] = {};
RwRaster* g_thawIcon = nullptr;
bool g_iconsTried = false;

int LoadGtaIconsUnsafe()
{
    int slot = CTxdStore::FindTxdSlot("mp_radio");
    if (slot == -1) slot = CTxdStore::AddTxdSlot("mp_radio");
    if (slot == -1) return -1;
    if (!CTxdStore::LoadTxd(slot, "MODELS\\FRONTEN1.TXD")) return -2;
    CTxdStore::AddRef(slot);
    CTxdStore::PushCurrentTxd();
    CTxdStore::SetCurrentTxd(slot);
    int found = 0;
    for (int i = 0; i <= kLastGta - kFirstGta; ++i) {
        g_gtaIcons[i] = RwTextureRead(kGtaIconNames[i], nullptr);
        if (g_gtaIcons[i]) ++found;
    }
    CTxdStore::PopCurrentTxd();
    return found;
}

int LoadGtaIcons()
{
    __try {
        return LoadGtaIconsUnsafe();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -3;
    }
}

template <typename T> void Release(T*& p)
{
    if (p) p->Release();
    p = nullptr;
}

// reTHAWed's logo (.ico, 256x256) through Windows' own image reader, into a
// RenderWare picture (8888, straight alpha).
RwRaster* LoadIcoRaster(const wchar_t* path, LogFn log)
{
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    RwRaster* raster = nullptr;
    BYTE* pixels = nullptr;
    UINT w = 0, h = 0;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr)) { // the largest frame
        UINT count = 0, best = 0, bestW = 0;
        decoder->GetFrameCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IWICBitmapFrameDecode* f = nullptr;
            if (FAILED(decoder->GetFrame(i, &f))) continue;
            UINT fw = 0, fh = 0;
            f->GetSize(&fw, &fh);
            if (fw > bestW && fw <= 256) { bestW = fw; best = i; }
            f->Release();
        }
        hr = decoder->GetFrame(best, &frame);
    }
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr)) hr = conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) hr = conv->GetSize(&w, &h);
    if (SUCCEEDED(hr) && w && h) {
        pixels = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(w) * h * 4));
        if (!pixels) hr = E_OUTOFMEMORY;
        else hr = conv->CopyPixels(nullptr, w * 4, w * h * 4, pixels);
    }
    if (SUCCEEDED(hr) && pixels) {
        raster = RwRasterCreate(static_cast<RwInt32>(w), static_cast<RwInt32>(h), 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
        RwUInt8* dst = raster ? RwRasterLock(raster, 0, rwRASTERLOCKWRITE | rwRASTERLOCKNOFETCH) : nullptr;
        if (dst) {
            const int stride = RwRasterGetStride(raster);
            for (UINT y = 0; y < h; ++y) memcpy(dst + static_cast<size_t>(y) * stride, pixels + static_cast<size_t>(y) * w * 4, w * 4);
            RwRasterUnlock(raster);
        } else if (raster) {
            RwRasterDestroy(raster);
            raster = nullptr;
        }
    }
    if (pixels) HeapFree(GetProcessHeap(), 0, pixels);
    Release(conv);
    Release(frame);
    Release(decoder);
    Release(factory);
    if (co == S_OK || co == S_FALSE) CoUninitialize();
    if (log) {
        if (raster) log("radio: reTHAWed logo loaded (%ux%u)", w, h);
        else log("radio: could not load reTHAWed's logo (error 0x%08lX); its slot shows the name", static_cast<unsigned long>(hr));
    }
    return raster;
}

void LoadIcons(LogFn log)
{
    if (g_iconsTried) return;
    g_iconsTried = true;
    const int found = LoadGtaIcons();
    if (log) {
        if (found >= 0) log("radio: %d of %d GTA station logos found in models\\fronten1.txd", found, kLastGta - kFirstGta + 1);
        else log("radio: could not load models\\fronten1.txd (%d); GTA slots show the names", found);
    }
    g_thawIcon = LoadIcoRaster(L"" MP_RETHAWED_DIR L"\\ReTHAWed_NotifIcon.ico", log);
}

RwRaster* IconOf(int station)
{
    if (station == kThaw) return g_thawIcon;
    if (station >= kFirstGta && station <= kLastGta) {
        RwTexture* t = g_gtaIcons[station - kFirstGta];
        return t ? RwTextureGetRaster(t) : nullptr;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// The wheel (R2): hold Back, point the right stick, let go
// ---------------------------------------------------------------------------

// pad filter state (GTA main thread)
bool g_backDown = false, g_backFired = false;
DWORD g_backStart = 0;
int g_tapFrames = 0;
bool g_wheelOpen = false;
int g_hover = 0;          // pointed slot (index in g_list)
bool g_choose = false;    // Back released with the wheel open: take g_hover
bool g_upWas = false, g_downWas = false;
int g_levelChanged = 0;   // for the log

void Im2DQuad(RwRaster* raster, float x0, float y0, float x1, float y1, RwUInt8 r, RwUInt8 g, RwUInt8 b, RwUInt8 a)
{
    const float z = RwIm2DGetNearScreenZ();
    RwIm2DVertex v[4] = {};
    const float xs[4] = {x0, x1, x0, x1}, ys[4] = {y0, y0, y1, y1}, us[4] = {0, 1, 0, 1}, vs[4] = {0, 0, 1, 1};
    for (int i = 0; i < 4; ++i) {
        RwIm2DVertexSetScreenX(&v[i], xs[i]);
        RwIm2DVertexSetScreenY(&v[i], ys[i]);
        RwIm2DVertexSetScreenZ(&v[i], z);
        RwIm2DVertexSetRecipCameraZ(&v[i], 1.0f);
        RwIm2DVertexSetIntRGBA(&v[i], r, g, b, a);
        RwIm2DVertexSetU(&v[i], us[i], 1.0f);
        RwIm2DVertexSetV(&v[i], vs[i], 1.0f);
    }
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, v, 4);
}

void Im2DDisc(float cx, float cy, float radius, RwUInt8 a)
{
    constexpr int kSteps = 48;
    RwIm2DVertex v[kSteps + 2] = {};
    const float z = RwIm2DGetNearScreenZ();
    for (int i = 0; i < kSteps + 2; ++i) {
        float x = cx, y = cy;
        if (i > 0) {
            const float ang = (i - 1) * 6.2831853f / kSteps;
            x = cx + radius * std::sin(ang);
            y = cy - radius * std::cos(ang);
        }
        RwIm2DVertexSetScreenX(&v[i], x);
        RwIm2DVertexSetScreenY(&v[i], y);
        RwIm2DVertexSetScreenZ(&v[i], z);
        RwIm2DVertexSetRecipCameraZ(&v[i], 1.0f);
        RwIm2DVertexSetIntRGBA(&v[i], 0, 0, 0, a);
    }
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nullptr);
    RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, v, kSteps + 2);
}

void Text(float x, float y, float scale, const char* text, CRGBA color)
{
    const float sw = static_cast<float>(RsGlobal.maximumWidth), sh = static_cast<float>(RsGlobal.maximumHeight);
    CFont::SetBackground(false, false);
    CFont::SetProportional(true);
    CFont::SetJustify(false);
    CFont::SetOrientation(ALIGN_CENTER);
    CFont::SetCentreSize(sw * 0.3f);
    CFont::SetFontStyle(FONT_MENU);
    CFont::SetScale(sw / 640.0f * 0.5f * scale, sh / 448.0f * 1.0f * scale);
    CFont::SetColor(color);
    CFont::SetEdge(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::PrintString(x, y, text);
}

void DrawWheel()
{
    const float sw = static_cast<float>(RsGlobal.maximumWidth), sh = static_cast<float>(RsGlobal.maximumHeight);
    const float cx = sw * 0.5f, cy = sh * 0.5f;
    const float ring = sh * 0.33f, icon = sh * 0.11f;

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
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, reinterpret_cast<void*>(FALSE));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, reinterpret_cast<void*>(FALSE));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, reinterpret_cast<void*>(TRUE));
    RwRenderStateSet(rwRENDERSTATESRCBLEND, reinterpret_cast<void*>(rwBLENDSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, reinterpret_cast<void*>(rwBLENDINVSRCALPHA));
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, reinterpret_cast<void*>(rwFILTERLINEAR));
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, reinterpret_cast<void*>(rwTEXTUREADDRESSCLAMP));
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, reinterpret_cast<void*>(FALSE));

    Im2DDisc(cx, cy, ring + icon * 0.95f, 150);
    for (int k = 0; k < g_count; ++k) {
        const float ang = k * 6.2831853f / g_count;
        const float x = cx + ring * std::sin(ang), y = cy - ring * std::cos(ang);
        const bool hover = k == g_hover, current = k == g_sel;
        const float half = icon * (hover ? 0.62f : 0.45f);
        if (current) Im2DQuad(nullptr, x - half - 4, y - half - 4, x + half + 4, y + half + 4, 225, 172, 61, 255); // gold frame
        if (hover) Im2DQuad(nullptr, x - half - 2, y - half - 2, x + half + 2, y + half + 2, 255, 255, 255, 230);
        RwRaster* r = IconOf(g_list[k]);
        const RwUInt8 shade = hover ? 255 : 150; // the others a bit darker
        if (r) {
            Im2DQuad(r, x - half, y - half, x + half, y + half, shade, shade, shade, 255);
        } else {
            Im2DQuad(nullptr, x - half, y - half, x + half, y + half, 30, 30, 30, 255);
            char n[64];
            NameOf(g_list[k], n, sizeof(n));
            Text(x, y - sh * 0.012f, 0.55f, g_list[k] == kOff ? "OFF" : n, CRGBA(shade, shade, shade, 255));
        }
    }
    char hovered[64];
    NameOf(g_list[g_hover], hovered, sizeof(hovered));
    Text(cx, cy - sh * 0.04f, 1.0f, hovered, CRGBA(255, 255, 255, 255));
    // the D-pad changes the volume of the station pointed at (user 21:49)
    const int pointed = g_list[g_hover];
    if (pointed != kOff) {
        char level[48];
        if (pointed == kThaw) sprintf_s(level, "reTHAWed music volume %d%%", ThawVolume());
        else sprintf_s(level, "GTA radio volume %d%%", static_cast<int>(g_radioLevel * 100.0f + 0.5f));
        Text(cx, cy + sh * 0.02f, 0.7f, level, CRGBA(225, 172, 61, 255));
        Text(cx, cy + sh * 0.06f, 0.55f, "D-pad up / down", CRGBA(170, 170, 170, 255));
    }

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, zTest);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, zWrite);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, vAlpha);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, src);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, dst);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, filter);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, addr);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, fog);
}

void DrawName()
{
    if (!g_skateWas || g_wheelOpen || GetTickCount() >= g_showUntil || !g_name[0]) return;
    const float sw = static_cast<float>(RsGlobal.maximumWidth), sh = static_cast<float>(RsGlobal.maximumHeight);
    Text(sw * 0.5f, sh * 22.0f / 448.0f, 1.2f, g_name, CRGBA(225, 172, 61, 255)); // gold, like GTA's radio names
}

void DrawSafe()
{
    __try {
        if (g_wheelOpen && g_count) DrawWheel();
        DrawName();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_showUntil = 0;
        g_wheelOpen = false;
    }
}

} // namespace

void RadioFilterPad(LONG& buttons, short* axes)
{
    if (!SkateModeIsOn()) {
        g_backDown = g_backFired = false;
        g_tapFrames = 0;
        g_wheelOpen = false;
        return;
    }
    const DWORD now = GetTickCount();
    const bool back = (buttons & kBtnSelect) != 0;
    buttons &= ~kBtnSelect; // reTHAWed only gets Back from us (below)
    if (back) {
        if (!g_backDown) {
            g_backDown = true;
            g_backFired = false;
            g_backStart = now;
        } else if (!g_backFired && now - g_backStart >= kHoldMs) {
            g_backFired = true;
            g_wheelOpen = true;
            g_hover = g_sel;
        }
    } else if (g_backDown) {
        g_backDown = false;
        if (!g_backFired) g_tapFrames = kTapFrames; // a tap: pass it on now
        if (g_wheelOpen) {
            g_wheelOpen = false;
            g_choose = true;
        }
    }
    if (g_tapFrames > 0) {
        buttons |= kBtnSelect;
        --g_tapFrames;
    }
    if (g_wheelOpen) {
        // D-pad Up / Down = GTA radio volume (kept from reTHAWed meanwhile)
        const bool up = (buttons & kBtnUp) != 0, down = (buttons & kBtnDown) != 0;
        const int step = (up && !g_upWas) ? 1 : (down && !g_downWas) ? -1 : 0;
        const int pointed = g_count ? g_list[g_hover] : kOff;
        if (step && pointed == kThaw) { // reTHAWed's own music volume, 0..100 in steps of 10
            int v = (ThawVolume() + 5) / 10 * 10 + step * 10;
            g_thawVolumeWish = v < 0 ? 0 : v > 100 ? 100 : v;
            ++g_levelChanged;
        } else if (step && pointed != kOff) { // GTA's radio while skating
            const float v = g_radioLevel + step * 0.1f;
            g_radioLevel = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
            ++g_levelChanged;
        }
        g_upWas = up;
        g_downWas = down;
        buttons &= ~(kBtnUp | kBtnDown);
    } else {
        g_upWas = g_downWas = false;
    }
    if (g_wheelOpen && g_count) {
        // right stick points at a slot (GTA: about -128..128, up = negative)
        const float x = axes[kAxisRX] / 128.0f, y = axes[kAxisRY] / 128.0f;
        if (x * x + y * y >= kStickDeadZone * kStickDeadZone) {
            float ang = std::atan2(x, -y); // 0 = up, clockwise
            if (ang < 0) ang += 6.2831853f;
            g_hover = static_cast<int>(std::floor(ang / (6.2831853f / g_count) + 0.5f)) % g_count;
        }
        axes[kAxisRX] = 0; // reTHAWed's camera doesn't turn meanwhile
        axes[kAxisRY] = 0;
    }
}

void RadioFrame(LinkBlock* link, LogFn log)
{
    g_link = link;
    BuildList();
    PatchGta(log);
    const bool on = SkateModeIsOn();
    if (!on) {
        // user 20:35: reTHAWed's music only plays in skate mode while GTA runs
        Tell(link, kMusicPause);
        if (g_skateWas) {
            StopGta(log);
            if (log) log("radio: skate mode off: GTA radio stopped, reTHAWed's music paused");
        }
        g_skateWas = false;
        g_choose = false;
        ApplyFader(); // back to normal volume
        return;
    }
    const DWORD now = GetTickCount();
    if (!g_skateWas) {
        g_skateWas = true;
        LoadIcons(log);
        if (!g_loggedNames && log) {
            g_loggedNames = true;
            char line[600] = "radio: GTA station names:";
            for (int id = kFirstGta; id <= kLastGta; ++id) {
                char n[64], part[80];
                NameOf(id, n, sizeof(n));
                sprintf_s(part, " %d=%s", id, n);
                strcat_s(line, part);
            }
            log("%s", line);
        }
        NameOf(g_list[g_sel], g_name, sizeof(g_name));
        g_showUntil = now + kShowMs;
        if (log) log("radio: skate mode on: station %s", g_name);
    }
    if (g_choose) {
        g_choose = false;
        if (g_hover != g_sel && g_hover >= 0 && g_hover < g_count) {
            g_sel = g_hover;
            NameOf(g_list[g_sel], g_name, sizeof(g_name));
            g_showUntil = now + kShowMs;
            if (log) log("radio: wheel: station %s", g_name);
        }
        if (g_levelChanged && log) {
            log("radio: volumes now: GTA radio while skating %d%%, reTHAWed music %d%%",
                static_cast<int>(g_radioLevel * 100.0f + 0.5f), ThawVolume());
            g_levelChanged = 0;
        }
    }

    const int station = g_list[g_sel];
    Tell(link, station == kThaw ? kMusicPlay : kMusicPause);
    const int want = station >= 0 ? station : -1;
    if (want != g_playingGta) {
        StopGta(log);
        if (want >= 0) StartGta(want, log);
        g_nextCheck = now + 3000;
        g_restarts = 0;
    } else if (g_playingGta >= 0 && now >= g_nextCheck) {
        g_nextCheck = now + 3000;
        bool radioOn = true;
        __try {
            radioOn = AudioEngine.IsRadioOn();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        if (!radioOn && g_restarts < 1) {
            ++g_restarts;
            if (log) log("radio: GTA's radio had stopped (station %d); starting it once more", g_playingGta);
            StartGta(g_playingGta, log);
        }
    }
    ApplyFader(); // skate-radio volume (every frame while our station plays)
}

void RadioAddHook()
{
    plugin::Events::drawHudEvent.Add([] { DrawSafe(); });
}

} // namespace mp::gta
