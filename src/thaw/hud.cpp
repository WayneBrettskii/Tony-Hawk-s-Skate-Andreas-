// my-passthrough: reTHAWed HUD catcher (phase 6 H2). See hud.h.

#include "hud.h"

#include "../common/hudlink.h"

#include <cstdio>
#include <cstring>

#ifndef MP_LOG_DIR
#error "MP_LOG_DIR must be set by CMakeLists.txt"
#endif

namespace mp::thaw {
namespace {

enum { S_SETRT = 37, S_CLEAR = 43, S_SETVIEWPORT = 47, S_SETRENDERSTATE = 57 };
void* const* g_orig = nullptr;

using SetRTFn = HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
using ClearFn = HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
using SetVPFn = HRESULT(__stdcall*)(IDirect3DDevice9*, const D3DVIEWPORT9*);
using SetRSFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
HRESULT OSetRT(IDirect3DDevice9* d, DWORD i, IDirect3DSurface9* s) { return reinterpret_cast<SetRTFn>(g_orig[S_SETRT])(d, i, s); }
HRESULT OSetVP(IDirect3DDevice9* d, const D3DVIEWPORT9* v) { return reinterpret_cast<SetVPFn>(g_orig[S_SETVIEWPORT])(d, v); }
HRESULT OSetRS(IDirect3DDevice9* d, D3DRENDERSTATETYPE s, DWORD v) { return reinterpret_cast<SetRSFn>(g_orig[S_SETRENDERSTATE])(d, s, v); }
HRESULT OClear(IDirect3DDevice9* d, DWORD c) { return reinterpret_cast<ClearFn>(g_orig[S_CLEAR])(d, 0, nullptr, D3DCLEAR_TARGET, c, 1.0f, 0); }

// The alpha states we set while the HUD draws (and the game's values to put back).
// Test 19:18: the picture's alpha stayed 0 everywhere, so the game probably
// draws its HUD with alpha writes off (colour write mask); we switch them on.
const D3DRENDERSTATETYPE kStates[] = {D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA,
                                      D3DRS_BLENDOPALPHA, D3DRS_COLORWRITEENABLE};
const DWORD kOurs[] = {TRUE, D3DBLEND_ONE, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD, 0xF};
const DWORD kDefaults[] = {FALSE, D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD, 0xF};
constexpr int kStateCount = 5;

// render thread state
volatile LONG g_allowed = 0;
bool g_doneThisFrame = false;   // HUD already caught (or skipped) this frame
bool g_active = false;
IDirect3DTexture9* g_tex = nullptr;
IDirect3DSurface9* g_surf = nullptr;
UINT g_w = 0, g_h = 0;
IDirect3DSurface9* g_gameRt = nullptr;
IDirect3DSurface9* g_gameDs = nullptr;
D3DVIEWPORT9 g_gameVp = {};
DWORD g_saved[kStateCount] = {};
unsigned g_draws = 0;           // HUD draws this frame

// numbers for the log (read by HudTick)
volatile LONG g_frames = 0, g_framesCaught = 0, g_lastDraws = 0, g_failures = 0;
volatile LONG g_lastError = 0;
volatile LONG g_shotAlphaPx = 0, g_shotColourPx = 0, g_writeMask = -1; // what the test picture had

// test picture: 0 none, 1 wanted, 2 ready in g_shot (half size, BGRA), 3 failed
volatile LONG g_shotState = 0;
BYTE* g_shot = nullptr;
UINT g_shotW = 0, g_shotH = 0;
IDirect3DSurface9* g_sys = nullptr;

template <typename T> void Rel(T*& p)
{
    if (p) p->Release();
    p = nullptr;
}

bool MakePicture(IDirect3DDevice9* dev, UINT w, UINT h)
{
    if (g_tex && g_w == w && g_h == h) return true;
    Rel(g_surf);
    Rel(g_tex);
    Rel(g_sys);
    HRESULT hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_tex, nullptr);
    if (FAILED(hr) || FAILED(hr = g_tex->GetSurfaceLevel(0, &g_surf))) {
        InterlockedExchange(&g_lastError, hr);
        Rel(g_tex);
        return false;
    }
    g_w = w;
    g_h = h;
    return true;
}

// Copy our picture to memory at half size (2x2 average), un-premultiplied so
// image viewers show it right.
void TakeShot(IDirect3DDevice9* dev)
{
    if (!g_sys && FAILED(dev->CreateOffscreenPlainSurface(g_w, g_h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &g_sys, nullptr))) {
        InterlockedExchange(&g_shotState, 3);
        return;
    }
    D3DLOCKED_RECT lr = {};
    if (FAILED(dev->GetRenderTargetData(g_surf, g_sys)) || FAILED(g_sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
        InterlockedExchange(&g_shotState, 3);
        return;
    }
    const UINT w = g_w / 2, h = g_h / 2;
    if (!g_shot || g_shotW != w || g_shotH != h) {
        if (g_shot) VirtualFree(g_shot, 0, MEM_RELEASE);
        g_shot = static_cast<BYTE*>(VirtualAlloc(nullptr, static_cast<SIZE_T>(w) * h * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        g_shotW = w;
        g_shotH = h;
    }
    LONG alphaPx = 0, colourPx = 0;
    if (g_shot) {
        const BYTE* src = static_cast<const BYTE*>(lr.pBits);
        for (UINT y = 0; y < h; ++y) {
            const BYTE* r0 = src + static_cast<size_t>(y * 2) * lr.Pitch;
            const BYTE* r1 = r0 + lr.Pitch;
            BYTE* out = g_shot + static_cast<size_t>(y) * w * 4;
            for (UINT x = 0; x < w; ++x) {
                unsigned sum[4];
                for (int c = 0; c < 4; ++c)
                    sum[c] = (r0[x * 8 + c] + r0[x * 8 + 4 + c] + r1[x * 8 + c] + r1[x * 8 + 4 + c]) / 4;
                const unsigned a = sum[3];
                if (a) ++alphaPx;
                if (sum[0] | sum[1] | sum[2]) ++colourPx;
                for (int c = 0; c < 3; ++c) {
                    // keep the colour as is where alpha is 0 (to see it anyway)
                    unsigned v = a ? sum[c] * 255 / a : sum[c];
                    out[x * 4 + c] = static_cast<BYTE>(v > 255 ? 255 : v);
                }
                out[x * 4 + 3] = static_cast<BYTE>(a);
            }
        }
    }
    g_sys->UnlockRect();
    InterlockedExchange(&g_shotAlphaPx, alphaPx);
    InterlockedExchange(&g_shotColourPx, colourPx);
    InterlockedExchange(&g_writeMask, static_cast<LONG>(g_saved[4]));
    InterlockedExchange(&g_shotState, g_shot ? 2 : 3);
}

// ---- H3: share the picture with GTA (docs/DESIGN.md 8.25) ----
HANDLE g_hlMap = nullptr;
mp::HudLinkHeader* g_hl = nullptr;
DWORD g_hlNextTry = 0;
volatile LONG g_hlError = 0;
IDirect3DSurface9* g_small[2] = {};     // shrunk picture on the graphics card
IDirect3DSurface9* g_smallSys[2] = {};  // ... copied to memory (one frame later)
bool g_pending[2] = {};
UINT g_sw = 0, g_sh = 0;
int g_ring = 0;
volatile LONG g_pubCount = 0, g_pubUsSum = 0, g_pubUsMax = 0, g_pubFails = 0;

void ReleaseSmall()
{
    for (int i = 0; i < 2; ++i) {
        Rel(g_small[i]);
        Rel(g_smallSys[i]);
        g_pending[i] = false;
    }
    g_sw = g_sh = 0;
}

bool HaveHudLink()
{
    if (g_hl) return true;
    const DWORD now = GetTickCount();
    if (now < g_hlNextTry) return false;
    g_hlNextTry = now + 2000;
    DWORD err = 0;
    g_hl = mp::OpenHudLink(g_hlMap, &err);
    if (!g_hl) {
        InterlockedExchange(&g_hlError, static_cast<LONG>(err ? err : 1));
        return false;
    }
    InterlockedExchange(&g_hl->latest, -1);
    InterlockedExchange(&g_hl->version, mp::kHudVersion);
    InterlockedExchange(&g_hl->magic, mp::kHudMagic);
    return true;
}

// Copy a picture that reached memory into the slot GTA isn't reading.
void WriteSlot(IDirect3DSurface9* sys)
{
    D3DLOCKED_RECT lr = {};
    if (FAILED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
        InterlockedIncrement(&g_pubFails);
        return;
    }
    const int slot = g_hl->latest == 0 ? 1 : 0;
    InterlockedIncrement(&g_hl->slotSeq[slot]); // odd: writing
    BYTE* dst = mp::HudSlot(g_hl, slot);
    const BYTE* src = static_cast<const BYTE*>(lr.pBits);
    const size_t row = static_cast<size_t>(g_sw) * 4;
    for (UINT y = 0; y < g_sh; ++y) memcpy(dst + y * row, src + static_cast<size_t>(y) * lr.Pitch, row);
    sys->UnlockRect();
    g_hl->slotW[slot] = static_cast<LONG>(g_sw);
    g_hl->slotH[slot] = static_cast<LONG>(g_sh);
    InterlockedIncrement(&g_hl->slotSeq[slot]); // even: done
    InterlockedExchange(&g_hl->latest, slot);
    InterlockedExchange(&g_hl->tickMs, static_cast<LONG>(GetTickCount()));
    InterlockedIncrement(&g_hl->frame);
}

// After a frame's HUD was caught: publish last frame's copy, start this one's.
void Publish(IDirect3DDevice9* dev)
{
    if (!HaveHudLink()) return;
    LARGE_INTEGER t0, t1, f;
    QueryPerformanceCounter(&t0);

    // shrunk size: fit kHudMaxW x kHudMaxH, same shape
    UINT sw = g_w, sh = g_h;
    if (sw > static_cast<UINT>(mp::kHudMaxW)) { sh = sh * mp::kHudMaxW / sw; sw = mp::kHudMaxW; }
    if (sh > static_cast<UINT>(mp::kHudMaxH)) { sw = sw * mp::kHudMaxH / sh; sh = mp::kHudMaxH; }
    if (sw != g_sw || sh != g_sh || !g_small[0]) {
        ReleaseSmall();
        for (int i = 0; i < 2; ++i) {
            if (FAILED(dev->CreateRenderTarget(sw, sh, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &g_small[i], nullptr)) ||
                FAILED(dev->CreateOffscreenPlainSurface(sw, sh, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &g_smallSys[i], nullptr))) {
                ReleaseSmall();
                InterlockedIncrement(&g_pubFails);
                return;
            }
        }
        g_sw = sw;
        g_sh = sh;
    }

    const int cur = g_ring, prev = g_ring ^ 1;
    if (g_pending[prev]) { // started last frame; should be in memory by now
        WriteSlot(g_smallSys[prev]);
        g_pending[prev] = false;
    }
    if (SUCCEEDED(dev->StretchRect(g_surf, nullptr, g_small[cur], nullptr, D3DTEXF_LINEAR)) &&
        SUCCEEDED(dev->GetRenderTargetData(g_small[cur], g_smallSys[cur])))
        g_pending[cur] = true;
    else
        InterlockedIncrement(&g_pubFails);
    g_ring = prev;

    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&f);
    const LONG us = static_cast<LONG>((t1.QuadPart - t0.QuadPart) * 1000000 / (f.QuadPart ? f.QuadPart : 1));
    InterlockedIncrement(&g_pubCount);
    InterlockedExchangeAdd(&g_pubUsSum, us);
    if (us > g_pubUsMax) InterlockedExchange(&g_pubUsMax, us);
}

void Start(IDirect3DDevice9* dev)
{
    g_doneThisFrame = true;
    IDirect3DSurface9* rt = nullptr;
    if (FAILED(dev->GetRenderTarget(0, &rt)) || !rt) return;
    D3DSURFACE_DESC d = {};
    rt->GetDesc(&d);
    if (d.Width < 640 || !MakePicture(dev, d.Width, d.Height)) {
        if (d.Width >= 640) InterlockedIncrement(&g_failures);
        rt->Release();
        return;
    }
    g_gameRt = rt;
    dev->GetDepthStencilSurface(&g_gameDs); // may stay null
    dev->GetViewport(&g_gameVp);
    for (int i = 0; i < kStateCount; ++i)
        if (FAILED(dev->GetRenderState(kStates[i], &g_saved[i]))) g_saved[i] = kDefaults[i];

    OSetRT(dev, 0, g_surf);
    dev->SetDepthStencilSurface(nullptr); // the HUD doesn't use depth; avoids a size/sampling mismatch
    OClear(dev, 0x00000000);              // fully see-through
    OSetVP(dev, &g_gameVp);
    for (int i = 0; i < kStateCount; ++i) OSetRS(dev, kStates[i], kOurs[i]);
    g_active = true;
    g_draws = 0;
}

void Stop(IDirect3DDevice9* dev)
{
    if (!g_active) return;
    g_active = false;
    if (g_shotState == 1) TakeShot(dev);
    OSetRT(dev, 0, g_gameRt);
    dev->SetDepthStencilSurface(g_gameDs);
    OSetVP(dev, &g_gameVp);
    for (int i = 0; i < kStateCount; ++i) OSetRS(dev, kStates[i], g_saved[i]);
    Rel(g_gameRt);
    Rel(g_gameDs);
    Publish(dev); // H3: share with GTA
    InterlockedIncrement(&g_framesCaught);
    InterlockedExchange(&g_lastDraws, static_cast<LONG>(g_draws));
}

void WriteTga(void (*log)(const char*, ...))
{
    static int files = 0;
    ++files;
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\thaw_hud_%d.tga", L"" MP_LOG_DIR, files);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        log("hud: could not write thaw_hud_%d.tga (error %lu)", files, GetLastError());
        return;
    }
    BYTE hdr[18] = {};
    hdr[2] = 2; // uncompressed true colour
    hdr[12] = static_cast<BYTE>(g_shotW & 0xFF);
    hdr[13] = static_cast<BYTE>(g_shotW >> 8);
    hdr[14] = static_cast<BYTE>(g_shotH & 0xFF);
    hdr[15] = static_cast<BYTE>(g_shotH >> 8);
    hdr[16] = 32;
    hdr[17] = 0x28; // 8 alpha bits, top row first
    DWORD w = 0;
    WriteFile(f, hdr, sizeof(hdr), &w, nullptr);
    WriteFile(f, g_shot, g_shotW * g_shotH * 4, &w, nullptr);
    CloseHandle(f);
    log("hud: HUD picture (%ux%u, half size) written to logs\\thaw_hud_%d.tga: %ld pixels with alpha, %ld with colour; "
        "game's colour write mask 0x%lX", g_shotW, g_shotH, files, static_cast<LONG>(g_shotAlphaPx),
        static_cast<LONG>(g_shotColourPx), static_cast<LONG>(g_writeMask));
}

} // namespace

void HudInit(void* const* orig) { g_orig = orig; }

bool HudBeforeSetRT(IDirect3DDevice9* dev, DWORD index)
{
    if (index != 0 || !g_active) return false;
    Stop(dev);
    return true;
}

bool HudBeforeIdxUP(IDirect3DDevice9* dev)
{
    if (g_active) { ++g_draws; return false; }
    if (g_doneThisFrame || !g_allowed || !g_orig) return false;
    Start(dev);
    if (g_active) ++g_draws;
    return g_active;
}

bool HudEnd(IDirect3DDevice9* dev)
{
    if (!g_active) return false;
    Stop(dev);
    return true;
}

bool HudFilterRS(D3DRENDERSTATETYPE s, DWORD v)
{
    if (!g_active) return false;
    for (int i = 0; i < kStateCount; ++i)
        if (kStates[i] == s) { g_saved[i] = v; return true; }
    return false;
}

void HudNoteViewport(const D3DVIEWPORT9* v)
{
    if (g_active && v) g_gameVp = *v;
}

void HudBeforeReset()
{
    g_active = false;
    Rel(g_gameRt);
    Rel(g_gameDs);
    Rel(g_surf);
    Rel(g_tex);
    Rel(g_sys);
    ReleaseSmall();
    g_w = g_h = 0;
}

void HudRequestShot()
{
    if (g_allowed) InterlockedCompareExchange(&g_shotState, 1, 0);
}

// Present: a new frame starts.
void HudNewFrame()
{
    g_doneThisFrame = false;
    InterlockedIncrement(&g_frames);
}

void HudTick(void (*log)(const char*, ...), bool allowed, bool toggle)
{
    static bool on = true, toggleWas = false, lastAllowed = false;
    static ULONGLONG nextLog = GetTickCount64() + 10000;
    if (toggle && !toggleWas) {
        on = !on;
        log("hud: touchpad + D-pad Left - catching reTHAWed's HUD %s", on ? "ON" : "OFF (reTHAWed draws it normally)");
    }
    toggleWas = toggle;
    const bool now = on && allowed;
    InterlockedExchange(&g_allowed, now ? 1 : 0);
    if (now != lastAllowed) {
        log("hud: catching reTHAWed's HUD: %s", now ? "yes" : "no (switched off, not in a level, or reTHAWed in front)");
        lastAllowed = now;
    }
    if (g_shotState == 2) {
        WriteTga(log);
        InterlockedExchange(&g_shotState, 0);
    } else if (g_shotState == 3) {
        log("hud: could not copy the HUD picture for saving");
        InterlockedExchange(&g_shotState, 0);
    }
    if (GetTickCount64() >= nextLog) {
        nextLog = GetTickCount64() + 10000;
        const LONG frames = InterlockedExchange(&g_frames, 0);
        const LONG caught = InterlockedExchange(&g_framesCaught, 0);
        const LONG fails = InterlockedExchange(&g_failures, 0);
        if (frames)
            log("hud: last 10 s: %ld frames, HUD caught in %ld (last one %ld draws), %ld failures (last error 0x%08lX)",
                frames, caught, static_cast<LONG>(g_lastDraws), fails, static_cast<LONG>(g_lastError));
        const LONG pubs = InterlockedExchange(&g_pubCount, 0);
        const LONG usSum = InterlockedExchange(&g_pubUsSum, 0);
        const LONG usMax = InterlockedExchange(&g_pubUsMax, 0);
        const LONG pubFails = InterlockedExchange(&g_pubFails, 0);
        if (pubs || pubFails)
            log("hud: shared with GTA: %ld pictures (%ux%u), %.2f ms each on average, %.2f ms at most, %ld failures",
                pubs, g_sw, g_sh, pubs ? usSum / 1000.0 / pubs : 0.0, usMax / 1000.0, pubFails);
        else if (g_hlError)
            log("hud: could not open the shared HUD block (Windows error %ld)", static_cast<LONG>(g_hlError));
    }
}

} // namespace mp::thaw
