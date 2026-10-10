// my-passthrough: reTHAWed frame drawing log. See drawlog.h.

#include <windows.h>
#include <d3d9.h>

#include "drawlog.h"
#include "hud.h"

#include <cstdio>
#include <cstring>

#ifndef MP_LOG_DIR
#error "MP_LOG_DIR must be set by CMakeLists.txt"
#endif

namespace mp::thaw {
namespace {

constexpr unsigned long kDeviceGlobal = 0x00852044;
// mov [0x00852044], eax  at 0x0052CDC0 (reTHAWed.exe 5.0.0.1)
const unsigned char kCheck[] = {0xA3, 0x44, 0x20, 0x85, 0x00};
constexpr unsigned long kCheckAddr = 0x0052CDC0;
constexpr int kKey = VK_F11;
constexpr int kMaxEntries = 6000;

// IDirect3DDevice9 vtable slots
enum Slot {
    S_RESET = 16, S_PRESENT = 17, S_SETRT = 37, S_BEGINSCENE = 41, S_ENDSCENE = 42, S_CLEAR = 43, S_SETTRANSFORM = 44,
    S_SETVIEWPORT = 47, S_SETRENDERSTATE = 57, S_SETTEXTURE = 65, S_DRAWPRIM = 81, S_DRAWINDEXED = 82,
    S_DRAWPRIMUP = 83, S_DRAWINDEXEDUP = 84, S_SETDECL = 87, S_SETFVF = 89, S_SETVS = 92, S_SETPS = 107,
};
// extra entry kinds for the frame log (not device functions)
constexpr unsigned char K_HUD_START = 200, K_HUD_END = 201;

struct Entry {
    unsigned char kind;
    unsigned long a, b, c, d;
};
Entry g_entries[kMaxEntries];
int g_count = 0;
volatile LONG g_state = 0; // 0 idle, 1 armed (start at next Present), 2 recording, 3 done (write)
int g_files = 0;
void** g_vtbl = nullptr;
void* g_orig[120] = {};

void Note(unsigned char kind, unsigned long a = 0, unsigned long b = 0, unsigned long c = 0, unsigned long d = 0)
{
    if (g_state != 2 || g_count >= kMaxEntries) return;
    g_entries[g_count++] = Entry{kind, a, b, c, d};
}

template <typename T>
T Orig(int slot)
{
    return reinterpret_cast<T>(g_orig[slot]);
}

// C1 (user 14:47: occasional stutter; GTA runs at 44-58 fps while reTHAWed runs
// behind it): "lean drawing". While reTHAWed is in a level and behind GTA, its
// 3D world isn't seen by anyone (only its HUD is shown, in GTA), so its world
// draws are dropped; the HUD (from the frame's first DrawIndexedPrimitiveUP on,
// 8.23) is drawn as before. reTHAWed's game, physics and animation don't
// depend on drawing. Switch: g_lean (touchpad + D-pad Right, temporary).
volatile LONG g_lean = 1;          // on by default
volatile LONG g_leanAllowed = 0;   // in a level and reTHAWed not in front (plugin loop)
bool g_hudPhase = false;           // render thread: the HUD part of this frame has begun
volatile LONG g_dropped = 0, g_drawn = 0; // draws this second (log)

bool DropThisDraw()
{
    if (g_lean && g_leanAllowed && !g_hudPhase) {
        InterlockedIncrement(&g_dropped);
        return true;
    }
    InterlockedIncrement(&g_drawn);
    return false;
}

HRESULT __stdcall HReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
{
    HudBeforeReset(); // our picture must be gone or the reset fails
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*)>(S_RESET)(dev, pp);
}
HRESULT __stdcall HPresent(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND w, const RGNDATA* r)
{
    if (HudEnd(dev)) Note(K_HUD_END);
    HudNewFrame();
    g_hudPhase = false; // C1: next frame starts with its 3D world
    if (g_state == 2) g_state = 3;          // the frame we watched is complete
    else if (g_state == 1) { g_count = 0; g_state = 2; HudRequestShot(); }
    Note(S_PRESENT);
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*)>(S_PRESENT)(dev, a, b, w, r);
}
HRESULT __stdcall HSetRT(IDirect3DDevice9* dev, DWORD i, IDirect3DSurface9* s)
{
    if (HudBeforeSetRT(dev, i)) Note(K_HUD_END);
    if (g_state == 2) {
        D3DSURFACE_DESC dsc = {};
        if (s) s->GetDesc(&dsc);
        Note(S_SETRT, i, reinterpret_cast<unsigned long>(s), dsc.Width, dsc.Height);
    }
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*)>(S_SETRT)(dev, i, s);
}
HRESULT __stdcall HBegin(IDirect3DDevice9* dev)
{
    Note(S_BEGINSCENE);
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*)>(S_BEGINSCENE)(dev);
}
HRESULT __stdcall HEnd(IDirect3DDevice9* dev)
{
    if (HudEnd(dev)) Note(K_HUD_END);
    Note(S_ENDSCENE);
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*)>(S_ENDSCENE)(dev);
}
HRESULT __stdcall HClear(IDirect3DDevice9* dev, DWORD n, const D3DRECT* r, DWORD f, D3DCOLOR c, float z, DWORD s)
{
    Note(S_CLEAR, n, f, c);
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD)>(S_CLEAR)(dev, n, r, f, c, z, s);
}
HRESULT __stdcall HSetTransform(IDirect3DDevice9* dev, D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m)
{
    // for projections note _34 (0 = orthographic, 2D) and _11
    if (g_state == 2 && m) {
        unsigned long m34, m11;
        memcpy(&m34, &m->_34, 4);
        memcpy(&m11, &m->_11, 4);
        Note(S_SETTRANSFORM, t, m34, m11);
    }
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*)>(S_SETTRANSFORM)(dev, t, m);
}
HRESULT __stdcall HSetViewport(IDirect3DDevice9* dev, const D3DVIEWPORT9* v)
{
    if (v) Note(S_SETVIEWPORT, v->X, v->Y, v->Width, v->Height);
    HudNoteViewport(v);
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, const D3DVIEWPORT9*)>(S_SETVIEWPORT)(dev, v);
}
HRESULT __stdcall HSetRS(IDirect3DDevice9* dev, D3DRENDERSTATETYPE s, DWORD v)
{
    // only the ones that tell 2D from 3D: z test / z write / alpha blend
    if (s == D3DRS_ZENABLE || s == D3DRS_ZWRITEENABLE || s == D3DRS_ALPHABLENDENABLE) Note(S_SETRENDERSTATE, s, v);
    if (HudFilterRS(s, v)) return D3D_OK; // HUD picture: the game's value is put back afterwards
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD)>(S_SETRENDERSTATE)(dev, s, v);
}
HRESULT __stdcall HSetTexture(IDirect3DDevice9* dev, DWORD st, IDirect3DBaseTexture9* t)
{
    if (st == 0) Note(S_SETTEXTURE, st, reinterpret_cast<unsigned long>(t));
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*)>(S_SETTEXTURE)(dev, st, t);
}
HRESULT __stdcall HDraw(IDirect3DDevice9* dev, D3DPRIMITIVETYPE p, UINT start, UINT n)
{
    Note(S_DRAWPRIM, p, start, n);
    if (DropThisDraw()) return D3D_OK; // C1
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT)>(S_DRAWPRIM)(dev, p, start, n);
}
HRESULT __stdcall HDrawIdx(IDirect3DDevice9* dev, D3DPRIMITIVETYPE p, INT base, UINT minI, UINT nv, UINT si, UINT n)
{
    Note(S_DRAWINDEXED, p, nv, n);
    if (DropThisDraw()) return D3D_OK; // C1
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT)>(S_DRAWINDEXED)(dev, p, base, minI, nv, si, n);
}
HRESULT __stdcall HDrawUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE p, UINT n, const void* v, UINT stride)
{
    Note(S_DRAWPRIMUP, p, n, stride);
    if (DropThisDraw()) return D3D_OK; // C1
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT)>(S_DRAWPRIMUP)(dev, p, n, v, stride);
}
HRESULT __stdcall HDrawIdxUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE p, UINT minI, UINT nv, UINT n, const void* idx,
                             D3DFORMAT fmt, const void* v, UINT stride)
{
    if (HudBeforeIdxUP(dev)) Note(K_HUD_START);
    g_hudPhase = true; // C1: the HUD part has begun: everything from here is drawn
    InterlockedIncrement(&g_drawn);
    Note(S_DRAWINDEXEDUP, p, nv, n, stride);
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT)>(
        S_DRAWINDEXEDUP)(dev, p, minI, nv, n, idx, fmt, v, stride);
}
HRESULT __stdcall HSetDecl(IDirect3DDevice9* dev, IDirect3DVertexDeclaration9* d)
{
    if (g_state == 2) {
        // note whether the declaration has a pre-transformed position (2D)
        D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1];
        UINT n = MAXD3DDECLLENGTH + 1;
        unsigned long positionT = 0, elements = 0;
        if (d && SUCCEEDED(d->GetDeclaration(el, &n))) {
            elements = n;
            for (UINT i = 0; i < n; ++i)
                if (el[i].Usage == D3DDECLUSAGE_POSITIONT) positionT = 1;
        }
        Note(S_SETDECL, reinterpret_cast<unsigned long>(d), elements, positionT);
    }
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, IDirect3DVertexDeclaration9*)>(S_SETDECL)(dev, d);
}
HRESULT __stdcall HSetFVF(IDirect3DDevice9* dev, DWORD fvf)
{
    Note(S_SETFVF, fvf);
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD)>(S_SETFVF)(dev, fvf);
}
HRESULT __stdcall HSetVS(IDirect3DDevice9* dev, IDirect3DVertexShader9* s)
{
    Note(S_SETVS, reinterpret_cast<unsigned long>(s));
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, IDirect3DVertexShader9*)>(S_SETVS)(dev, s);
}
HRESULT __stdcall HSetPS(IDirect3DDevice9* dev, IDirect3DPixelShader9* s)
{
    Note(S_SETPS, reinterpret_cast<unsigned long>(s));
    return Orig<HRESULT(__stdcall*)(IDirect3DDevice9*, IDirect3DPixelShader9*)>(S_SETPS)(dev, s);
}

