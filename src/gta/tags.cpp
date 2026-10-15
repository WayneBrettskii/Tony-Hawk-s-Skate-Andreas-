// SkateAndreas: Grove Street tags in GTA. See tags.h and docs/DESIGN.md 8.28 a.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CColPoint.h"
#include "CModelInfo.h"
#include "CObject.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CStreaming.h"
#include "CWorld.h"
#include "RenderWare.h"
#include "common.h"
#include <windows.h>
#ifdef GetObject
#undef GetObject // windows.h; we want CPools::GetObject
#endif

#include "tags.h"

#include <cmath>
#include <cstring>
#include <cwchar>
#include <vector>

namespace mp::gta {
namespace {

constexpr int kTagModel = 1490;                  // tag_01 (txd tags2_LAlae): GTA's collectible tag
constexpr int kKeepInMemory = 8;                 // streaming flag
constexpr int kMaxTags = 50;
constexpr unsigned char kObjectTypeGame = 1;     // normal object (type 2 = mission would be saved)

// GTA's tag functions (gta_sa.exe 1.0 US), docs/DESIGN.md 8.28 a
constexpr uintptr_t kSetupCall = 0x533DCE;       // CEntity::CreateRwObject -> tag setup
constexpr uintptr_t kSetupTag = 0x49CD50;        // void (CEntity*): alpha from GTA's tag list (crashes if not in it)
constexpr uintptr_t kSprayCall = 0x565C5E;       // spray can hits a tag -> set alpha
constexpr uintptr_t kSetTagAlpha = 0x49CEC0;     // void (CEntity*, uchar): same lookup, same crash
constexpr uintptr_t kSetAtomicAlpha = 0x49CD30;  // void (RpAtomic*, uchar): the alpha itself
constexpr uintptr_t kGetAtomicAlpha = 0x49CD40;  // ushort (RpAtomic*)
constexpr uintptr_t kTagCount = 0xA9AD70;        // int: how many tags GTA has
constexpr uintptr_t kTagList = 0xA9A8C0;         // {CEntity*, uchar alpha} x count

using SetupFn = void(__cdecl*)(CEntity*);
using SetTagAlphaFn = void(__cdecl*)(CEntity*, unsigned char);
using SetAtomicAlphaFn = void(__cdecl*)(RpAtomic*, unsigned char);

struct V3 {
    float x, y, z;
};
V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 Mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Len(V3 a) { return std::sqrt(Dot(a, a)); }
V3 Norm(V3 a)
{
    const float l = Len(a);
    return l > 1e-6f ? Mul(a, 1.0f / l) : V3{0.0f, 0.0f, 0.0f};
}
V3 FromC(const CVector& v) { return {v.x, v.y, v.z}; }
CVector ToC(V3 v) { return CVector(v.x, v.y, v.z); }

bool g_setupHooked = false, g_sprayHooked = false, g_hookLogged = false;
bool g_haveCount = false;
LONG g_lastCount = 0;
bool g_lastNow = false;
int g_refs[kMaxTags];      // pool handles of our tags, oldest first
int g_numRefs = 0;

// The tag model's own frame (from its geometry, worked out on the first tag).
bool g_modelKnown = false;
long g_ourSetups = 0;      // times GTA built one of our tags (our setup ran)
V3 g_modelNormal{0.0f, -1.0f, 0.0f}; // the side the tag is seen from
V3 g_modelUp{0.0f, 0.0f, 1.0f};
V3 g_modelCentre{0.0f, 0.0f, 0.0f};

bool InGtaTagList(CEntity* e)
{
    const int n = *reinterpret_cast<const int*>(kTagCount);
    const unsigned char* list = reinterpret_cast<const unsigned char*>(kTagList);
    for (int i = 0; i < n && i < 1000; ++i)
        if (*reinterpret_cast<CEntity* const*>(list + i * 8) == e) return true;
    return false;
}

// Replaces GTA's call at 0x533DCE (a tag object's model was just built).
void __cdecl OurTagSetup(CEntity* e)
{
    if (InGtaTagList(e)) {
        reinterpret_cast<SetupFn>(kSetupTag)(e);
        return;
    }
    ++g_ourSetups;
    if (e && e->m_pRwObject) // ours: fully sprayed Grove tag
        reinterpret_cast<SetAtomicAlphaFn>(kSetAtomicAlpha)(reinterpret_cast<RpAtomic*>(e->m_pRwObject), 255);
}

// Replaces GTA's call at 0x565C5E (the spray can hit a tag).
void __cdecl OurTagSpray(CEntity* e, unsigned char alpha)
{
    if (InGtaTagList(e)) reinterpret_cast<SetTagAlphaFn>(kSetTagAlpha)(e, alpha);
    // ours: already fully sprayed; nothing to do
}

bool CallsTo(uintptr_t site, uintptr_t target)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(site);
    return p[0] == 0xE8 && site + 5 + *reinterpret_cast<const int*>(p + 1) == target;
}

// ---- the tag model's shape, read from GTA's own gta3.img (read only) ----
// GTA keeps no readable vertices once a model is loaded (test 23:45), so we read
// tag_01.dff from the archive once and look at its vertices / normals.

bool ReadAt(HANDLE f, unsigned long long offset, void* buf, DWORD size)
{
    // 8.44: 64-bit offset (the old one went negative past 2 GB)
    LARGE_INTEGER at;
    at.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(f, at, nullptr, FILE_BEGIN)) return false;
    DWORD got = 0;
    return ReadFile(f, buf, size, &got, nullptr) && got == size;
}

