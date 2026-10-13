// SkateAndreas: the stand-in skateboard. See board.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CPlayerPed.h"
#include "CWorld.h"
#include "RenderWare.h"
#include "common.h"
#include <windows.h>

#include "board.h"
#include "recorder.h"

#include <cmath>
#include <vector>

namespace mp::gta {
namespace {

// ---- shape (metres; x = right, y = forward, z = up; 0,0,0 = ground under the middle) ----
constexpr float kDeckHalfWidth = 0.105f;   // 21 cm wide
constexpr float kDeckFlatHalf = 0.27f;     // flat middle part: 54 cm
constexpr float kDeckEnd = 0.40f;          // nose / tail tip: 80 cm long in all
constexpr float kKick = 0.055f;            // how far nose and tail kick up
constexpr float kDeckThick = 0.012f;
constexpr float kDeckBottom = kBoardLift - kDeckThick;
constexpr float kTruckY = 0.22f;           // trucks this far from the middle
constexpr float kWheelRadius = 0.027f;
constexpr float kWheelHalfWidth = 0.016f;
constexpr float kWheelX = 0.085f;          // wheel centre this far from the middle

// ---- colours (r, g, b) ----
struct Rgb { int r, g, b; };
constexpr Rgb kGrip{38, 38, 42};       // black grip tape on top
constexpr Rgb kWood{196, 150, 96};     // deck edges
constexpr Rgb kGraphic{180, 40, 36};   // deck underside: red
constexpr Rgb kMetal{170, 172, 178};   // trucks
constexpr Rgb kWheel{235, 228, 205};   // off-white wheels

// ---- following ----
constexpr float kProbe = 0.30f;        // ground checked this far ahead and behind
constexpr float kMaxSlopeStep = 0.35f; // bigger height difference = a kerb or wall: ignore
constexpr float kTiltFollow = 0.30f;   // 0..1 per frame: how fast the board tilts to the slope

// A small 3D vector of our own (independent of plugin-sdk's version).
struct V {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    V() = default;
    V(float ax, float ay, float az) : x(ax), y(ay), z(az) {}
};
V operator+(const V& a, const V& b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
V operator-(const V& a, const V& b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
V operator*(const V& a, float k) { return V(a.x * k, a.y * k, a.z * k); }

struct LocalVert {
    float x, y, z;
    RwUInt32 color;
};
std::vector<LocalVert> g_local;      // the board, built once
std::vector<RwImVertexIndex> g_index;
std::vector<RwIm3DVertex> g_world;   // the board placed in the world, each frame

struct Pose {
    bool show = false;
    V origin{0, 0, 0};
    V right{1, 0, 0}, forward{0, 1, 0}, up{0, 0, 1};
    float pitch = 0.0f;
} g;
bool g_loggedShow = false;
unsigned g_drawErrors = 0;

RwUInt32 Shade(Rgb c, float light)
{
    auto ch = [light](int v) { int o = static_cast<int>(v * light); return o < 0 ? 0 : (o > 255 ? 255 : o); };
    return RWRGBALONG(ch(c.r), ch(c.g), ch(c.b), 255);
}

// Light for a face from its direction: brightest on top, darker below.
float LightFor(float nx, float ny, float nz)
{
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len <= 0.0f) return 0.75f;
    const float d = (0.25f * nx + 0.35f * ny + 0.90f * nz) / len; // light from above, a bit ahead
    return 0.70f + 0.30f * d;
}

void AddQuad(const V& a, const V& b, const V& c, const V& d, Rgb col)
{
    const V u = b - a, v = d - a;
    const float nx = u.y * v.z - u.z * v.y, ny = u.z * v.x - u.x * v.z, nz = u.x * v.y - u.y * v.x;
    const RwUInt32 shade = Shade(col, LightFor(nx, ny, nz));
    const auto base = static_cast<RwImVertexIndex>(g_local.size());
    for (const V* p : {&a, &b, &c, &d}) g_local.push_back({p->x, p->y, p->z, shade});
    for (int i : {0, 1, 2, 0, 2, 3}) g_index.push_back(static_cast<RwImVertexIndex>(base + i));
}

// A six-sided block from 4 bottom and 4 top corners (same order around).
// top / bottom / side colours separately. Faces are drawn from both sides,
// so the corner order doesn't matter for visibility; the outward direction is
// only used for shading.
void AddBlock(const V bot[4], const V top[4], Rgb topCol, Rgb botCol, Rgb sideCol)
{
    V centre{0, 0, 0};
    for (int i = 0; i < 4; ++i) centre = centre + bot[i] + top[i];
    centre = centre * 0.125f;
    auto quad = [&](const V& a, const V& b, const V& c, const V& d, Rgb col) {
        // make the face point away from the middle (for shading)
        const V u = b - a, v = d - a;
        const V n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        const V mid = (a + b + c + d) * 0.25f;
        const V out = mid - centre;
        if (n.x * out.x + n.y * out.y + n.z * out.z < 0.0f) AddQuad(a, d, c, b, col);
        else AddQuad(a, b, c, d, col);
    };
    quad(top[0], top[1], top[2], top[3], topCol);
    quad(bot[0], bot[1], bot[2], bot[3], botCol);
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        quad(bot[i], bot[j], top[j], top[i], sideCol);
    }
}

void AddBox(float x0, float y0, float z0, float x1, float y1, float z1, Rgb col)
{
    const V bot[4] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}};
    const V top[4] = {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    AddBlock(bot, top, col, col, col);
}

// A deck piece from y0 to y1, its bottom rising from z0 to z1.
void AddDeckPiece(float y0, float z0, float y1, float z1)
{
    const float w = kDeckHalfWidth, t = kDeckThick;
    const V bot[4] = {{-w, y0, z0}, {w, y0, z0}, {w, y1, z1}, {-w, y1, z1}};
    const V top[4] = {{-w, y0, z0 + t}, {w, y0, z0 + t}, {w, y1, z1 + t}, {-w, y1, z1 + t}};
    AddBlock(bot, top, kGrip, kGraphic, kWood);
}

// A wheel: an eight-sided cylinder lying along x.
void AddWheel(float cx, float cy)
{
    constexpr int kSides = 8;
    const float cz = kWheelRadius;
    V ring0[kSides], ring1[kSides];
    for (int i = 0; i < kSides; ++i) {
        const float a = 6.2831853f * (i + 0.5f) / kSides;
        const float y = cy + kWheelRadius * std::cos(a), z = cz + kWheelRadius * std::sin(a);
        ring0[i] = V(cx - kWheelHalfWidth, y, z);
        ring1[i] = V(cx + kWheelHalfWidth, y, z);
    }
    for (int i = 0; i < kSides; ++i) {
        const int j = (i + 1) % kSides;
        // outward = away from the axle
        const V mid = (ring0[i] + ring0[j]) * 0.5f;
        const V u = ring0[j] - ring0[i], v = ring1[i] - ring0[i];
        const V n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        const float out = n.y * (mid.y - cy) + n.z * (mid.z - cz);
        if (out >= 0.0f) AddQuad(ring0[i], ring0[j], ring1[j], ring1[i], kWheel);
        else AddQuad(ring0[i], ring1[i], ring1[j], ring0[j], kWheel);
    }
    // the two flat sides, as fans of quads (pairs of triangles)
    const Rgb side{static_cast<int>(kWheel.r * 0.85f), static_cast<int>(kWheel.g * 0.85f), static_cast<int>(kWheel.b * 0.85f)};
    for (const V* ring : {ring0, ring1}) {
        const V c{ring[0].x, cy, cz};
        for (int i = 0; i < kSides; i += 2)
            AddQuad(c, ring[i], ring[(i + 1) % kSides], ring[(i + 2) % kSides], side);
    }
}

void AddTruck(float y)
{
    const float zAxle = kWheelRadius;
    // baseplate under the deck, the hanger (axle housing) below it, the axle across
    AddBox(-0.035f, y - 0.03f, kDeckBottom - 0.010f, 0.035f, y + 0.03f, kDeckBottom, kMetal);
    AddBox(-0.020f, y - 0.012f, zAxle + 0.008f, 0.020f, y + 0.012f, kDeckBottom - 0.010f, kMetal);
    AddBox(-0.075f, y - 0.010f, zAxle - 0.008f, 0.075f, y + 0.010f, zAxle + 0.012f, kMetal);
}

void BuildBoard()
{
    if (!g_local.empty()) return;
    const float zFlat = kDeckBottom;
    AddDeckPiece(-kDeckFlatHalf, zFlat, kDeckFlatHalf, zFlat);
    AddDeckPiece(kDeckFlatHalf, zFlat, kDeckEnd, zFlat + kKick);     // nose
    AddDeckPiece(-kDeckEnd, zFlat + kKick, -kDeckFlatHalf, zFlat);   // tail
    for (float y : {-kTruckY, kTruckY}) {
        AddTruck(y);
        AddWheel(-kWheelX, y);
        AddWheel(kWheelX, y);
    }
    g_world.resize(g_local.size());
}

bool GroundAt(float x, float y, float z, float& gz)
{
    bool found = false;
    gz = CWorld::FindGroundZFor3DCoord(x, y, z + 1.0f, &found, nullptr);
    return found;
}

void Draw()
{
    if (!g.show || g_local.empty()) return;
    for (size_t i = 0; i < g_local.size(); ++i) {
        const LocalVert& l = g_local[i];
        const V p = g.origin + g.right * l.x + g.forward * l.y + g.up * l.z;
        RwIm3DVertex& v = g_world[i];
        RwIm3DVertexSetPos(&v, p.x, p.y, p.z);
        v.color = l.color;
        RwIm3DVertexSetU(&v, 0.0f);
        RwIm3DVertexSetV(&v, 0.0f);
    }

    void *raster = nullptr, *zTest = nullptr, *zWrite = nullptr, *alpha = nullptr, *cull = nullptr;
    RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &raster);
    RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &zTest);
    RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &zWrite);
    RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &alpha);
    RwRenderStateGet(rwRENDERSTATECULLMODE, &cull);

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nullptr);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, reinterpret_cast<void*>(TRUE));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, reinterpret_cast<void*>(TRUE));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, reinterpret_cast<void*>(FALSE));
    RwRenderStateSet(rwRENDERSTATECULLMODE, reinterpret_cast<void*>(rwCULLMODECULLNONE));

    if (RwIm3DTransform(g_world.data(), static_cast<RwUInt32>(g_world.size()), nullptr,
                        rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA | rwIM3D_ALLOPAQUE)) {
        RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, g_index.data(), static_cast<RwInt32>(g_index.size()));
        RwIm3DEnd();
    }

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, zTest);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, zWrite);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, alpha);
    RwRenderStateSet(rwRENDERSTATECULLMODE, cull);
}

