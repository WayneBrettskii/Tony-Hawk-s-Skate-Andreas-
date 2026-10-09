// my-passthrough: CJ's skating poses. See skateanim.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CAnimManager.h"
#include "CPedIntelligence.h"
#include "CPlayerPed.h"
#include "CStreaming.h"
#include "CTaskManager.h"
#include "CTaskSimpleRunNamedAnim.h"
#include <windows.h>

#include "skateanim.h"

namespace mp::gta {
namespace {

// ---- settings (change and rebuild) ----
constexpr float kPushSpeed = 1.0f;      // m/s: slower = standing pose
constexpr float kFastSpeed = 7.0f;      // m/s: faster = sprint pose
constexpr float kAirHeight = 0.8f;      // m above GTA's ground = in the air
constexpr ULONGLONG kMinPoseMs = 250;   // keep a ground pose at least this long (no flicker)
constexpr float kBlend = 4.0f;          // how fast poses blend (GTA's usual value)
constexpr int kIfpFirstId = 25575;      // GTA SA 1.0: streaming id of the first animation file
constexpr int kKeepInMemory = 8;        // streaming flag: don't throw it out
// looped, started; root movement taken out (we place CJ ourselves every frame)
constexpr int kAnimFlags = ANIMATION_STARTED | ANIMATION_LOOPED | ANIMATION_TRANLSATE_X | ANIMATION_TRANLSATE_Y;

enum class Pose { None, Idle, Push, Fast, Air };

struct Pick {
    const char* anim;
    const char* file;
};

Pick PoseAnim(Pose p)
{
    switch (p) {
    case Pose::Idle: return {"skate_idle", "skate"};
    case Pose::Push: return {"skate_run", "skate"};
    case Pose::Fast: return {"skate_sprint", "skate"};
    case Pose::Air: return {"JUMP_glide", "ped"};
    default: return {nullptr, nullptr};
    }
}

struct State {
    int block = -1;          // "skate" animation file
    bool ref = false;        // we hold a reference on it
    bool failed = false;     // couldn't load it: don't keep trying every frame
    Pose pose = Pose::None;
    CTask* mine = nullptr;   // our task in CJ's task slot (to fade it out when we stop)
    ULONGLONG poseSince = 0;
    unsigned changes = 0;
};
State a;

bool EnsureSkateFile(LogFn log)
{
    if (a.failed) return false;
    if (a.ref) return true;
    if (a.block < 0) a.block = CAnimManager::GetAnimationBlockIndex("skate");
    if (a.block < 0) {
        log("skate anim: GTA has no animation file \"skate\"; CJ keeps his normal animations");
        a.failed = true;
        return false;
    }
    CAnimBlock& b = CAnimManager::ms_aAnimBlocks[a.block];
    if (!b.bLoaded) {
        CStreaming::RequestModel(a.block + kIfpFirstId, kKeepInMemory);
        CStreaming::LoadAllRequestedModels(false);
    }
    if (!b.bLoaded) {
        log("skate anim: animation file \"skate\" (block %d) did not load; CJ keeps his normal animations", a.block);
        a.failed = true;
        return false;
    }
    if (!a.ref) {
        CAnimManager::AddAnimBlockRef(a.block);
        a.ref = true;
        log("skate anim: animation file \"skate\" loaded (block %d, %d animations)", a.block, b.animationCount);
    }
    return true;
}

// If our task is still in CJ's slot: let its animation fade out (GTA would
// otherwise keep a looped animation playing after the task is deleted).
void FadeOutMine(CPlayerPed* ped)
{
    if (!a.mine || !ped || !ped->m_pIntelligence) return;
    CTask* now = ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[TASK_PRIMARY_PRIMARY];
    if (now == a.mine) now->MakeAbortable(ped, ABORT_PRIORITY_URGENT, nullptr);
}

void Play(CPlayerPed* ped, Pose p, LogFn log)
{
    const Pick k = PoseAnim(p);
    if (!k.anim || !ped->m_pIntelligence) return;
    auto* task = new CTaskSimpleRunNamedAnim(k.anim, k.file, kAnimFlags, kBlend, -1, true, false, false, false);
    FadeOutMine(ped);
    ped->m_pIntelligence->m_TaskMgr.SetTask(task, TASK_PRIMARY_PRIMARY, false);
    a.mine = task;
    ++a.changes;
    if (a.changes <= 20 || a.changes % 100 == 0) log("skate anim: CJ now %s (%s/%s), change %u",
        p == Pose::Idle ? "standing" : p == Pose::Push ? "pushing" : p == Pose::Fast ? "going fast" : "in the air",
        k.file, k.anim, a.changes);
}

} // namespace

void SkateAnimUpdate(CPlayerPed* ped, float speed, float height, LogFn log)
{
    if (!ped || !EnsureSkateFile(log)) return;
    Pose want;
    if (height > kAirHeight) want = Pose::Air;
    else if (speed < kPushSpeed) want = Pose::Idle;
    else if (speed < kFastSpeed) want = Pose::Push;
    else want = Pose::Fast;
    if (want == a.pose) return;
    const ULONGLONG now = GetTickCount64();
    const bool airChange = want == Pose::Air || a.pose == Pose::Air || a.pose == Pose::None;
    if (!airChange && now - a.poseSince < kMinPoseMs) return;
    Play(ped, want, log);
    a.pose = want;
    a.poseSince = now;
}

void SkateAnimStop(CPlayerPed* ped, LogFn log)
{
    if (a.pose != Pose::None && ped && ped->m_pIntelligence) {
        FadeOutMine(ped);
        if (ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[TASK_PRIMARY_PRIMARY] == a.mine)
            ped->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);
        log("skate anim: CJ back to his normal animations (%u pose changes)", a.changes);
    }
    a.pose = Pose::None;
    a.mine = nullptr;
    a.changes = 0;
    if (a.ref) {
        CAnimManager::RemoveAnimBlockRef(a.block);
        a.ref = false;
    }
}

} // namespace mp::gta
