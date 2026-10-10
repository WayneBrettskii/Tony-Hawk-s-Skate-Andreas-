// my-passthrough: GPS route to the map waypoint (docs/DESIGN.md 8.31 D5). See gps.h.
//
// Road data: GTA's own path files <GTA>\data\Paths\NODES0..63.DAT, read once
// (read only). Format (checked 2026-10-10 on the user's files with a Python
// reader: 64 files, 30,587 car nodes, every link valid):
//   header: u32 node count, car node count, ped node count, navi node count, link count
//   nodes, 28 bytes each: +8 x, y, z (int16, / 8 = metres), +16 u16 first link,
//     +24 u32 flags (bits 0-3 = number of links, bit 7 = boat road)
//   navi nodes, 14 bytes each
//   links, 4 bytes each: u16 area (file number), u16 node in that file
// Car nodes come first in each file and link only to car nodes.
//
// Route: A* along the car roads, from the road node nearest CJ to the one
// nearest the waypoint. Drawn from CJ along the route to the waypoint:
//   - radar: GTA's own radar transforms (CRadar::TransformRealWorldPointToRadarSpace,
//     TransformRadarPointToScreenSpace), clipped to the radar's circle (radius
//     1 in radar space), in the hook plugin-sdk calls drawRadarOverlayEvent
//     (after the radar map, before the blips)
//   - pause menu map: the same hook and transforms (the menu sets them up for
//     the big map; FrontEndMenuManager.m_bDrawRadarOrMap tells which)

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CMenuManager.h"
#include "CPlayerPed.h"
#include "CRadar.h"
#include "RenderWare.h"
#include "common.h"
#include <windows.h>

#include "gps.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <queue>
#include <vector>

namespace mp::gta {
namespace {

// ---- settings ----
constexpr float kOffRouteM = 40.0f;         // CJ this far from the route: work it out again
constexpr ULONGLONG kRecheckMs = 500;       // at most this often
constexpr float kArrivedM = 15.0f;          // this close (ground plan) to the waypoint: done, waypoint removed
constexpr float kLineWidthRadar = 0.0040f;  // of the screen height
constexpr float kLineWidthMap = 0.0035f;
constexpr unsigned char kR = 182, kG = 74, kB = 230, kA = 235; // GTA V's waypoint purple
constexpr float kWorldMin = -3000.0f;
constexpr int kGrid = 64;
constexpr float kCell = 6000.0f / kGrid;

struct Node {
    float x, y, z;
    int first, count; // into g_links
};
std::vector<Node> g_nodes;
std::vector<int> g_links;
std::vector<int> g_cells[kGrid * kGrid];
bool g_loadTried = false, g_loaded = false;

struct Route {
    bool valid = false;
    int handle = 0;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    std::vector<CVector> pts; // road nodes then the waypoint
    size_t progress = 0;      // the line runs CJ -> pts[progress] -> ... -> waypoint
    ULONGLONG nextCheck = 0;
};
Route g_route;
CVector g_cj;
bool g_haveCj = false;
LogFn g_log = nullptr;
unsigned g_routesFound = 0, g_routesFailed = 0, g_drawn = 0, g_drawErrors = 0;
ULONGLONG g_nextReport = 0;
float g_lastSearchMs = 0.0f;

bool ReadWholeFile(const wchar_t* path, std::vector<unsigned char>& out)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (16 << 20);
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        ok = ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size();
    }
    CloseHandle(f);
    return ok;
}

template <class T> T Get(const std::vector<unsigned char>& b, size_t at)
{
    T v;
    memcpy(&v, b.data() + at, sizeof(T));
    return v;
}

int CellOf(float x, float y)
{
    int cx = static_cast<int>((x - kWorldMin) / kCell), cy = static_cast<int>((y - kWorldMin) / kCell);
    cx = cx < 0 ? 0 : (cx >= kGrid ? kGrid - 1 : cx);
    cy = cy < 0 ? 0 : (cy >= kGrid ? kGrid - 1 : cy);
    return cy * kGrid + cx;
}

