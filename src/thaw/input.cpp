// SkateAndreas: reTHAWed takes its controller input from GTA (phase 3-3).
// See input.h.

#include <windows.h>
#include "input.h"
#include "music.h"
#include "bmx.h"
#include "railscan.h" // 8.46: RailPoolGameThreadTick
#include "vehscan.h"  // 8.51: VehGameThreadTick
#include "autostart.h"

#include <cstring>

namespace mp::thaw {

// ---- pure conversions ----

short GtaStickToSdl(short gta)
{
    const int v = static_cast<int>(gta) * 256;
    if (v < -32768) return -32768;
    if (v > 32767) return 32767;
    return static_cast<short>(v);
}

short GtaTriggerToSdl(short gta)
{
    int v = gta;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return static_cast<short>(v * 32767 / 255);
}

long SdlButtonToGtaBit(int sdlButton)
{
    // SDL_GameControllerButton numbers (SDL 2.26).
    switch (sdlButton) {
    case 0: return kBtnCross;     // A
    case 1: return kBtnCircle;    // B
    case 2: return kBtnSquare;    // X
    case 3: return kBtnTriangle;  // Y
    case 4: return kBtnSelect;    // BACK
    case 6: return kBtnStart;     // START
    case 7: return kBtnL3;        // LEFTSTICK
    case 8: return kBtnR3;        // RIGHTSTICK
    case 9: return kBtnL1;        // LEFTSHOULDER
    case 10: return kBtnR1;       // RIGHTSHOULDER
    case 11: return kBtnUp;       // DPAD_UP
    case 12: return kBtnDown;     // DPAD_DOWN
    case 13: return kBtnLeft;     // DPAD_LEFT
    case 14: return kBtnRight;    // DPAD_RIGHT
    default: return 0;            // GUIDE, MISC1, paddles, touchpad: GTA has none
    }
}

namespace {

enum Source : LONG { kReal = 0, kFromGta = 1, kNothing = 2 };
const char* const kSourceNames[3] = {"REAL controller (reTHAWed in front)", "GTA", "NOTHING"};

using GetButtonFn = unsigned char(__cdecl*)(void*, int);
using GetAxisFn = short(__cdecl*)(void*, int);

GetButtonFn g_realGetButton = nullptr;
GetAxisFn g_realGetAxis = nullptr;
LinkBlock* volatile g_link = nullptr;
volatile LONG g_source = kNothing;
bool g_tried = false;
bool g_installed = false;

volatile LONG g_calls[3] = {};
volatile LONG g_pressedFromGta = 0; // GetButton answers "pressed" from GTA's frame

unsigned char __cdecl HookGetButton(void* controller, int button)
{
    // 8.27 radio: reTHAWed's game thread; pause / resume its music as GTA wants
    MusicGameThreadTick(g_link);
    // 8.29 BMX: rider on / off the bike as CJ
    BmxGameThreadTick(g_link);
    // 8.46: the rail pool's queued writes, applied between reTHAWed's frames
    RailPoolGameThreadTick();
    // 8.51: a reTHAWed vehicle following a GTA car, written between reTHAWed's frames
    VehGameThreadTick();
    // 8.35 test: try to load our map from reTHAWed's front end
    AutoStartGameThreadTick(g_link);
    // 8.35: the front-end replay drives reTHAWed's menus for the player
    const int synth = AutoStartSyntheticButtons();
    if (synth) {
        const long bit = SdlButtonToGtaBit(button);
        if (bit && (synth & bit)) return 1;
    }
    const LONG src = g_source;
    InterlockedIncrement(&g_calls[src]);
    if (src == kReal) {
        const unsigned char real = g_realGetButton(controller, button);
        // 8.35: capture the front-end navigation while reTHAWed is the front window
        AutoStartNoteFrontButton(static_cast<int>(SdlButtonToGtaBit(button)), real != 0);
        return real;
    }
    if (src == kNothing) return 0;
    InputShared in;
    LinkBlock* link = g_link;
    if (!link || !ReadInputShared(link, in)) return 0;
    const long bit = SdlButtonToGtaBit(button);
    if (bit && (in.buttons & bit)) {
        InterlockedIncrement(&g_pressedFromGta);
        return 1;
    }
    return 0;
}

short __cdecl HookGetAxis(void* controller, int axis)
{
    const LONG src = g_source;
    InterlockedIncrement(&g_calls[src]);
    if (src == kReal) return g_realGetAxis(controller, axis);
    if (src == kNothing) return 0;
    InputShared in;
    LinkBlock* link = g_link;
    if (!link || !ReadInputShared(link, in)) return 0;
    switch (axis) { // SDL_GameControllerAxis numbers
    case 0: return GtaStickToSdl(in.axes[kAxisLX]);
    case 1: return GtaStickToSdl(in.axes[kAxisLY]);
    case 2: return GtaStickToSdl(in.axes[kAxisRX]);
    case 3: return GtaStickToSdl(in.axes[kAxisRY]);
    case 4: return GtaTriggerToSdl(in.axes[kAxisL2]);
    case 5: return GtaTriggerToSdl(in.axes[kAxisR2]);
    default: return 0;
    }
}

// Find one imported function's slot in a module's import table.
// Returns nullptr if the module doesn't import it from `dllName`.
void** FindImportSlot(HMODULE module, const char* dllName, const char* function)
{
    auto* base = reinterpret_cast<unsigned char*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp(reinterpret_cast<const char*>(base + imp->Name), dllName) != 0) continue;
        if (!imp->OriginalFirstThunk) return nullptr; // no name list; can't match by name
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), function) == 0)
                return reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return nullptr;
}

