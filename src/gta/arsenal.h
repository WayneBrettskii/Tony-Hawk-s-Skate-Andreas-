#pragma once
#include <windows.h>
namespace mp::gta {
using LogFn = void (*)(const char*, ...);
void ArsenalFrame(LogFn log);
void ArsenalFilterPad(LONG& buttons, short* axes);
bool ArsenalBlocksInput();
void ArsenalAddHook();
}