// The dff's bytes, or empty. 'why' says what went wrong.
std::vector<unsigned char> ReadTagDff(const char*& why)
{
    std::vector<unsigned char> out;
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH); // ...\gta_sa.exe
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) { why = "no game folder"; return out; }
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"models\\gta3.img");
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { why = "can't open models\\gta3.img"; return out; }
    unsigned char head[8] = {};
    if (!ReadAt(f, 0, head, 8) || memcmp(head, "VER2", 4) != 0) {
        why = "gta3.img isn't the expected kind";
        CloseHandle(f);
        return out;
    }
    DWORD count = 0;
    memcpy(&count, head + 4, sizeof(count));
    // 8.44 F2b (audit): check the entry count BEFORE making the list - a damaged archive
    // used to make us reserve up to 128 GB first and only then refuse it.
    if (count == 0 || count > 100000) {
        why = "gta3.img's list has an impossible size";
        CloseHandle(f);
        return out;
    }
    std::vector<unsigned char> dir(static_cast<size_t>(count) * 32);
    if (!ReadAt(f, 8, dir.data(), static_cast<DWORD>(dir.size()))) {
        why = "can't read gta3.img's list";
        CloseHandle(f);
        return out;
    }
    for (DWORD i = 0; i < count; ++i) {
        const unsigned char* e = dir.data() + i * 32;
        char name[25] = {};
        memcpy(name, e + 8, 24);
        if (_stricmp(name, "tag_01.dff") != 0) continue;
        const DWORD sector = *reinterpret_cast<const DWORD*>(e);
        DWORD sectors = *reinterpret_cast<const unsigned short*>(e + 4);
        if (!sectors) sectors = *reinterpret_cast<const unsigned short*>(e + 6);
        if (!sectors || sectors > 512) break;
        out.resize(static_cast<size_t>(sectors) * 2048);
        if (!ReadAt(f, static_cast<unsigned long long>(sector) * 2048ull, out.data(), static_cast<DWORD>(out.size())))
            out.clear();
        break;
    }
    CloseHandle(f);
    if (out.empty()) why = "tag_01.dff not found in gta3.img";
    return out;
}

// Finds the first Geometry chunk (0x0F) inside clump (0x10) / geometry list (0x1A).
const unsigned char* FindGeometry(const unsigned char* p, const unsigned char* end, DWORD& size)
{
    while (p + 12 <= end) {
        const DWORD type = *reinterpret_cast<const DWORD*>(p);
        const DWORD sz = *reinterpret_cast<const DWORD*>(p + 4);
        const unsigned char* body = p + 12;
        if (sz > static_cast<DWORD>(end - body)) return nullptr;
        if (type == 0x0F) { size = sz; return body; }
        if (type == 0x10 || type == 0x1A) {
            const unsigned char* g = FindGeometry(body, body + sz, size);
            if (g) return g;
        }
        p = body + sz;
    }
    return nullptr;
}

