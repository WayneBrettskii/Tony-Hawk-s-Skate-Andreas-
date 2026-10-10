// my-passthrough: reTHAWed music control for the radio. See music.h.

#include <windows.h>

#include "music.h"

namespace mp::thaw {
namespace {

constexpr unsigned long kPauseMusic = 0x004B4DE0;   // void __cdecl PauseMusic(int pause)
constexpr unsigned long kMusicPausedByte = 0x00763B72;
constexpr DWORD kStaleMs = 5000;                    // GTA's wish older than this = GTA gone (music back to normal)

struct Check {
    unsigned long address;
    unsigned char bytes[12];
    unsigned int length;
};
const Check kChecks[] = {
    {0x004B4DE0, {0x8A, 0x15, 0x94, 0xC5, 0x71, 0x00, 0x8B, 0x4C, 0x24, 0x04}, 10}, // PauseMusic start
    {0x004B4E70, {0xA0, 0x72, 0x3B, 0x76, 0x00, 0xC3}, 6},                         // MusicIsPaused
    {0x0048B2FF, {0xE8, 0xDC, 0x9A, 0x02, 0x00}, 5},                               // script PauseMusic -> 0x4B4DE0
};

volatile LONG g_ok = 0;
bool g_wePaused = false;            // game thread only
DWORD g_next = 0;                   // game thread only
volatile LONG g_pauses = 0, g_resumes = 0, g_errors = 0;
volatile LONG g_lastWish = -1;

using PauseFn = void(__cdecl*)(int);

bool MusicPaused() { return *reinterpret_cast<volatile unsigned char*>(kMusicPausedByte) != 0; }

void CallPause(int pause)
{
    __try {
        reinterpret_cast<PauseFn>(kPauseMusic)(pause);
        InterlockedIncrement(pause ? &g_pauses : &g_resumes);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&g_errors);
        InterlockedExchange(&g_ok, 0); // stop trying
    }
}

} // namespace

bool MusicCodeChecksPass(void (*log)(const char*, ...))
{
    for (const Check& c : kChecks) {
        const unsigned char* code = reinterpret_cast<const unsigned char*>(c.address);
        for (unsigned int i = 0; i < c.length; ++i)
            if (code[i] != c.bytes[i]) {
                log("music: code check 0x%08lX DIFFERENT at byte %u; GTA radio won't pause reTHAWed's music",
                    c.address, i);
                return false;
            }
    }
    InterlockedExchange(&g_ok, 1);
    log("music: code checks passed; reTHAWed's music follows GTA's radio choice");
    return true;
}

void MusicGameThreadTick(LinkBlock* link)
{
    if (!g_ok || !link) return;
    const DWORD now = GetTickCount();
    if (g_next && static_cast<LONG>(now - g_next) < 0) return; // not time yet
    g_next = now + 30;

    LONG wish = link->reserved[1];
    if (now - static_cast<DWORD>(link->reserved[2]) > kStaleMs) wish = kMusicLeave; // GTA not telling us
    InterlockedExchange(&g_lastWish, wish);

    const bool paused = MusicPaused();
    if (wish == kMusicPause) {
        if (!paused) {
            CallPause(1);
            g_wePaused = true;
        }
    } else if (g_wePaused) {
        if (paused) CallPause(0);
        g_wePaused = false;
    }
}

void MusicSecond(void (*log)(const char*, ...))
{
    const LONG p = InterlockedExchange(&g_pauses, 0), r = InterlockedExchange(&g_resumes, 0),
               e = InterlockedExchange(&g_errors, 0);
    if (p || r || e)
        log("music: paused reTHAWed's music %ld times, resumed %ld times, %ld errors (GTA wants %ld: 0 leave, 1 play, 2 pause)",
            p, r, e, static_cast<LONG>(g_lastWish));
}

} // namespace mp::thaw
