// my-passthrough: reading the local skater from reTHAWed's memory (phase 1).
// See skater.h for where every address comes from. READ ONLY.

#include "skater.h"

namespace mp::thaw {
namespace {

constexpr unsigned long kSkateManagerGlobal = 0x0074FB44;
constexpr unsigned long kGetLocalSkater = 0x004A0160;
constexpr unsigned long kPosOffset = 0x68;
constexpr unsigned long kVelOffset = 0xC8;

// Exact bytes found in reTHAWed.exe (5.0.0.1) at these addresses.
const CodeCheck kChecks[] = {
    {0x005A9407, {0x8B, 0x0D, 0x44, 0xFB, 0x74, 0x00, 0xE8, 0x4E, 0x6D, 0xEF, 0xFF}, 11,
     "SkaterSpeedGreaterThan: mov ecx,[0x74FB44]; call 0x4A0160"},
    {0x005A9416, {0xD9, 0x80, 0xD0, 0x00, 0x00, 0x00, 0xD9, 0x80, 0xCC, 0x00, 0x00, 0x00,
                  0xD9, 0x80, 0xC8, 0x00, 0x00, 0x00}, 18,
     "SkaterSpeedGreaterThan: velocity at +0xD0/+0xCC/+0xC8"},
    {0x005A98E3, {0xF3, 0x0F, 0x10, 0x48, 0x6C, 0xF3, 0x0F, 0x10, 0x40, 0x68}, 10,
     "GetSkaterPosition: position at +0x6C/+0x68"},
    {0x005A98F6, {0xF3, 0x0F, 0x10, 0x48, 0x70}, 5,
     "GetSkaterPosition: position at +0x70"},
    {0x004A0160, {0x53, 0x56, 0x57, 0x33, 0xFF, 0x83, 0xCE, 0xFF}, 8,
     "get local skater: start of function"},
};

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

} // namespace mp::thaw