struct Hook {
    int slot;
    void* fn;
};
const Hook kHooks[] = {
    {S_RESET, reinterpret_cast<void*>(&HReset)},         {S_PRESENT, reinterpret_cast<void*>(&HPresent)},     {S_SETRT, reinterpret_cast<void*>(&HSetRT)},
    {S_BEGINSCENE, reinterpret_cast<void*>(&HBegin)},    {S_ENDSCENE, reinterpret_cast<void*>(&HEnd)},
    {S_CLEAR, reinterpret_cast<void*>(&HClear)},         {S_SETTRANSFORM, reinterpret_cast<void*>(&HSetTransform)},
    {S_SETVIEWPORT, reinterpret_cast<void*>(&HSetViewport)}, {S_SETRENDERSTATE, reinterpret_cast<void*>(&HSetRS)},
    {S_SETTEXTURE, reinterpret_cast<void*>(&HSetTexture)}, {S_DRAWPRIM, reinterpret_cast<void*>(&HDraw)},
    {S_DRAWINDEXED, reinterpret_cast<void*>(&HDrawIdx)}, {S_DRAWPRIMUP, reinterpret_cast<void*>(&HDrawUP)},
    {S_DRAWINDEXEDUP, reinterpret_cast<void*>(&HDrawIdxUP)}, {S_SETDECL, reinterpret_cast<void*>(&HSetDecl)},
    {S_SETFVF, reinterpret_cast<void*>(&HSetFVF)},       {S_SETVS, reinterpret_cast<void*>(&HSetVS)},
    {S_SETPS, reinterpret_cast<void*>(&HSetPS)},
};