void LoadRoads(LogFn log)
{
    g_loadTried = true;
    const ULONGLONG t0 = GetTickCount64();
    wchar_t dir[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) { log("gps: could not find GTA's folder; GPS off"); return; }
    if (wchar_t* slash = wcsrchr(dir, L'\\')) slash[1] = 0;

    struct Area {
        std::vector<unsigned char> b;
        unsigned n = 0, nv = 0, nn = 0, nl = 0;
        bool ok = false;
    };
    static Area areas[64];
    int base[64] = {};
    int total = 0, files = 0;
    for (int a = 0; a < 64; ++a) {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%lsdata\\Paths\\NODES%d.DAT", dir, a);
        Area& ar = areas[a];
        if (!ReadWholeFile(path, ar.b) || ar.b.size() < 20) continue;
        ar.n = Get<unsigned>(ar.b, 0);
        ar.nv = Get<unsigned>(ar.b, 4);
        ar.nn = Get<unsigned>(ar.b, 12);
        ar.nl = Get<unsigned>(ar.b, 16);
        const size_t need = 20 + 28ull * ar.n + 14ull * ar.nn + 4ull * ar.nl;
        if (ar.nv > ar.n || ar.b.size() < need) continue;
        ar.ok = true;
        base[a] = total;
        total += static_cast<int>(ar.nv);
        ++files;
    }
    g_nodes.reserve(total);
    for (int a = 0; a < 64; ++a) {
        const Area& ar = areas[a];
        if (!ar.ok) continue;
        const size_t linkAt = 20 + 28ull * ar.n + 14ull * ar.nn;
        for (unsigned i = 0; i < ar.nv; ++i) {
            const size_t at = 20 + 28ull * i;
            Node nd;
            nd.x = Get<short>(ar.b, at + 8) / 8.0f;
            nd.y = Get<short>(ar.b, at + 10) / 8.0f;
            nd.z = Get<short>(ar.b, at + 12) / 8.0f;
            nd.first = static_cast<int>(g_links.size());
            nd.count = 0;
            const unsigned first = Get<unsigned short>(ar.b, at + 16);
            const unsigned flags = Get<unsigned>(ar.b, at + 24);
            if (!(flags & 0x80)) { // not a boat road
                for (unsigned k = 0; k < (flags & 15u); ++k) {
                    if (first + k >= ar.nl) break;
                    const unsigned la = Get<unsigned short>(ar.b, linkAt + 4ull * (first + k));
                    const unsigned ln = Get<unsigned short>(ar.b, linkAt + 4ull * (first + k) + 2);
                    if (la >= 64 || !areas[la].ok || ln >= areas[la].nv) continue;
                    if (Get<unsigned>(areas[la].b, 20 + 28ull * ln + 24) & 0x80) continue;
                    g_links.push_back(base[la] + static_cast<int>(ln));
                    ++nd.count;
                }
            }
            g_nodes.push_back(nd);
        }
    }
    for (Area& ar : areas) std::vector<unsigned char>().swap(ar.b); // free the file copies
    for (int i = 0; i < static_cast<int>(g_nodes.size()); ++i)
        if (g_nodes[i].count > 0) g_cells[CellOf(g_nodes[i].x, g_nodes[i].y)].push_back(i);
    g_loaded = !g_nodes.empty();
    log("gps: read GTA's roads: %d of 64 NODES files, %d car road points, %d links (%.0f ms)%s", files,
        static_cast<int>(g_nodes.size()), static_cast<int>(g_links.size()),
        static_cast<double>(GetTickCount64() - t0), g_loaded ? "" : "; GPS off");
}

// The road point nearest (x, y, z): heights count double, so a bridge and the
// road under it aren't mixed up.
int Nearest(float x, float y, float z)
{
    const int cx = static_cast<int>((x - kWorldMin) / kCell), cy = static_cast<int>((y - kWorldMin) / kCell);
    int best = -1;
    float bestD = 1e30f;
    for (int r = 0; r <= 4; ++r) {
        for (int gy = cy - r; gy <= cy + r; ++gy)
            for (int gx = cx - r; gx <= cx + r; ++gx) {
                if (gx < 0 || gy < 0 || gx >= kGrid || gy >= kGrid) continue;
                if (r > 0 && gx != cx - r && gx != cx + r && gy != cy - r && gy != cy + r) continue; // ring only
                for (int i : g_cells[gy * kGrid + gx]) {
                    const Node& n = g_nodes[i];
                    const float dx = n.x - x, dy = n.y - y, dz = (n.z - z) * 2.0f;
                    const float d = dx * dx + dy * dy + dz * dz;
                    if (d < bestD) { bestD = d; best = i; }
                }
            }
        if (best >= 0 && std::sqrt(bestD) < r * kCell) break; // nothing closer in the next ring
    }
    return best;
}