void LearnModel(LogFn log)
{
    if (g_modelKnown) return;
    g_modelKnown = true; // one try only
    const char* why = "";
    const std::vector<unsigned char> dff = ReadTagDff(why);
    if (dff.empty()) {
        log("tags: %s; tag facing is a guess (-Y, up +Z)", why);
        return;
    }
    DWORD gsize = 0;
    const unsigned char* geo = FindGeometry(dff.data(), dff.data() + dff.size(), gsize);
    // its first child is the Struct (0x01) with the data
    if (!geo || gsize < 12 + 16 || *reinterpret_cast<const DWORD*>(geo) != 0x01) {
        log("tags: tag_01.dff has no geometry we can read; tag facing is a guess");
        return;
    }
    const DWORD ssize = *reinterpret_cast<const DWORD*>(geo + 4);
    const unsigned char* s = geo + 12;
    const unsigned char* send = s + ssize;
    if (send > dff.data() + dff.size()) return;
    const DWORD format = *reinterpret_cast<const DWORD*>(s);
    const DWORD nt = *reinterpret_cast<const DWORD*>(s + 4);
    const DWORD nv = *reinterpret_cast<const DWORD*>(s + 8);
    const DWORD flags = format & 0xFFFF;
    DWORD ntex = (format >> 16) & 0xFF;
    if (!ntex) ntex = (flags & 0x80) ? 2 : ((flags & 0x04) ? 1 : 0);
    if ((format & 0x01000000) || nv == 0 || nv > 10000 || nt > 20000) {
        log("tags: tag_01.dff geometry is in a form we don't read (format 0x%08lX); tag facing is a guess", format);
        return;
    }
    const unsigned char* q = s + 16;
    if (flags & 0x08) q += nv * 4;      // prelit colours
    q += ntex * nv * 8;                 // texture coordinates
    const unsigned char* tris = q;
    q += nt * 8;                        // triangles: v2, v1, material, v3
    q += 16;                            // morph target bounding sphere
    if (q + 8 > send) return;
    const DWORD hasVerts = *reinterpret_cast<const DWORD*>(q);
    const DWORD hasNormals = *reinterpret_cast<const DWORD*>(q + 4);
    q += 8;
    if (!hasVerts || q + nv * 12 > send) {
        log("tags: tag_01.dff has no vertices; tag facing is a guess");
        return;
    }
    const float* verts = reinterpret_cast<const float*>(q);
    const float* normals = (hasNormals && q + nv * 24 <= send) ? reinterpret_cast<const float*>(q + nv * 12) : nullptr;
    V3 sum{0, 0, 0}, lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (DWORD i = 0; i < nv; ++i) {
        const V3 v{verts[i * 3], verts[i * 3 + 1], verts[i * 3 + 2]};
        sum = Add(sum, v);
        lo = {std::fmin(lo.x, v.x), std::fmin(lo.y, v.y), std::fmin(lo.z, v.z)};
        hi = {std::fmax(hi.x, v.x), std::fmax(hi.y, v.y), std::fmax(hi.z, v.z)};
    }
    g_modelCentre = Mul(Add(lo, hi), 0.5f);
    V3 nN{0, 0, 0}, nT{0, 0, 0};
    if (normals)
        for (DWORD i = 0; i < nv; ++i) nN = Add(nN, V3{normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]});
    for (DWORD k = 0; k < nt; ++k) {
        const unsigned short* tr = reinterpret_cast<const unsigned short*>(tris + k * 8);
        const unsigned short i0 = tr[1], i1 = tr[0], i2 = tr[3]; // (v1, v2, v3)
        if (i0 >= nv || i1 >= nv || i2 >= nv) continue;
        const V3 a{verts[i0 * 3], verts[i0 * 3 + 1], verts[i0 * 3 + 2]};
        const V3 b{verts[i1 * 3], verts[i1 * 3 + 1], verts[i1 * 3 + 2]};
        const V3 c{verts[i2 * 3], verts[i2 * 3 + 1], verts[i2 * 3 + 2]};
        nT = Add(nT, Cross(Sub(b, a), Sub(c, a)));
    }
    const V3 n = Len(nN) > 1e-4f ? nN : nT;
    if (Len(n) >= 1e-4f) g_modelNormal = Norm(n);
    // the tag's "up": model Z for a standing tag, model Y for one lying flat
    const V3 upGuess = std::fabs(g_modelNormal.z) < 0.7f ? V3{0, 0, 1} : V3{0, 1, 0};
    g_modelUp = Norm(Sub(upGuess, Mul(g_modelNormal, Dot(upGuess, g_modelNormal))));
    const V3 tn = Norm(nT);
    log("tags: tag model from gta3.img: %lu vertices, %lu triangles, size %.2f x %.2f x %.2f m, centre (%.2f %.2f %.2f), "
        "facing (%.2f %.2f %.2f) from %s (triangles say %.2f %.2f %.2f), up (%.2f %.2f %.2f)",
        nv, nt, hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, g_modelCentre.x, g_modelCentre.y, g_modelCentre.z,
        g_modelNormal.x, g_modelNormal.y, g_modelNormal.z, Len(nN) > 1e-4f ? "normals" : "triangles", tn.x, tn.y, tn.z,
        g_modelUp.x, g_modelUp.y, g_modelUp.z);
}