const char* KindName(unsigned char k)
{
    switch (k) {
    case S_RESET: return "Reset";
    case S_PRESENT: return "Present";
    case K_HUD_START: return "-- HUD picture start --";
    case K_HUD_END: return "-- HUD picture end --";
    case S_SETRT: return "SetRenderTarget";
    case S_BEGINSCENE: return "BeginScene";
    case S_ENDSCENE: return "EndScene";
    case S_CLEAR: return "Clear";
    case S_SETTRANSFORM: return "SetTransform";
    case S_SETVIEWPORT: return "SetViewport";
    case S_SETRENDERSTATE: return "SetRenderState";
    case S_SETTEXTURE: return "SetTexture0";
    case S_DRAWPRIM: return "DrawPrimitive";
    case S_DRAWINDEXED: return "DrawIndexedPrimitive";
    case S_DRAWPRIMUP: return "DrawPrimitiveUP";
    case S_DRAWINDEXEDUP: return "DrawIndexedPrimitiveUP";
    case S_SETDECL: return "SetVertexDeclaration";
    case S_SETFVF: return "SetFVF";
    case S_SETVS: return "SetVertexShader";
    case S_SETPS: return "SetPixelShader";
    }
    return "?";
}

bool Install(void (*log)(const char*, ...))
{
    for (unsigned i = 0; i < sizeof(kCheck); ++i)
        if (reinterpret_cast<const unsigned char*>(kCheckAddr)[i] != kCheck[i]) {
            log("draw log: code check at 0x%08lX DIFFERENT; not watching reTHAWed's drawing", kCheckAddr);
            return false;
        }
    IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9* volatile*>(kDeviceGlobal);
    if (!dev) return false; // not created yet; try again later
    void** vtbl = *reinterpret_cast<void***>(dev);
    DWORD old = 0;
    if (!VirtualProtect(vtbl, 120 * sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        log("draw log: could not unlock the device's function table (error %lu)", GetLastError());
        return false;
    }
    for (const Hook& h : kHooks) {
        g_orig[h.slot] = vtbl[h.slot];
        InterlockedExchangePointer(&vtbl[h.slot], h.fn);
    }
    DWORD tmp = 0;
    VirtualProtect(vtbl, 120 * sizeof(void*), old, &tmp);
    g_vtbl = vtbl;
    HudInit(g_orig);
    log("draw log: watching reTHAWed's drawing (device 0x%p, %u functions); F11 = log the next frame",
        static_cast<void*>(dev), static_cast<unsigned>(sizeof(kHooks) / sizeof(kHooks[0])));
    return true;
}

void Write(void (*log)(const char*, ...))
{
    ++g_files;
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\thaw_frame_%d.txt", L"" MP_LOG_DIR, g_files);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        log("draw log: could not write thaw_frame_%d.txt (error %lu)", g_files, GetLastError());
        return;
    }
    char line[200];
    DWORD w = 0;
    for (int i = 0; i < g_count; ++i) {
        const Entry& e = g_entries[i];
        const int n = sprintf_s(line, "%5d %-24s %08lX %08lX %08lX %08lX\r\n", i, KindName(e.kind), e.a, e.b, e.c, e.d);
        WriteFile(f, line, static_cast<DWORD>(n), &w, nullptr);
    }
    CloseHandle(f);
    log("draw log: one frame (%d calls%s) written to logs\\thaw_frame_%d.txt", g_count,
        g_count >= kMaxEntries ? ", list FULL" : "", g_files);
}

} // namespace