float Dist(const Node& a, const Node& b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool AStar(int from, int to, std::vector<int>& out)
{
    const size_t n = g_nodes.size();
    std::vector<float> cost(n, 1e30f);
    std::vector<int> came(n, -1);
    std::vector<char> done(n, 0);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    const Node& goal = g_nodes[to];
    cost[from] = 0.0f;
    open.push({Dist(g_nodes[from], goal), from});
    while (!open.empty()) {
        const int u = open.top().second;
        open.pop();
        if (done[u]) continue;
        done[u] = 1;
        if (u == to) break;
        const Node& nu = g_nodes[u];
        for (int k = 0; k < nu.count; ++k) {
            const int v = g_links[nu.first + k];
            if (done[v]) continue;
            const float c = cost[u] + Dist(nu, g_nodes[v]);
            if (c < cost[v]) {
                cost[v] = c;
                came[v] = u;
                open.push({c + Dist(g_nodes[v], goal), v});
            }
        }
    }
    if (from != to && came[to] < 0) return false;
    out.clear();
    for (int v = to; v >= 0; v = came[v]) {
        out.push_back(v);
        if (v == from) break;
    }
    for (size_t i = 0, j = out.size() - 1; i < j; ++i, --j) std::swap(out[i], out[j]);
    return true;
}

void MakeRoute(const CVector& cj, LogFn log)
{
    const ULONGLONG t0 = GetTickCount64();
    g_route.pts.clear();
    g_route.progress = 0;
    const int a = Nearest(cj.x, cj.y, cj.z);
    const int b = Nearest(g_route.tx, g_route.ty, g_route.tz);
    std::vector<int> path;
    const bool ok = a >= 0 && b >= 0 && AStar(a, b, path);
    if (ok)
        for (int i : path) g_route.pts.push_back(CVector(g_nodes[i].x, g_nodes[i].y, g_nodes[i].z));
    g_route.pts.push_back(CVector(g_route.tx, g_route.ty, g_route.tz)); // no road route: a straight line
    g_lastSearchMs = static_cast<float>(GetTickCount64() - t0);
    if (ok) ++g_routesFound; else ++g_routesFailed;
    float length = 0.0f;
    for (size_t i = 1; i < g_route.pts.size(); ++i) {
        const CVector& p = g_route.pts[i];
        const CVector& q = g_route.pts[i - 1];
        length += std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z));
    }
    log("gps: route to %.0f %.0f: %s, %u points, %.0f m along it (%.0f ms)", g_route.tx, g_route.ty,
        ok ? "along the roads" : "NO road route found, straight line", static_cast<unsigned>(g_route.pts.size()),
        length, g_lastSearchMs);
}

float DistToSegment2D(const CVector& p, const CVector& a, const CVector& b)
{
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float l2 = vx * vx + vy * vy;
    float t = l2 > 1e-6f ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float dx = a.x + vx * t - p.x, dy = a.y + vy * t - p.y;
    return std::sqrt(dx * dx + dy * dy);
}