// Which picture layers the tag has (the Grove one should be the one GTA fades in).
void LogLayers(CObject* o, LogFn log)
{
    static bool done = false;
    if (done) return;
    done = true;
    RpAtomic* atomic = reinterpret_cast<RpAtomic*>(o->m_pRwObject);
    RpGeometry* g = atomic ? RpAtomicGetGeometry(atomic) : nullptr;
    if (!g) {
        log("tags: built tag has no geometry to look at");
        return;
    }
    const int n = g->matList.numMaterials;
    for (int i = 0; i < n && i < 4; ++i) {
        RpMaterial* m = g->matList.materials[i];
        const char* tex = (m && m->texture) ? m->texture->name : "(none)";
        log("tags: tag layer %d: texture '%s', colour alpha %d", i, tex, m ? m->color.alpha : -1);
    }
    const unsigned short a = reinterpret_cast<unsigned short(__cdecl*)(RpAtomic*)>(kGetAtomicAlpha)(atomic);
    log("tags: tag's sprayed value right after GTA built it: %u (we want 255); our setup ran %ld time(s) so far", a,
        g_ourSetups);
}

// Place the object so the model's facing side points along worldNormal, its up
// along worldUp, and its centre sits on 'at'.
void PlaceObject(CObject* o, V3 at, V3 worldNormal, V3 worldUp)
{
    const V3 mr = Cross(g_modelUp, g_modelNormal), mu = g_modelUp, mn = g_modelNormal;
    const V3 wr = Cross(worldUp, worldNormal), wu = worldUp, wn = worldNormal;
    auto rot = [&](V3 v) { return Add(Add(Mul(wr, Dot(mr, v)), Mul(wu, Dot(mu, v))), Mul(wn, Dot(mn, v))); };
    CMatrix m;
    m.right = ToC(rot({1, 0, 0}));
    m.up = ToC(rot({0, 1, 0}));
    m.at = ToC(rot({0, 0, 1}));
    m.pos = ToC(Sub(at, rot(g_modelCentre)));
    o->SetMatrix(m);
    if (o->m_matrix) o->m_matrix->UpdateRW();
    o->UpdateRwFrame();
}

void RemoveOldest()
{
    if (g_numRefs <= 0) return;
    CObject* old = CPools::GetObject(g_refs[0]);
    if (old && old->m_nModelIndex == kTagModel) {
        CWorld::Remove(old);
        delete old;
    }
    for (int i = 1; i < g_numRefs; ++i) g_refs[i - 1] = g_refs[i];
    --g_numRefs;
}

