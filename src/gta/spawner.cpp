// SkateAndreas: vehicle spawner. See spawner.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CCheat.h"
#include "CFont.h"
#include "CModelInfo.h"
#include "CPad.h"
#include "CSprite2d.h"
#include "CText.h"
#include "CVehicleModelInfo.h"
#include "RenderWare.h"
#include <windows.h>

#include "spawner.h"
#include "arsenal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mp::gta {
namespace {

constexpr int kKey = VK_F6;
constexpr int kFirstVehicle = 400, kLastVehicle = 611;
constexpr int kBmx = 481;
const int kTrains[] = {449, 537, 538, 569, 570, 590}; // tram, trains, carriages: rails only
constexpr int kRows = 14;              // rows on screen
constexpr DWORD kRepeatDelayMs = 350, kRepeatMs = 70;

struct Entry {
    int model;
    std::string name;
};
std::vector<Entry> g_list;
bool g_open = false;
int g_sel = 0, g_top = 0;
int g_spawnRequest = -1;   // model to spawn on the next world update
bool g_guard = false;      // after closing: keep the pad from GTA until buttons are let go
bool g_keyWas = false;

// pad state (read in the pad filter)
bool g_crossWas = false, g_circleWas = false;
int g_dirWas = 0;          // -1 up, +1 down, 0 none
DWORD g_dirSince = 0, g_lastRepeat = 0;

bool GtaInFront()
{
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

bool IsTrain(int m)
{
    for (int t : kTrains)
        if (t == m) return true;
    return false;
}

void NameOfRaw(int model, char* buf, size_t size)
{
    char key[9] = "";
    __try {
        CBaseModelInfo* mi = CModelInfo::GetModelInfo(model);
        if (mi) memcpy(key, reinterpret_cast<CVehicleModelInfo*>(mi)->m_szGameName, 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        key[0] = 0;
    }
    key[8] = 0;
    const char* text = nullptr;
    if (key[0]) {
        __try {
            text = TheText.Get(key);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            text = nullptr;
        }
    }
    if (text && text[0]) sprintf_s(buf, size, "%s", text);
    else if (key[0]) sprintf_s(buf, size, "%s", key);
    else sprintf_s(buf, size, "Vehicle %d", model);
}

std::string NameOf(int model)
{
    char buf[64];
    NameOfRaw(model, buf, sizeof(buf));
    return buf;
}

bool HasModel(int model)
{
    __try {
        return CModelInfo::GetModelInfo(model) != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void BuildList()
{
    if (!g_list.empty()) return;
    std::vector<Entry> rest;
    for (int m = kFirstVehicle; m <= kLastVehicle; ++m) {
        if (IsTrain(m) || !HasModel(m)) continue;
        Entry e{m, NameOf(m)};
        if (m == kBmx) g_list.push_back(e);
        else rest.push_back(e);
    }
    std::sort(rest.begin(), rest.end(), [](const Entry& a, const Entry& b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
    g_list.insert(g_list.end(), rest.begin(), rest.end());
}

void Move(int step)
{
    if (g_list.empty()) return;
    const int n = static_cast<int>(g_list.size());
    g_sel = (g_sel + step + n) % n;
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + kRows) g_top = g_sel - kRows + 1;
}

void Close()
{
    g_open = false;
    g_guard = true;
}

CVehicle* SpawnUnsafe(int model) { return CCheat::VehicleCheat(model); }

bool Spawn(int model)
{
    __try {
        return SpawnUnsafe(model) != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Text(float x, float y, float scale, const char* text, CRGBA color, eFontAlignment align)
{
    const float sw = static_cast<float>(RsGlobal.maximumWidth), sh = static_cast<float>(RsGlobal.maximumHeight);
    CFont::SetBackground(false, false);
    CFont::SetProportional(true);
    CFont::SetJustify(false);
    CFont::SetOrientation(align);
    CFont::SetWrapx(sw);
    CFont::SetCentreSize(sw);
    CFont::SetFontStyle(FONT_MENU);
    CFont::SetScale(sw / 640.0f * 0.45f * scale, sh / 448.0f * 0.95f * scale);
    CFont::SetColor(color);
    CFont::SetEdge(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::PrintString(x, y, text);
}

void Draw()
{
    if (!g_open) return;
    const float sw = static_cast<float>(RsGlobal.maximumWidth), sh = static_cast<float>(RsGlobal.maximumHeight);
    const float x0 = sw * 0.05f, x1 = sw * 0.42f, y0 = sh * 0.12f;
    const float row = sh * 0.045f;
    const float y1 = y0 + row * (kRows + 3.2f);
    CSprite2d::DrawRect(CRect(x0, y0, x1, y1), CRGBA(0, 0, 0, 180));
    Text(x0 + sw * 0.015f, y0 + row * 0.3f, 1.2f, "Vehicle spawner", CRGBA(225, 172, 61, 255), ALIGN_LEFT);
    const int n = static_cast<int>(g_list.size());
    for (int i = 0; i < kRows && g_top + i < n; ++i) {
        const int k = g_top + i;
        const float y = y0 + row * (1.6f + i);
        if (k == g_sel) CSprite2d::DrawRect(CRect(x0 + sw * 0.008f, y - row * 0.1f, x1 - sw * 0.008f, y + row * 0.9f), CRGBA(225, 172, 61, 200));
        const CRGBA c = k == g_sel ? CRGBA(0, 0, 0, 255) : CRGBA(230, 230, 230, 255);
        Text(x0 + sw * 0.02f, y, 1.0f, g_list[k].name.c_str(), c, ALIGN_LEFT);
    }
    char pos[32];
    sprintf_s(pos, "%d / %d", g_sel + 1, n);
    Text(x1 - sw * 0.015f, y0 + row * 0.3f, 0.9f, pos, CRGBA(170, 170, 170, 255), ALIGN_RIGHT);
    Text(x0 + sw * 0.015f, y1 - row * 1.3f, 0.85f, "Cross: spawn   Circle / F6: close", CRGBA(170, 170, 170, 255), ALIGN_LEFT);
}

void DrawSafe()
{
    __try {
        Draw();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_open = false;
    }
}

} // namespace

bool SpawnerPadFilter()
{
    CPad* pad = CPad::GetPad(0);
    if (!pad) return g_open;
    const CControllerState& s = pad->NewState;
    const bool cross = s.ButtonCross != 0, circle = s.ButtonCircle != 0;
    const bool up = s.DPadUp != 0 || s.LeftStickY < -80, down = s.DPadDown != 0 || s.LeftStickY > 80;
    if (!g_open) {
        if (g_guard && !cross && !circle && !up && !down) g_guard = false;
        g_crossWas = cross;
        g_circleWas = circle;
        g_dirWas = 0;
        return g_guard; // keep the pad from GTA until the buttons are let go
    }
    const DWORD now = GetTickCount();
    const bool upKey = (GetAsyncKeyState(VK_UP) & 0x8000) != 0, downKey = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    const bool enterKey = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    const int dir = (up || upKey) ? -1 : (down || downKey) ? 1 : 0;
    if (dir != g_dirWas) {
        if (dir) Move(dir);
        g_dirSince = g_lastRepeat = now;
    } else if (dir && now - g_dirSince >= kRepeatDelayMs && now - g_lastRepeat >= kRepeatMs) {
        Move(dir);
        g_lastRepeat = now;
    }
    g_dirWas = dir;
    if (((cross && !g_crossWas) || enterKey) && !g_list.empty() && g_spawnRequest < 0) {
        g_spawnRequest = g_list[g_sel].model;
        Close();
    } else if (circle && !g_circleWas) {
        Close();
    }
    g_crossWas = cross;
    g_circleWas = circle;
    return true; // GTA doesn't get the controller while the list is open
}

bool SpawnerIsOpen() { return g_open; }

void SpawnerFrame(LogFn log)
{
    const bool key = (GetAsyncKeyState(kKey) & 0x8000) != 0;
    if (key && !g_keyWas && GtaInFront() && !ArsenalBlocksInput()) {
        if (g_open) {
            Close();
        } else if (SkateModeIsOn()) {
            if (log) log("spawner: not while skating (turn skate mode off first)");
        } else {
            BuildList();
            g_open = !g_list.empty();
            if (log) log("spawner: open (%u vehicles)", static_cast<unsigned>(g_list.size()));
        }
    }
    g_keyWas = key;
    if (g_open && SkateModeIsOn()) Close();
    if (g_spawnRequest >= 0) {
        const int model = g_spawnRequest;
        g_spawnRequest = -1;
        const bool ok = Spawn(model);
        if (log) log("spawner: %s (model %d) %s", NameOf(model).c_str(), model, ok ? "spawned" : "could NOT be spawned");
    }
}

void SpawnerAddHook()
{
    plugin::Events::drawHudEvent.Add([] { DrawSafe(); });
}

} // namespace mp::gta