void DrawSafe()
{
    __try {
        Draw();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g.show = false; // stop drawing until skate mode moves CJ again
        ++g_drawErrors;
    }
}

} // namespace

void BoardAddHook()
{
    // Right after GTA has drawn the world (and its effects): the depth buffer
    // is still there, so the board hides correctly behind walls and CJ's legs.
    plugin::Events::renderEffectsEvent.Add([] { DrawSafe(); });
}

void BoardUpdate(CPlayerPed* ped, float x, float y, float z, bool onGround, LogFn log)
{
    if (!ped) return;
    BuildBoard();
    if (g_drawErrors) {
        log("skate board: drawing failed %u time(s); board hidden for that frame", g_drawErrors);
        g_drawErrors = 0;
    }

    // Direction: CJ's facing, flat.
    const CVector& f = ped->GetForward();
    float fx = f.x, fy = f.y;
    const float len = std::sqrt(fx * fx + fy * fy);
    if (len < 1e-4f) { fx = 0.0f; fy = 1.0f; } else { fx /= len; fy /= len; }

    // Tilt along the slope: ground height at the nose and the tail.
    float want = 0.0f;
    if (onGround) {
        float zn = 0.0f, zt = 0.0f;
        if (GroundAt(x + fx * kProbe, y + fy * kProbe, z, zn) && GroundAt(x - fx * kProbe, y - fy * kProbe, z, zt)
            && std::fabs(zn - zt) < kMaxSlopeStep) {
            want = std::atan2(zn - zt, 2.0f * kProbe);
        }
    }
    g.pitch += (want - g.pitch) * kTiltFollow;

    const float c = std::cos(g.pitch), s = std::sin(g.pitch);
    g.forward = V(fx * c, fy * c, s);
    g.up = V(-fx * s, -fy * s, c);
    g.right = V(fy, -fx, 0.0f); // forward x up, flat
    g.origin = V(x, y, z);
    if (!g.show && !g_loggedShow) {
        log("skate board: shown under CJ (%u corners, %u triangles)", static_cast<unsigned>(g_local.size()),
            static_cast<unsigned>(g_index.size() / 3));
        g_loggedShow = true;
    }
    g.show = true;
}

void BoardUpdatePose(const float origin[3], const float right[3], const float forward[3], const float up[3],
                     LogFn log)
{
    BuildBoard();
    if (g_drawErrors) {
        log("skate board: drawing failed %u time(s); board hidden for that frame", g_drawErrors);
        g_drawErrors = 0;
    }
    g.origin = V(origin[0], origin[1], origin[2]);
    RecSet3(REC_BOARD_X, origin[0], origin[1], origin[2]);
    g.right = V(right[0], right[1], right[2]);
    g.forward = V(forward[0], forward[1], forward[2]);
    g.up = V(up[0], up[1], up[2]);
    g.pitch = 0.0f;
    if (!g.show && !g_loggedShow) {
        log("skate board: shown under CJ, following reTHAWed's board (%u corners, %u triangles)",
            static_cast<unsigned>(g_local.size()), static_cast<unsigned>(g_index.size() / 3));
        g_loggedShow = true;
    }
    g.show = true;
}

void BoardHide(LogFn log)
{
    if (g.show) log("skate board: hidden");
    g.show = false;
    g.pitch = 0.0f;
    g_loggedShow = false;
}

} // namespace mp::gta
