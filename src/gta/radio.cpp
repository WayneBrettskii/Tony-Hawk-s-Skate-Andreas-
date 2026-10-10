// my-passthrough: radio while skating. See radio.h.

#include "plugin.h" // plugin-sdk; must come before windows.h
#include "CAudioEngine.h"
#include "CAERadioTrackManager.h"
#include "CFont.h"
#include "RenderWare.h"
#include <windows.h>

#include "radio.h"

#include <cstdio>
#include <intrin.h>

namespace mp::gta {
namespace {

constexpr DWORD kHoldMs = 400;       // Back held this long = radio, not reTHAWed
constexpr int kTapFrames = 4;        // a tap reaches reTHAWed as this many frames of Back
constexpr DWORD kShowMs = 2500;      // station name on screen
constexpr int kFirstGta = 1, kLastGta = 11; // GTA station ids (Playback FM .. WCTR; to be checked by the log)
constexpr int kThaw = -2, kOff = -1;

// pad filter state (GTA main thread)
bool g_backDown = false, g_backFired = false;
DWORD g_backStart = 0;
int g_tapFrames = 0;
int g_holdEvents = 0;

// station state
// Fix 1 (test 20:45): GTA's interior-radio code (CAEAmbienceTrackManager,
// 0x4D6E60) stops any radio every frame while CJ is outside a club or house:
// three calls to AudioEngine.StopRadio (0x506F70). We point those three calls
// at ours, which skips the stop while our skate-mode GTA station plays.
const uintptr_t kAmbienceStopCalls[3] = {0x4D71D2, 0x4D71FD, 0x4D7221};
constexpr uintptr_t kStopRadio = 0x506F70;
bool g_ownRadio = false;      // a GTA station is playing for skate mode
bool g_patchTried = false, g_patched = false;
unsigned g_stopsSkipped = 0;

void __fastcall AmbienceStopRadio(CAudioEngine* self, void*, tVehicleAudioSettings* settings, bool arg)
{
    if (g_ownRadio) {
        ++g_stopsSkipped;
        return;
    }
    self->StopRadio(settings, arg); // GTA's own (0x506F70)
}

// Fix 2 (diagnostics 21:01): the stop that kills our station comes from
// 0x4EA5D2, in GTA's radio code (0x4EA590): when CJ has no vehicle radio it
// asks "is an interior radio playing?" (AudioEngine at 0x507280 ->
// AEAmbienceTrackManager 0x4D6D40) and stops the radio if not. We answer
// "yes" while our skate-mode station plays; otherwise GTA's own answer.
constexpr uintptr_t kAmbienceRadioQuestion = 0x4EA5C3;  // call 0x507280
constexpr uintptr_t kIsAmbienceRadioActive = 0x507280;
bool g_questionPatched = false;
unsigned g_questionsAnswered = 0;

bool __fastcall OurIsAmbienceRadioActive(CAudioEngine* self, void*)
{
    if (g_ownRadio) {
        ++g_questionsAnswered;
        return true;
    }
    return plugin::CallMethodAndReturn<bool, kIsAmbienceRadioActive, CAudioEngine*>(self);
}

void PatchAmbience(LogFn log)
{
    if (g_patchTried) return;
    g_patchTried = true;
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(kAmbienceRadioQuestion);
        const uintptr_t target = kAmbienceRadioQuestion + 5 + *reinterpret_cast<const int*>(p + 1);
        if (p[0] == 0xE8 && target == kIsAmbienceRadioActive) {
            plugin::patch::RedirectCall(kAmbienceRadioQuestion, reinterpret_cast<void*>(&OurIsAmbienceRadioActive));
            g_questionPatched = true;
            if (log) log("radio: GTA now treats our skate-mode station like an interior radio (call at 0x4EA5C3 redirected)");
        } else if (log) {
            log("radio: call at 0x4EA5C3 is not GTA's original (another mod?); GTA stations will stop on foot");
        }
    }
    for (uintptr_t a : kAmbienceStopCalls) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(a);
        const uintptr_t target = a + 5 + *reinterpret_cast<const int*>(p + 1);
        if (p[0] != 0xE8 || target != kStopRadio) {
            if (log) log("radio: call at 0x%08X is not GTA's StopRadio (another mod?); GTA stations may stop on foot", static_cast<unsigned>(a));
            return;
        }
    }
    for (uintptr_t a : kAmbienceStopCalls) plugin::patch::RedirectCall(a, reinterpret_cast<void*>(&AmbienceStopRadio));
    g_patched = true;
    if (log) log("radio: GTA's interior-radio code no longer stops our station while skating (3 calls redirected)");
}