void Frame(LogFn log)
{
    g_log = log;
    if (!g_loadTried) LoadRoads(log);
    if (!g_loaded) return;
    CPlayerPed* ped = FindPlayerPed();
    if (!ped) { g_haveCj = false; return; }
    g_cj = FindPlayerCoors(0);
    g_haveCj = true;
    const ULONGLONG now = GetTickCount64();

    const int handle = FrontEndMenuManager.m_nTargetBlipIndex;
    const int slot = handle ? CRadar::GetActualBlipArrayIndex(handle) : -1;
    if (slot < 0) {
        if (g_route.valid) log("gps: waypoint gone, route cleared");
        g_route.valid = false;
        return;
    }
    const CVector w = CRadar::ms_RadarTrace[slot].m_vecPos;
    const bool changed = !g_route.valid || handle != g_route.handle || std::fabs(w.x - g_route.tx) > 1.0f ||
                         std::fabs(w.y - g_route.ty) > 1.0f;
    if (changed) {
        g_route.valid = true;
        g_route.handle = handle;
        g_route.tx = w.x;
        g_route.ty = w.y;
        g_route.tz = w.z; // the waypoint's height isn't meaningful; Nearest() is fine with it
        g_route.nextCheck = now + kRecheckMs;
        MakeRoute(g_cj, log);
    }

    // arrived?
    const float tdx = g_cj.x - g_route.tx, tdy = g_cj.y - g_route.ty;
    if (std::sqrt(tdx * tdx + tdy * tdy) < kArrivedM) {
        log("gps: arrived at the waypoint (%.0f %.0f); waypoint removed", g_route.tx, g_route.ty);
        CRadar::ClearBlip(handle);
        FrontEndMenuManager.m_nTargetBlipIndex = 0;
        g_route.valid = false;
        return;
    }

    // how far along: the nearest piece of the next few, never going back
    const size_t n = g_route.pts.size();
    float best = 1e30f;
    size_t bestSeg = g_route.progress;
    for (size_t i = g_route.progress; i + 1 < n && i < g_route.progress + 12; ++i) {
        const float d = DistToSegment2D(g_cj, g_route.pts[i], g_route.pts[i + 1]);
        if (d < best) { best = d; bestSeg = i; }
    }
    if (bestSeg + 1 < n && best < 1e29f) g_route.progress = bestSeg + 1;
    if (n == 1) best = 0.0f; // straight line only
    if (best > kOffRouteM && now >= g_route.nextCheck) {
        g_route.nextCheck = now + kRecheckMs;
        log("gps: CJ %.0f m off the route: working it out again", best);
        MakeRoute(g_cj, log);
    }

    if (now >= g_nextReport) {
        g_nextReport = now + 10000;
        if (g_drawn || g_drawErrors)
            log("gps: last 10 s: route drawn %u times (%u errors), %u routes found, %u without a road route", g_drawn,
                g_drawErrors, g_routesFound, g_routesFailed);
        g_drawn = g_drawErrors = 0;
    }
}

// ---- drawing ----

std::vector<RwIm2DVertex> g_verts;

void AddVertex(float x, float y, float z)
{
    RwIm2DVertex v = {};
    RwIm2DVertexSetScreenX(&v, x);
    RwIm2DVertexSetScreenY(&v, y);
    RwIm2DVertexSetScreenZ(&v, z);
    RwIm2DVertexSetRecipCameraZ(&v, 1.0f);
    RwIm2DVertexSetIntRGBA(&v, kR, kG, kB, kA);
    RwIm2DVertexSetU(&v, 0.0f, 1.0f);
    RwIm2DVertexSetV(&v, 0.0f, 1.0f);
    g_verts.push_back(v);
}

// a thick line from a to b (screen pixels), as two triangles, plus a square
// at b so the corners join
void AddLine(const CVector2D& a, const CVector2D& b, float w, float z)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) return;
    const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    AddVertex(a.x + nx, a.y + ny, z); AddVertex(b.x + nx, b.y + ny, z); AddVertex(b.x - nx, b.y - ny, z);
    AddVertex(a.x + nx, a.y + ny, z); AddVertex(b.x - nx, b.y - ny, z); AddVertex(a.x - nx, a.y - ny, z);
    const float h = w * 0.5f;
    AddVertex(b.x - h, b.y - h, z); AddVertex(b.x + h, b.y - h, z); AddVertex(b.x + h, b.y + h, z);
    AddVertex(b.x - h, b.y - h, z); AddVertex(b.x + h, b.y + h, z); AddVertex(b.x - h, b.y + h, z);
}

