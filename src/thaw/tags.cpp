// my-passthrough: watches reTHAWed's graffiti tags. See tags.h.
//
// Found 2026-10-09 (docs/DESIGN.md 8.28 a): holding Triangle off the board runs
// the skater's graffiti command (C code 0x005F8B20), which sets byte +0x1C of
// the graffiti component when a tag starts. The component's update
// (0x005F9B50, vtable slot 0x006EE3C8) carries it on each frame; when the tag
// is finished, 0x005F8B20 puts the tag in the world through the call at
// 0x005F92E0 (to 0x005F8680, a 12-argument thiscall; the 2nd argument is 1 for
// a wall tag, 0 for a ground tag), then clears +0x1C. (The score script call at
// 0x005F933A only runs with a combo going, so it's no good as "tag finished";
// test 23:36.)

#include <windows.h>

#include <cstring>

#include "tags.h"

namespace mp::thaw {
namespace {

constexpr unsigned long kUpdateSlot = 0x006EE3C8;  // graffiti component vtable: update
constexpr unsigned long kUpdate = 0x005F9B50;      // the original update
constexpr unsigned long kSlotBefore = 0x006EE3C4;  // the slot before it (check only)
constexpr unsigned long kSlotBeforeValue = 0x005F8AF0;
constexpr unsigned long kDoneCall = 0x005F92E0;    // call 0x005F8680 (puts the tag in the world)
constexpr unsigned long kPlaceTag = 0x005F8680;
constexpr unsigned int kTaggingByte = 0x1C;        // component: 1 while a tag is being sprayed

struct Check {
    unsigned long address;
    unsigned char bytes[16];
    unsigned int length;
    const char* what;
};
const Check kChecks[] = {
    {0x005F92D2, {0x8B, 0x4E, 0x48, 0x33, 0xD2, 0x8A, 0x56, 0x1E, 0x89, 0x64, 0x24, 0x3C, 0x52, 0x53,
                  0xE8, 0x9B}, 16, "wall flag +0x1E pushed, call 0x5F8680"},
    {0x005F92E2, {0xF3, 0xFF, 0xFF}, 3, "... call 0x5F8680 (end)"},
    {0x005F88BC, {0xC2, 0x30, 0x00}, 3, "0x5F8680 takes 12 arguments"},
    // the call's target (bytes 11-14) isn't checked: reTHAWed itself points it
    // elsewhere at start (test 23:32); we only wrap the whole update.
    {0x005F9B50, {0x56, 0x8B, 0xF1, 0x8A, 0x46, 0x1C, 0x84, 0xC0, 0x74, 0x05, 0xE8}, 11,
     "graffiti update: if +0x1C, call ..."},
    {0x005F9197, {0xC6, 0x46, 0x1C, 0x01}, 4, "tag start sets +0x1C"},
    {0x005F9356, {0xC6, 0x46, 0x1C, 0x00}, 4, "tag finish clears +0x1C"},
};

using UpdateFn = void(__thiscall*)(void*);
using PlaceTagFn = int(__thiscall*)(void*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                    DWORD, DWORD);

LinkBlock* g_link = nullptr;
volatile LONG g_ok = 0;
volatile LONG g_done = 0;            // finished tags so far
volatile LONG g_now = 0;             // 1 while spraying
volatile LONG g_onWall = 0;          // the last finished tag went on a wall (else the ground)
float g_lastVecA[4] = {}, g_lastVecB[4] = {}; // the two vectors handed to 0x005F8680 (for the log)
volatile LONG g_spotFound = 0;       // the last tag's spot / direction could be told apart
void* volatile g_tagger = nullptr;   // the component that is (or last was) spraying
volatile LONG g_lastUpdateTick = 0;
volatile LONG g_starts = 0, g_doneSecond = 0, g_updates = 0; // for the per-second log

void Publish()
{
    if (!g_link) return;
    const LONG v = (g_done & kTagCountMask) | (g_now ? kTagNow : 0) | (g_ok ? kTagWatchOk : 0) |
                   (g_onWall ? kTagOnWall : 0) |
                   (g_spotFound ? kTagSpotOk : 0);
    InterlockedExchange(&g_link->reserved[5], v);
}

// reTHAWed's game thread, once per frame per graffiti component.
void __fastcall OurUpdate(unsigned char* self, void* /*edx*/)
{
    reinterpret_cast<UpdateFn>(kUpdate)(self);
    InterlockedIncrement(&g_updates);
    InterlockedExchange(&g_lastUpdateTick, static_cast<LONG>(GetTickCount()));
    const bool spraying = self[kTaggingByte] != 0;
    if (spraying) {
        if (!g_now) InterlockedIncrement(&g_starts);
        g_tagger = self;
        InterlockedExchange(&g_now, 1);
    } else if (g_tagger == self && g_now) {
        InterlockedExchange(&g_now, 0);
    }
    Publish();
}

// reTHAWed's game thread: a tag is being put in the world. Same 12 arguments
// as 0x005F8680 (fastcall + unused edx = thiscall; we pop the 48 bytes too).
int __fastcall OurTagDone(void* self, void* /*edx*/, DWORD a1, DWORD wall, DWORD a3, DWORD a4, DWORD a5, DWORD a6,
                          DWORD a7, DWORD a8, DWORD a9, DWORD a10, DWORD a11, DWORD a12)
{
    InterlockedExchange(&g_onWall, (wall & 0xFF) ? 1 : 0);
    // a3..a6 and a7..a10 are two 4-float vectors: the tag's spot and the direction
    // it faces (length 1). Tell them apart by length; GTA gets both before the count.
    const DWORD va[4] = {a3, a4, a5, a6}, vb[4] = {a7, a8, a9, a10};
    memcpy(g_lastVecA, va, sizeof(va));
    memcpy(g_lastVecB, vb, sizeof(vb));
    auto len = [](const float* v) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; };
    const float la = len(g_lastVecA), lb = len(g_lastVecB);
    const float* spot = nullptr;
    const float* dir = nullptr;
    if (lb > 0.8f && lb < 1.2f && !(la > 0.8f && la < 1.2f)) { spot = g_lastVecA; dir = g_lastVecB; }
    else if (la > 0.8f && la < 1.2f && !(lb > 0.8f && lb < 1.2f)) { spot = g_lastVecB; dir = g_lastVecA; }
    InterlockedExchange(&g_spotFound, spot ? 1 : 0);
    if (g_link && spot) {
        for (int i = 0; i < 3; ++i) {
            g_link->thawTagPos[i] = spot[i];
            g_link->thawTagNormal[i] = dir[i];
        }
        MemoryBarrier();
    }
    InterlockedIncrement(&g_done);
    InterlockedIncrement(&g_doneSecond);
    Publish();
    return reinterpret_cast<PlaceTagFn>(kPlaceTag)(self, a1, wall, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
}

bool ChecksPass(void (*log)(const char*, ...))
{
    for (const Check& c : kChecks) {
        const unsigned char* code = reinterpret_cast<const unsigned char*>(c.address);
        for (unsigned int i = 0; i < c.length; ++i)
            if (code[i] != c.bytes[i]) {
                log("tags: code check 0x%08lX (%s) DIFFERENT at byte %u; tags won't show in GTA", c.address, c.what, i);
                return false;
            }
    }
    const unsigned long slot = *reinterpret_cast<const unsigned long*>(kUpdateSlot);
    const unsigned long before = *reinterpret_cast<const unsigned long*>(kSlotBefore);
    if (slot != kUpdate || before != kSlotBeforeValue) {
        log("tags: graffiti vtable different (0x%08lX, 0x%08lX); tags won't show in GTA", before, slot);
        return false;
    }
    return true;
}

bool WriteCode(void* where, const void* bytes, SIZE_T size)
{
    DWORD old = 0, tmp = 0;
    if (!VirtualProtect(where, size, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(where, bytes, size);
    VirtualProtect(where, size, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), where, size);
    return true;
}

} // namespace

bool TagsStart(LinkBlock* link, void (*log)(const char*, ...))
{
    g_link = link;
    if (!ChecksPass(log)) return false;

    const LONG rel = static_cast<LONG>(reinterpret_cast<unsigned long>(&OurTagDone) - (kDoneCall + 5));
    if (!WriteCode(reinterpret_cast<void*>(kDoneCall + 1), &rel, sizeof(rel))) {
        log("tags: could not change the call at 0x%08lX (error %lu); tags won't show in GTA", kDoneCall, GetLastError());
        return false;
    }
    const unsigned long update = reinterpret_cast<unsigned long>(&OurUpdate);
    if (!WriteCode(reinterpret_cast<void*>(kUpdateSlot), &update, sizeof(update))) {
        // put the call back so we don't count without the other half
        const LONG orig = static_cast<LONG>(kPlaceTag - (kDoneCall + 5));
        WriteCode(reinterpret_cast<void*>(kDoneCall + 1), &orig, sizeof(orig));
        log("tags: could not change the graffiti vtable (error %lu); tags won't show in GTA", GetLastError());
        return false;
    }
    InterlockedExchange(&g_ok, 1);
    Publish();
    const unsigned long callAt = 0x005F9B5A;
    log("tags: graffiti update calls 0x%08lX (original 0x005F8B20)",
        callAt + 5 + *reinterpret_cast<const unsigned long*>(callAt + 1));
    log("tags: code checks passed; watching reTHAWed's graffiti tags (update hook 0x%08lX, tag-placing call 0x%08lX)",
        kUpdateSlot, kDoneCall);
    return true;
}

void TagsSecond(void (*log)(const char*, ...))
{
    if (!g_ok) return;
    // reTHAWed stopped updating graffiti (left the level mid-tag): not spraying any more
    if (g_now && GetTickCount() - static_cast<DWORD>(g_lastUpdateTick) > 500) {
        InterlockedExchange(&g_now, 0);
        Publish();
    }
    const LONG s = InterlockedExchange(&g_starts, 0), d = InterlockedExchange(&g_doneSecond, 0);
    const LONG u = InterlockedExchange(&g_updates, 0);
    if (s || d)
        log("tags: %ld tag(s) started, %ld finished in the last second (%ld finished so far, spraying now: %s, %ld graffiti updates)",
            s, d, static_cast<LONG>(g_done), g_now ? "yes" : "no", u);
    if (d)
        log("tags: last tag went on %s in reTHAWed; vectors (%.2f %.2f %.2f %.2f) (%.2f %.2f %.2f %.2f) -> spot/direction %s",
            g_onWall ? "a wall" : "the ground", g_lastVecA[0], g_lastVecA[1], g_lastVecA[2], g_lastVecA[3],
            g_lastVecB[0], g_lastVecB[1], g_lastVecB[2], g_lastVecB[3], g_spotFound ? "found" : "NOT told apart");
}

} // namespace mp::thaw
