// my-passthrough: reading the local skater from reTHAWed's memory (phase 1).
//
// READ ONLY: nothing here writes into the game or hooks anything.
//
// Where the numbers come from (docs/DESIGN.md section 8.4; read from a copy of
// reTHAWed.exe, which has THAW 1.01's code layout, same as partymod-thaw):
//   - global pointer at 0x0074FB44 = skate manager (most likely)
//   - 0x004A0160, called on the manager = returns the local skater (most likely)
//   - skater +0x68/+0x6C/+0x70 = position x/y/z (from GetSkaterPosition)
//   - skater +0xC8/+0xCC/+0xD0 = velocity x/y/z (from SkaterSpeedGreaterThan)

#pragma once

#include <windows.h>

namespace mp::thaw {

struct SkaterSample {
    bool readOk = false;          // false = an exception happened while reading
    DWORD exceptionCode = 0;      // Windows exception code if readOk is false
    unsigned long manager = 0;    // value of the global at 0x0074FB44 (0 = none)
    unsigned long skater = 0;     // address returned by 0x004A0160 (0 = no skater)
    float pos[3] = {};
    float vel[3] = {};
};

// One code-byte check: what we expect at an address in reTHAWed.exe.
struct CodeCheck {
    unsigned long address;
    unsigned char bytes[18];
    unsigned int length;
    const char* what;
};

// The checks; all must pass before ReadSkater may be used.
const CodeCheck* CodeChecks(unsigned int& count);

// Compare one check with the game's memory. Returns true if all bytes match.
// `firstMismatch` gets the index of the first wrong byte (if any).
bool RunCodeCheck(const CodeCheck& check, unsigned int& firstMismatch, unsigned char& found);

// Read the local skater. Safe to call from our own thread: any bad read is
// caught and reported in the sample instead of crashing the game.
SkaterSample ReadSkater();

} // namespace mp::thaw