// ---- Diagnostics (STATUS.md, 20:58): who stops GTA's radio, and its state ----
// Every other call of GTA's two stop functions goes through a counter that
// notes the calling address and then calls GTA's own function as before.
// Behaviour is unchanged; this only records. Temporary.
const uintptr_t kTrackStopSites[] = {0x4EA5D2, 0x4EB7DF, 0x4EB835, 0x4EBE2F, 0x507AB4, 0x507CCA, 0x507E5B, 0x507F6B};
constexpr uintptr_t kTrackStop = 0x4E9820; // CAERadioTrackManager::StopRadio
const uintptr_t kEngineStopSites[] = {0x4EB941, 0x4F5B83, 0x4F5BD3, 0x4FBA3E, 0x4FD0EA, 0x5737A5, 0x576C55};
struct StopCount {
    uintptr_t site;
    unsigned count;
};
StopCount g_stops[24];
int g_stopKinds = 0;
unsigned g_diagSecond = 0;

void NoteStop(uintptr_t site)
{
    for (int i = 0; i < g_stopKinds; ++i)
        if (g_stops[i].site == site) { ++g_stops[i].count; return; }
    if (g_stopKinds < 24) g_stops[g_stopKinds++] = {site, 1};
}

void __fastcall DiagTrackStop(CAERadioTrackManager* self, void*, tVehicleAudioSettings* settings, char arg)
{
    NoteStop(reinterpret_cast<uintptr_t>(_ReturnAddress()) - 5);
    plugin::CallMethod<0x4E9820, CAERadioTrackManager*, tVehicleAudioSettings*, char>(self, settings, arg);
}

void __fastcall DiagEngineStop(CAudioEngine* self, void*, tVehicleAudioSettings* settings, bool arg)
{
    NoteStop(reinterpret_cast<uintptr_t>(_ReturnAddress()) - 5);
    self->StopRadio(settings, arg);
}

bool CallsTo(uintptr_t site, uintptr_t target)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(site);
    return p[0] == 0xE8 && site + 5 + *reinterpret_cast<const int*>(p + 1) == target;
}

void PatchDiag(LogFn log)
{
    static bool tried = false;
    if (tried) return;
    tried = true;
    int done = 0, other = 0;
    for (uintptr_t a : kTrackStopSites) {
        if (CallsTo(a, kTrackStop)) { plugin::patch::RedirectCall(a, reinterpret_cast<void*>(&DiagTrackStop)); ++done; }
        else ++other;
    }
    for (uintptr_t a : kEngineStopSites) {
        if (CallsTo(a, 0x506F70)) { plugin::patch::RedirectCall(a, reinterpret_cast<void*>(&DiagEngineStop)); ++done; }
        else ++other;
    }
    if (log) log("radio diag: counting %d stop calls (%d were not GTA's original call and are left alone)", done, other);
}

// GTA's radio state, as text (all reads; see STATUS.md for what each is).
void DiagState(char* out, size_t size)
{
    const unsigned char* rm = reinterpret_cast<const unsigned char*>(&AERadioTrackManager);
    const int status = *reinterpret_cast<const int*>(rm + 0x68);
    const int cutsceneState = *reinterpret_cast<const int*>(0x8AE554 + 0x8);
    const unsigned char f48 = *reinterpret_cast<const unsigned char*>(0xB7CB48);
    const unsigned char f49 = *reinterpret_cast<const unsigned char*>(0xB7CB49);
    bool radioOn = false, ambience = false;
    __try {
        radioOn = AudioEngine.IsRadioOn();
        ambience = AudioEngine.IsAmbienceTrackActive();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    sprintf_s(out, size,
              "radio on %d | manager status %d, flags %02X %02X %02X %02X %02X %02X %02X %02X, retunes %d/%d, "
              "station byte +0xE9 %d | ambience track %d | cutscene-track state %d | B7CB48 %u B7CB49 %u",
              radioOn, status, rm[0], rm[1], rm[2], rm[3], rm[4], rm[5], rm[6], rm[7],
              *reinterpret_cast<const int*>(rm + 0x6C), *reinterpret_cast<const int*>(rm + 0x70),
              static_cast<signed char>(rm[0xE9]), ambience, cutsceneState, f48, f49);
}

void DiagLogState(LogFn log, const char* when)
{
    if (!log) return;
    char st[400];
    __try {
        DiagState(st, sizeof(st));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sprintf_s(st, "(could not read)");
    }
    log("radio diag %s: %s", when, st);
}

void DiagLogStops(LogFn log)
{
    if (!log || !g_stopKinds) return;
    char line[600] = "radio diag: stop calls since last line:";
    for (int i = 0; i < g_stopKinds; ++i) {
        char part[40];
        sprintf_s(part, " 0x%06X x%u", static_cast<unsigned>(g_stops[i].site), g_stops[i].count);
        strcat_s(line, part);
    }
    char part[40];
    sprintf_s(part, " | interior stops skipped %u, questions answered %u", g_stopsSkipped, g_questionsAnswered);
    strcat_s(line, part);
    log("%s", line);
    g_stopKinds = 0;
}

int g_list[2 + (kLastGta - kFirstGta + 1)];
int g_count = 0;
int g_sel = 0;           // index in g_list; 0 = reTHAWed
int g_playingGta = -1;   // GTA station we started, -1 none
bool g_skateWas = false;
DWORD g_showUntil = 0;
char g_name[64] = "";
bool g_loggedNames = false;
DWORD g_nextCheck = 0;
int g_restarts = 0;          // restarts of the current station (max 1; each one loads the station = a hitch)

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
    DiagLogState(log, "before StartRadio");
    __try {
        g_ownRadio = true;
        AudioEngine.StartRadio(id, 0);
        g_playingGta = id;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (log) log("radio: ERROR starting GTA station %d", id);
        g_playingGta = -1;
        g_ownRadio = false;
    }
    DiagLogState(log, "right after StartRadio");
}