// Forget tags GTA has thrown away (a game was loaded).
void DropGone()
{
    int k = 0;
    for (int i = 0; i < g_numRefs; ++i) {
        CObject* o = CPools::GetObject(g_refs[i]);
        if (o && o->m_nModelIndex == kTagModel) g_refs[k++] = g_refs[i];
    }
    g_numRefs = k;
}

// One Grove tag in front of / under CJ. Returns false if nothing was placed.
bool SprayTag(LogFn log, bool thawWall, const float* thawSpot, const float* thawDir)
{
    CPlayerPed* ped = FindPlayerPed();
    if (!ped || !ped->m_matrix) return false;
    const V3 pos = FromC(ped->GetPosition());
    V3 fwd = FromC(ped->GetForward());
    fwd.z = 0.0f;
    fwd = Norm(fwd);
    if (Len(fwd) < 0.5f) return false;

    V3 at{}, normal{}, up{};
    const char* where = nullptr;
    CColPoint cp;
    CEntity* hit = nullptr;
    const V3 chest = Add(pos, V3{0.0f, 0.0f, 0.35f});
    float gp[3] = {}, gd[3] = {};
    if (thawSpot && SkateModeThawToGta(thawSpot, thawDir, gp, gd)) {
        // exactly where reTHAWed put it (its bowl walls aren't in GTA's world)
        normal = Norm(V3{gd[0], gd[1], gd[2]});
        if (std::fabs(normal.z) < 0.7f) {
            up = Norm(Sub(V3{0, 0, 1}, Mul(normal, normal.z)));
            where = "wall (reTHAWed's spot)";
        } else {
            up = Norm(Sub(fwd, Mul(normal, Dot(fwd, normal))));
            where = "ground (reTHAWed's spot)";
        }
        at = Add(V3{gp[0], gp[1], gp[2]}, Mul(normal, 0.03f));
    } else if (thawWall && CWorld::ProcessLineOfSight(ToC(chest), ToC(Add(chest, Mul(fwd, 1.5f))), cp, hit, true, false, false, true,
                                   false, true, false, false) &&
        std::fabs(cp.m_vecNormal.z) < 0.5f) {
        normal = Norm(FromC(cp.m_vecNormal));
        up = Norm(Sub(V3{0, 0, 1}, Mul(normal, normal.z)));
        at = Add(FromC(cp.m_vecPoint), Mul(normal, 0.02f));
        where = "wall";
    } else {
        hit = nullptr;
        if (!CWorld::ProcessLineOfSight(ToC(pos), ToC(Add(pos, V3{0.0f, 0.0f, -2.0f})), cp, hit, true, false, false,
                                        true, false, true, false, false)) {
            log("tags: reTHAWed tag finished, but no GTA wall in front of CJ and no ground under him; no tag");
            return false;
        }
        normal = Norm(FromC(cp.m_vecNormal));
        if (normal.z < 0.5f) normal = {0, 0, 1};
        up = Norm(Sub(fwd, Mul(normal, Dot(fwd, normal))));
        at = Add(FromC(cp.m_vecPoint), Mul(normal, 0.02f));
        where = "ground";
    }

    if (CStreaming::ms_aInfoForModel[kTagModel].m_nLoadState != LOADSTATE_LOADED) {
        CStreaming::RequestModel(kTagModel, kKeepInMemory);
        CStreaming::LoadAllRequestedModels(false);
    }
    if (CStreaming::ms_aInfoForModel[kTagModel].m_nLoadState != LOADSTATE_LOADED) {
        log("tags: GTA's tag model %d did not load; no tag", kTagModel);
        return false;
    }

    DropGone();
    while (g_numRefs >= kMaxTags) RemoveOldest();

    CObject* o = CObject::Create(kTagModel);
    if (!o) {
        log("tags: GTA has no room for another object; no tag");
        return false;
    }
    o->m_nObjectType = kObjectTypeGame;
    o->bUsesCollision = false;
    o->SetIsStatic(true);
    o->m_nAreaCode = ped->m_nAreaCode;
    LearnModel(log);
    LogLayers(o, log);
    PlaceObject(o, at, normal, up);
    if (o->m_pRwObject) reinterpret_cast<SetAtomicAlphaFn>(kSetAtomicAlpha)(reinterpret_cast<RpAtomic*>(o->m_pRwObject), 255);
    CWorld::Add(o);
    g_refs[g_numRefs++] = CPools::GetObjectRef(o);
    log("tags: Grove tag on the %s at (%.2f %.2f %.2f), facing (%.2f %.2f %.2f); %d of our tags in the world", where,
        at.x, at.y, at.z, normal.x, normal.y, normal.z, g_numRefs);
    return true;
}