// No C++ objects with destructors here (__try/__except).
void** FindImportSlotSafe(HMODULE module, const char* dllName, const char* function)
{
    __try {
        return FindImportSlot(module, dllName, function);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool WriteSlot(void** slot, void* value)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) return false;
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot), value); // one aligned write
    DWORD unused = 0;
    VirtualProtect(slot, sizeof(void*), oldProtect, &unused);
    return true;
}

void Install(InputLogFn log)
{
    HMODULE rthw = GetModuleHandleW(L"rthw.dll");
    HMODULE sdl = GetModuleHandleW(L"SDL2.dll");
    if (!rthw || !sdl) {
        log("input: NOT installed (rthw.dll %s, SDL2.dll %s)", rthw ? "found" : "MISSING",
            sdl ? "found" : "MISSING");
        return;
    }
    void** buttonSlot = FindImportSlotSafe(rthw, "SDL2.dll", "SDL_GameControllerGetButton");
    void** axisSlot = FindImportSlotSafe(rthw, "SDL2.dll", "SDL_GameControllerGetAxis");
    if (!buttonSlot || !axisSlot) {
        log("input: NOT installed (rthw.dll's import table: GetButton %s, GetAxis %s)",
            buttonSlot ? "found" : "MISSING", axisSlot ? "found" : "MISSING");
        return;
    }
    void* sdlButton = reinterpret_cast<void*>(GetProcAddress(sdl, "SDL_GameControllerGetButton"));
    void* sdlAxis = reinterpret_cast<void*>(GetProcAddress(sdl, "SDL_GameControllerGetAxis"));
    log("input: rthw.dll import slots: GetButton at 0x%08lX = 0x%08lX (%s), GetAxis at 0x%08lX = 0x%08lX (%s)",
        static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(buttonSlot)),
        static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(*buttonSlot)),
        *buttonSlot == sdlButton ? "SDL2.dll's own" : "NOT SDL2.dll's own; chaining to it",
        static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(axisSlot)),
        static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(*axisSlot)),
        *axisSlot == sdlAxis ? "SDL2.dll's own" : "NOT SDL2.dll's own; chaining to it");

    // For the record: does the game exe itself also ask SDL? (Not changed.)
    void** exeButton = FindImportSlotSafe(GetModuleHandleW(nullptr), "SDL2.dll", "SDL_GameControllerGetButton");
    log("input: reTHAWed.exe imports SDL_GameControllerGetButton: %s", exeButton ? "YES (not changed)" : "no");

    g_realGetButton = reinterpret_cast<GetButtonFn>(*buttonSlot);
    g_realGetAxis = reinterpret_cast<GetAxisFn>(*axisSlot);
    const bool okButton = WriteSlot(buttonSlot, reinterpret_cast<void*>(&HookGetButton));
    const bool okAxis = WriteSlot(axisSlot, reinterpret_cast<void*>(&HookGetAxis));
    g_installed = okButton || okAxis;
    log("input: replaced GetButton: %s, GetAxis: %s", okButton ? "yes" : "FAILED", okAxis ? "yes" : "FAILED");
}

bool ReTHAWedInFront()
{
    HWND w = GetForegroundWindow();
    if (!w) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

LONG g_lastCalls[3] = {};
LONG g_lastPressed = 0;
LONG g_loggedSource = -1;

} // namespace

void InputTick(LinkBlock* link, InputLogFn log)
{
    g_link = link;
    if (!g_tried) {
        g_tried = true;
        Install(log);
    }
    if (!g_installed) return;

    // Pick the source.
    LONG src = kNothing;
    const char* why = nullptr;
    if (ReTHAWedInFront()) {
        src = kReal;
        why = "reTHAWed is the front window";
    } else if (!link) {
        why = "no link to GTA";
    } else if (link->gtaPid == 0) {
        why = "GTA not running";
    } else if (link->gtaSkateMode == 0) { // 1 skate mode, 2 BMX mode (8.29)
        why = "skate / BMX mode is off in GTA";
    } else {
        InputShared in;
        if (!ReadInputShared(link, in) || in.count == 0) {
            why = "no controller frame from GTA yet";
        } else {
            const LONG age = static_cast<LONG>(GetTickCount64() & 0xFFFFFFFFu) - in.tick;
            if (age > 100 || age < -100) {
                why = "GTA's controller frames stopped (over 100 ms old)";
            } else {
                src = kFromGta;
                why = link->gtaSkateMode == 2 ? "BMX mode on, reTHAWed behind GTA" : "skate mode on, reTHAWed behind GTA";
            }
        }
    }
    InterlockedExchange(&g_source, src);
    if (src != g_loggedSource) {
        log("input: source now %s (%s)", kSourceNames[src], why);
        g_loggedSource = src;
    }
}

LONG InputCallTotal()
{
    return g_calls[0] + g_calls[1] + g_calls[2];
}

void InputSecond(InputLogFn log)
{
    if (!g_installed) return;
    LONG now[3];
    bool any = false;
    for (int i = 0; i < 3; ++i) {
        now[i] = g_calls[i];
        if (now[i] != g_lastCalls[i]) any = true;
    }
    const LONG pressed = g_pressedFromGta;
    if (any)
        log("input: questions answered in the last second: real %ld, from GTA %ld (%ld 'pressed'), nothing %ld",
            now[kReal] - g_lastCalls[kReal], now[kFromGta] - g_lastCalls[kFromGta], pressed - g_lastPressed,
            now[kNothing] - g_lastCalls[kNothing]);
    for (int i = 0; i < 3; ++i) g_lastCalls[i] = now[i];
    g_lastPressed = pressed;
}

} // namespace mp::thaw