void Tell(LinkBlock* link, LONG wish)
{
    if (!link) return;
    InterlockedExchange(&link->reserved[1], wish);
    InterlockedExchange(&link->reserved[2], static_cast<LONG>(GetTickCount()));
}

void DrawName()
{
    if (!g_skateWas || GetTickCount() >= g_showUntil || !g_name[0]) return;
    const float sw = static_cast<float>(RsGlobal.maximumWidth), sh = static_cast<float>(RsGlobal.maximumHeight);
    CFont::SetBackground(false, false);
    CFont::SetProportional(true);
    CFont::SetJustify(false);
    CFont::SetOrientation(ALIGN_CENTER);
    CFont::SetCentreSize(sw);
    CFont::SetFontStyle(FONT_MENU);
    CFont::SetScale(sw / 640.0f * 0.6f, sh / 448.0f * 1.2f);
    CFont::SetColor(CRGBA(225, 172, 61, 255)); // gold, like GTA's radio names
    CFont::SetEdge(2);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::PrintString(sw * 0.5f, sh * 22.0f / 448.0f, g_name);
}

void DrawNameSafe()
{
    __try {
        DrawName();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_showUntil = 0;
    }
}

} // namespace

void RadioFilterPad(LONG& buttons)
{
    if (!SkateModeIsOn()) {
        g_backDown = g_backFired = false;
        g_tapFrames = 0;
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
            ++g_holdEvents;
        }
    } else if (g_backDown) {
        g_backDown = false;
        if (!g_backFired) g_tapFrames = kTapFrames; // a tap: pass it on now
    }
    if (g_tapFrames > 0) {
        buttons |= kBtnSelect;
        --g_tapFrames;
    }
}

void RadioFrame(LinkBlock* link, LogFn log)
{
    BuildList();
    PatchAmbience(log);
    PatchDiag(log);
    const bool on = SkateModeIsOn();
    if (!on) {
        // user 20:35: reTHAWed's music only plays in skate mode while GTA runs
        Tell(link, kMusicPause);
        if (g_skateWas) {
            StopGta(log);
            if (log) log("radio: skate mode off: GTA radio stopped, reTHAWed's music paused");
        }
        g_skateWas = false;
        g_holdEvents = 0;
        return;
    }
    const DWORD now = GetTickCount();
    if (!g_skateWas) {
        g_skateWas = true;
        if (!g_loggedNames && log) {
            g_loggedNames = true;
            char line[600] = "radio: GTA station names:";
            for (int id = 0; id <= 13; ++id) {
                char n[64];
                NameOf(id, n, sizeof(n));
                char part[80];
                sprintf_s(part, " %d=%s", id, n);
                strcat_s(line, part);
            }
            log("%s", line);
        }
        NameOf(g_list[g_sel], g_name, sizeof(g_name));
        g_showUntil = now + kShowMs;
        if (log) log("radio: skate mode on: station %s", g_name);
    }
    if (g_holdEvents > 0) {
        g_holdEvents = 0;
        g_sel = (g_sel + 1) % g_count;
        NameOf(g_list[g_sel], g_name, sizeof(g_name));
        g_showUntil = now + kShowMs;
        if (log) log("radio: Back held: station %s", g_name);
    }

    const int station = g_list[g_sel];
    Tell(link, station == kThaw ? kMusicPlay : kMusicPause);
    if (station >= 0 && now / 1000 != g_diagSecond) { // diagnostics: once a second on a GTA station
        g_diagSecond = now / 1000;
        DiagLogState(log, "each second");
        DiagLogStops(log);
    }
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
        if (!radioOn) {
            if (g_restarts < 1) {
                ++g_restarts;
                if (log) log("radio: GTA's radio had stopped (station %d, %u interior stops skipped so far); starting it once more",
                             g_playingGta, g_stopsSkipped);
                StartGta(g_playingGta, log);
            } else if (g_restarts == 1) {
                ++g_restarts;
                if (log) log("radio: GTA's radio stopped again (station %d); not restarting (each start is a hitch)", g_playingGta);
            }
        }
    }
}

void RadioAddHook()
{
    plugin::Events::drawHudEvent.Add([] { DrawNameSafe(); });
}

} // namespace mp::gta