// Clip the radar-space segment a-b to the circle of radius 1. False = all outside.
bool ClipToCircle(CVector2D& a, CVector2D& b)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float A = dx * dx + dy * dy;
    const float B = 2.0f * (a.x * dx + a.y * dy);
    const float C = a.x * a.x + a.y * a.y - 1.0f;
    if (A < 1e-9f) return C <= 0.0f;
    const float disc = B * B - 4.0f * A * C;
    if (disc <= 0.0f) return false;
    const float s = std::sqrt(disc);
    float t0 = (-B - s) / (2.0f * A), t1 = (-B + s) / (2.0f * A);
    if (t1 < 0.0f || t0 > 1.0f) return false;
    t0 = t0 < 0.0f ? 0.0f : t0;
    t1 = t1 > 1.0f ? 1.0f : t1;
    const CVector2D p(a.x + dx * t0, a.y + dy * t0), q(a.x + dx * t1, a.y + dy * t1);
    a = p;
    b = q;
    return true;
}

void Draw()
{
    if (!g_loaded || !g_route.valid || !g_haveCj || g_route.pts.empty()) return;
    const bool map = FrontEndMenuManager.m_bDrawRadarOrMap;
    const float sh = static_cast<float>(RsGlobal.maximumHeight);
    const float w = sh * (map ? kLineWidthMap : kLineWidthRadar);
    const float z = RwIm2DGetNearScreenZ();
    g_verts.clear();

    // CJ, then the route from where he is along it
    std::vector<CVector2D> world;
    world.push_back(CVector2D(g_cj.x, g_cj.y));
    for (size_t i = g_route.progress; i < g_route.pts.size(); ++i)
        world.push_back(CVector2D(g_route.pts[i].x, g_route.pts[i].y));

    CVector2D prev;
    CRadar::TransformRealWorldPointToRadarSpace(prev, world[0]);
    for (size_t i = 1; i < world.size(); ++i) {
        CVector2D cur;
        CRadar::TransformRealWorldPointToRadarSpace(cur, world[i]);
        CVector2D a = prev, b = cur;
        prev = cur;
        if (!map && !ClipToCircle(a, b)) continue;
        CVector2D sa, sb;
        CRadar::TransformRadarPointToScreenSpace(sa, a);
        CRadar::TransformRadarPointToScreenSpace(sb, b);
        AddLine(sa, sb, w, z);
    }
    if (g_verts.empty()) return;

    void *raster, *zTest, *zWrite, *vAlpha, *src, *dst, *fog, *cull;
    RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &raster);
    RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &zTest);
    RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &zWrite);
    RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &vAlpha);
    RwRenderStateGet(rwRENDERSTATESRCBLEND, &src);
    RwRenderStateGet(rwRENDERSTATEDESTBLEND, &dst);
    RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fog);
    RwRenderStateGet(rwRENDERSTATECULLMODE, &cull);

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nullptr);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, reinterpret_cast<void*>(FALSE));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, reinterpret_cast<void*>(FALSE));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, reinterpret_cast<void*>(TRUE));
    RwRenderStateSet(rwRENDERSTATESRCBLEND, reinterpret_cast<void*>(rwBLENDSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, reinterpret_cast<void*>(rwBLENDINVSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, reinterpret_cast<void*>(FALSE));
    RwRenderStateSet(rwRENDERSTATECULLMODE, reinterpret_cast<void*>(rwCULLMODECULLNONE));

    RwIm2DRenderPrimitive(rwPRIMTYPETRILIST, g_verts.data(), static_cast<RwInt32>(g_verts.size()));

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, zTest);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, zWrite);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, vAlpha);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, src);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, dst);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, fog);
    RwRenderStateSet(rwRENDERSTATECULLMODE, cull);
    ++g_drawn;
}

void DrawSafe()
{
    __try {
        // The pause menu map is drawn while the game is paused (no world
        // updates): keep the route up to date from here then, so a new
        // waypoint shows its route at once.
        if (FrontEndMenuManager.m_bDrawRadarOrMap && g_log) Frame(g_log);
        Draw();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++g_drawErrors;
    }
}

} // namespace

void GpsAddHook(LogFn log)
{
    g_log = log;
    plugin::Events::drawRadarOverlayEvent.Add([] { DrawSafe(); });
}

void GpsFrameSafe(LogFn log)
{
    __try {
        Frame(log);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_route.valid = false;
        log("gps: ERROR (exception 0x%08lX); route cleared", GetExceptionCode());
    }
}

} // namespace mp::gta
