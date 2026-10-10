// my-passthrough: reading the local skater from reTHAWed's memory (phase 1).
// See skater.h for where every address comes from. READ ONLY.

#include "skater.h"

#include <cstring>

namespace mp::thaw {
namespace {

constexpr unsigned long kSkateManagerGlobal = 0x0074FB44;
constexpr unsigned long kGetLocalSkater = 0x004A0160;
constexpr unsigned long kPosOffset = 0x68;
constexpr unsigned long kVelOffset = 0xC8;

// Exact bytes found in reTHAWed.exe (5.0.0.1) at these addresses.
const CodeCheck kChecks[] = {
    {0x005A9407, {0x8B, 0x0D, 0x44, 0xFB, 0x74, 0x00, 0xE8, 0x4E, 0x6D, 0xEF, 0xFF}, 11,
     "SkaterSpeedGreaterThan: mov ecx,[0x74FB44]; call 0x4A0160", kGroupSkater},
    {0x005A9416, {0xD9, 0x80, 0xD0, 0x00, 0x00, 0x00, 0xD9, 0x80, 0xCC, 0x00, 0x00, 0x00,
                  0xD9, 0x80, 0xC8, 0x00, 0x00, 0x00}, 18,
     "SkaterSpeedGreaterThan: velocity at +0xD0/+0xCC/+0xC8", kGroupSkater},
    {0x005A98E3, {0xF3, 0x0F, 0x10, 0x48, 0x6C, 0xF3, 0x0F, 0x10, 0x40, 0x68}, 10,
     "GetSkaterPosition: position at +0x6C/+0x68", kGroupSkater},
    {0x005A98F6, {0xF3, 0x0F, 0x10, 0x48, 0x70}, 5,
     "GetSkaterPosition: position at +0x70", kGroupSkater},
    {0x004A0160, {0x53, 0x56, 0x57, 0x33, 0xFF, 0x83, 0xCE, 0xFF}, 8,
     "get local skater: start of function", kGroupSkater},
    // phase 1-2. InFrontend: only the first 8 bytes; its final jump (at 0x0059A668)
    // is changed in the running game (attempt 1), so it is logged, not checked.
    {0x0059A660, {0xA1, 0x5C, 0x3B, 0x8A, 0x00, 0x8B, 0x48, 0x20}, 8,
     "InFrontend: mov eax,[0x8A3B5C]; mov ecx,[eax+0x20]", kGroupState},
    {0x00594310, {0x51, 0x8B, 0x81, 0xB0, 0x00, 0x00, 0x00, 0x8B, 0x89, 0xA8, 0x00, 0x00, 0x00}, 13,
     "original in-front-end check: reads [ecx+0xB0], [ecx+0xA8]", kGroupState},
    {0x0059C230, {0xA1, 0x5C, 0x3B, 0x8A, 0x00, 0x8B, 0x48, 0x64}, 8,
     "GetCurrentLevel: mov eax,[0x8A3B5C]; mov ecx,[eax+0x64]", kGroupState},
    {0x0059BD30, {0xA0, 0x2C, 0xDD, 0x7C, 0x00, 0x84, 0xC0}, 7,
     "GameIsPaused: mov al,[0x7CDD2C]; test al,al", kGroupState},
};

constexpr unsigned long kInFrontendJumpAddr = 0x0059A668;

constexpr unsigned long kGameFlowGlobal = 0x008A3B5C;
constexpr unsigned long kFrontendObjOffset = 0x20;
constexpr unsigned long kLevelIdOffset = 0x64;
constexpr unsigned long kInFrontendFn = 0x00594310;
constexpr unsigned long kPausedFlag = 0x007CDD2C;

// 0x00594310: member function ("thiscall"), returns a bool in AL.
using InFrontendFn = bool(__thiscall*)(void* frontendObj);

void ReadGameStateUnprotected(GameState* out)
{
    out->paused = *reinterpret_cast<volatile unsigned char*>(kPausedFlag) ? 1 : 0;

    const unsigned long flow = *reinterpret_cast<volatile unsigned long*>(kGameFlowGlobal);
    out->gameFlow = flow;
    if (flow == 0) return;

    out->levelId = *reinterpret_cast<volatile unsigned long*>(flow + kLevelIdOffset);
    const unsigned long fe = *reinterpret_cast<volatile unsigned long*>(flow + kFrontendObjOffset);
    if (fe == 0) return;
    auto inFrontend = reinterpret_cast<InFrontendFn>(kInFrontendFn);
    out->inFrontend = inFrontend(reinterpret_cast<void*>(fe)) ? 1 : 0;
}

// 0x004A0160 is a C++ member function: the manager goes in the ECX register
// ("thiscall"), and it returns the skater pointer.
using GetLocalSkaterFn = void*(__thiscall*)(void* manager);

// The actual reads. Kept in its own function with no C++ objects, because
// Windows' exception protection (__try/__except) requires that.
void ReadSkaterUnprotected(SkaterSample* out)
{
    const unsigned long manager = *reinterpret_cast<volatile unsigned long*>(kSkateManagerGlobal);
    out->manager = manager;
    if (manager == 0) return;

    auto getLocalSkater = reinterpret_cast<GetLocalSkaterFn>(kGetLocalSkater);
    const unsigned long skater =
        reinterpret_cast<unsigned long>(getLocalSkater(reinterpret_cast<void*>(manager)));
    out->skater = skater;
    if (skater == 0) return;

    const volatile float* pos = reinterpret_cast<const volatile float*>(skater + kPosOffset);
    const volatile float* vel = reinterpret_cast<const volatile float*>(skater + kVelOffset);
    for (int i = 0; i < 3; ++i) {
        out->pos[i] = pos[i];
        out->vel[i] = vel[i];
    }
}

} // namespace

const CodeCheck* CodeChecks(unsigned int& count)
{
    count = static_cast<unsigned int>(sizeof(kChecks) / sizeof(kChecks[0]));
    return kChecks;
}

bool RunCodeCheck(const CodeCheck& check, unsigned int& firstMismatch, unsigned char& found)
{
    const unsigned char* code = reinterpret_cast<const unsigned char*>(check.address);
    for (unsigned int i = 0; i < check.length; ++i) {
        if (code[i] != check.bytes[i]) {
            firstMismatch = i;
            found = code[i];
            return false;
        }
    }
    return true;
}

SkaterSample ReadSkater()
{
    SkaterSample sample;
    __try {
        ReadSkaterUnprotected(&sample);
        sample.readOk = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sample.readOk = false;
        sample.exceptionCode = GetExceptionCode();
    }
    return sample;
}

bool ReadPosQuick(unsigned long skater, float out[3])
{
    __try {
        const volatile float* pos = reinterpret_cast<const volatile float*>(skater + kPosOffset);
        out[0] = pos[0];
        out[1] = pos[1];
        out[2] = pos[2];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

JumpInfo ReadInFrontendJump()
{
    JumpInfo info;
    const unsigned char* code = reinterpret_cast<const unsigned char*>(kInFrontendJumpAddr);
    for (int i = 0; i < 5; ++i) info.bytes[i] = code[i];
    info.isJump = (info.bytes[0] == 0xE9);
    if (!info.isJump) return info;

    long offset = 0;
    memcpy(&offset, &info.bytes[1], sizeof(offset));
    info.target = kInFrontendJumpAddr + 5 + static_cast<unsigned long>(offset);

    // Which loaded file (exe or DLL) contains the target? Only looks it up;
    // does not keep the file loaded.
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(info.target), &mod) && mod) {
        GetModuleFileNameW(mod, info.module, MAX_PATH);
    }
    return info;
}

GameState ReadGameState()
{
    GameState state;
    __try {
        ReadGameStateUnprotected(&state);
        state.readOk = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        state.readOk = false;
        state.exceptionCode = GetExceptionCode();
    }
    return state;
}

} // namespace mp::thaw