// Every frame: our tags stay fully sprayed (whatever GTA did to them).
void KeepSprayed()
{
    for (int i = 0; i < g_numRefs; ++i) {
        CObject* o = CPools::GetObject(g_refs[i]);
        if (o && o->m_nModelIndex == kTagModel && o->m_pRwObject)
            reinterpret_cast<SetAtomicAlphaFn>(kSetAtomicAlpha)(reinterpret_cast<RpAtomic*>(o->m_pRwObject), 255);
    }
}

void KeepSprayedSafe()
{
    __try {
        KeepSprayed();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_numRefs = 0; // forget them rather than crash
    }
}

bool SprayTagSafe(LogFn log, bool thawWall, const float* thawSpot, const float* thawDir)
{
    __try {
        return SprayTag(log, thawWall, thawSpot, thawDir);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("tags: error while placing a tag (code 0x%08lX); no tag", GetExceptionCode());
        return false;
    }
}

} // namespace

void TagsAddHook()
{
    if (CallsTo(kSetupCall, kSetupTag)) {
        plugin::patch::RedirectCall(kSetupCall, reinterpret_cast<void*>(&OurTagSetup));
        g_setupHooked = true;
    }
    if (CallsTo(kSprayCall, kSetTagAlpha)) {
        plugin::patch::RedirectCall(kSprayCall, reinterpret_cast<void*>(&OurTagSpray));
        g_sprayHooked = true;
    }
}

void TagsFrame(LinkBlock* link, LogFn log)
{
    if (!g_hookLogged) {
        g_hookLogged = true;
        if (g_setupHooked && g_sprayHooked)
            log("tags: GTA's tag calls at 0x533DCE and 0x565C5E go through us (our tags shown fully sprayed)");
        else
            log("tags: GTA's tag calls are not the original (another mod?) [setup %s, spray %s]; no tags will be placed",
                g_setupHooked ? "ok" : "different", g_sprayHooked ? "ok" : "different");
    }
    if (g_numRefs) KeepSprayedSafe();
    if (!link) return;
    const LONG v = link->reserved[5];
    if (!(v & kTagWatchOk)) {
        g_haveCount = false;
        return;
    }
    const bool now = (v & kTagNow) != 0;
    if (now != g_lastNow) {
        g_lastNow = now;
        if (SkateModeIsOn()) log("tags: reTHAWed skater %s spraying", now ? "started" : "stopped");
    }
    const LONG count = v & kTagCountMask;
    if (!g_haveCount) {
        g_haveCount = true;
        g_lastCount = count;
        return;
    }
    const LONG diff = (count - g_lastCount) & kTagCountMask;
    if (!diff) return;
    g_lastCount = count;
    if (diff > 5) return;                     // reTHAWed restarted: just catch up
    if (!SkateModeIsOn()) return;             // tags only while skating in GTA
    if (!g_setupHooked || !g_sprayHooked) return; // would crash GTA
    float spot[3] = {}, dir[3] = {};
    const bool haveSpot = (v & kTagSpotOk) != 0;
    if (haveSpot) {
        MemoryBarrier();
        for (int i = 0; i < 3; ++i) {
            spot[i] = link->thawTagPos[i];
            dir[i] = link->thawTagNormal[i];
        }
    }
    log("tags: reTHAWed tag finished (on %s there%s)", (v & kTagOnWall) ? "a wall" : "the ground",
        haveSpot ? ", spot known" : ", spot unknown");
    SprayTagSafe(log, (v & kTagOnWall) != 0, haveSpot ? spot : nullptr, haveSpot ? dir : nullptr);
}

} // namespace mp::gta
