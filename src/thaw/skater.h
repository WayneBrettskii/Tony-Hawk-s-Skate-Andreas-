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
// Phase 1-2 (section 8.5):
//   - global pointer at 0x008A3B5C = game-flow object (most likely)
//   - its +0x20 object, passed to 0x00594310 = "in front end (menus)?" (InFrontend)
//   - its +0x64 = current level ID, a checksum (GetCurrentLevel)
//   - byte at 0x007CDD2C = game paused (GameIsPaused)

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
enum CheckGroup { kGroupSkater = 0, kGroupState = 1 };
struct CodeCheck {
    unsigned long address;
    unsigned char bytes[18];
    unsigned int length;
    const char* what;
    int group; // CheckGroup: a failed check only disables its own group
};

// Where InFrontend's final jump (at 0x0059A668) goes in the running game.
// In the file it goes to 0x00594310; reTHAWed may redirect it.
struct JumpInfo {
    bool isJump = false;          // first byte is E9 (jump)
    unsigned char bytes[5] = {};
    unsigned long target = 0;
    wchar_t module[MAX_PATH] = {}; // file the target lies in, if any
};
JumpInfo ReadInFrontendJump();

// The checks; all must pass before ReadSkater may be used.
const CodeCheck* CodeChecks(unsigned int& count);

// Compare one check with the game's memory. Returns true if all bytes match.
// `firstMismatch` gets the index of the first wrong byte (if any).
bool RunCodeCheck(const CodeCheck& check, unsigned int& firstMismatch, unsigned char& found);

// Read the local skater. Safe to call from our own thread: any bad read is
// caught and reported in the sample instead of crashing the game.
SkaterSample ReadSkater();

struct GameState {
    bool readOk = false;          // false = an exception happened while reading
    DWORD exceptionCode = 0;
    unsigned long gameFlow = 0;   // value of the global at 0x008A3B5C (0 = none)
    int inFrontend = -1;          // 1 = menus, 0 = not menus, -1 = unknown
    unsigned long levelId = 0;    // current level checksum
    int paused = -1;              // 1 = paused, 0 = not paused, -1 = unknown
};

// Read menu/level/pause state. Separately protected from ReadSkater.
GameState ReadGameState();

} // namespace mp::thaw