void DrawLogTick(void (*log)(const char*, ...), bool padCombo, bool hudAllowed, bool hudToggle, bool leanToggle)
{
    // C1: lean drawing
    {
        static bool leanWas = false;
        static LONG lastAllowed = -1;
        static ULONGLONG nextLog = 0;
        if (leanToggle && !leanWas) {
            const LONG now = InterlockedExchange(&g_lean, g_lean ? 0 : 1) ? 0 : 1;
            log("lean drawing: %s (touchpad + D-pad Right)", now ? "ON - reTHAWed's 3D world not drawn while behind GTA"
                                                                 : "OFF - reTHAWed draws everything");
        }
        leanWas = leanToggle;
        const LONG allowed = hudAllowed ? 1 : 0;
        InterlockedExchange(&g_leanAllowed, allowed);
        if (allowed != lastAllowed) {
            lastAllowed = allowed;
            if (g_lean) log("lean drawing: %s", allowed ? "active (in a level, reTHAWed behind GTA)" : "paused (menus, or reTHAWed in front)");
        }
        const ULONGLONG t = GetTickCount64();
        if (t >= nextLog) {
            nextLog = t + 10000;
            const LONG d = InterlockedExchange(&g_dropped, 0), k = InterlockedExchange(&g_drawn, 0);
            if (d || k) log("lean drawing (10 s): %ld draws dropped, %ld drawn (switch %s)", d, k, g_lean ? "on" : "off");
        }
    }
    static bool tried = false, installed = false, keyWasDown = false;
    static ULONGLONG nextTry = 0;
    if (!installed) {
        if (tried && GetTickCount64() < nextTry) return;
        tried = true;
        nextTry = GetTickCount64() + 2000;
        __try {
            installed = Install(log);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log("draw log: ERROR while installing (exception 0x%08lX)", GetExceptionCode());
            nextTry = ~0ull; // give up
        }
        return;
    }
    const bool down = (GetAsyncKeyState(kKey) & 0x8000) != 0 || padCombo;
    if (down && !keyWasDown && g_state == 0) {
        InterlockedExchange(&g_state, 1);
        log("draw log: %s - logging reTHAWed's next frame", padCombo ? "touchpad + D-pad Up" : "F11");
    }
    keyWasDown = down;
    if (g_state == 3) {
        Write(log);
        InterlockedExchange(&g_state, 0);
    }
    HudTick(log, hudAllowed, hudToggle);
}

} // namespace mp::thaw
