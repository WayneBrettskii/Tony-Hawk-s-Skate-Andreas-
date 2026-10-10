// my-passthrough: frame recorder. See recorder.h.

#include <windows.h>

#include "recorder.h"

#include <cstdio>
#include <cstring>

#ifndef MP_LOG_DIR
#error "MP_LOG_DIR must be set by CMakeLists.txt"
#endif

namespace mp::gta {
namespace {

constexpr int kKey = VK_F9;
constexpr int kRows = 180; // 3 seconds at 60 frames a second

const char* const kNames[REC_COLS] = {
    "t_ms", "frame", "sample", "age_ms", "blend_pct",
    "thaw_x", "thaw_y", "thaw_z", "vel_x", "vel_y", "vel_z",
    "target_x", "target_y", "target_z", "cj_x", "cj_y", "cj_z", "up_x", "up_y", "up_z",
    "board_x", "board_y", "board_z", "hips_x", "hips_y", "hips_z",
    "chest_x", "chest_y", "chest_z", "arm_x", "arm_y", "arm_z",
    "draw_ms", "draw_frame", "drawn_hips_x", "drawn_hips_y", "drawn_hips_z",
};

float g_rows[kRows][REC_COLS];
int g_row = -1;          // current row; -1 = not recording
bool g_keyWasDown = false;
int g_files = 0;
LARGE_INTEGER g_start = {}, g_freq = {};

bool GtaIsForeground()
{
    HWND w = GetForegroundWindow();
    if (!w) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

void Write(LogFn log)
{
    ++g_files;
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\gta_frames_%d.csv", L"" MP_LOG_DIR, g_files);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        log("recorder: could not write gta_frames_%d.csv (Windows error %lu)", g_files, GetLastError());
        return;
    }
    char line[2048];
    DWORD written = 0;
    int n = 0;
    for (int c = 0; c < REC_COLS; ++c) n += sprintf_s(line + n, sizeof(line) - n, c ? ",%s" : "%s", kNames[c]);
    n += sprintf_s(line + n, sizeof(line) - n, "\r\n");
    WriteFile(f, line, static_cast<DWORD>(n), &written, nullptr);
    for (int r = 0; r < kRows; ++r) {
        n = 0;
        for (int c = 0; c < REC_COLS; ++c) n += sprintf_s(line + n, sizeof(line) - n, c ? ",%.4f" : "%.4f", g_rows[r][c]);
        n += sprintf_s(line + n, sizeof(line) - n, "\r\n");
        WriteFile(f, line, static_cast<DWORD>(n), &written, nullptr);
    }
    CloseHandle(f);
    log("recorder: 3 seconds (%d frames) written to logs\\gta_frames_%d.csv", kRows, g_files);
}

} // namespace

float RecNowMs()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<float>(static_cast<double>(now.QuadPart - g_start.QuadPart) * 1000.0 /
                              static_cast<double>(g_freq.QuadPart ? g_freq.QuadPart : 1));
}

void RecorderFrameBegin(LogFn log, bool padCombo)
{
    static bool padWasDown = false;
    const bool padPressed = padCombo && !padWasDown;
    padWasDown = padCombo;
    const bool down = (GetAsyncKeyState(kKey) & 0x8000) != 0;
    const bool pressed = down && !g_keyWasDown;
    g_keyWasDown = down;

    if (g_row >= 0) {
        if (++g_row >= kRows) {
            g_row = -1;
            Write(log);
            return;
        }
    } else if ((pressed && GtaIsForeground()) || padPressed) {
        QueryPerformanceFrequency(&g_freq);
        QueryPerformanceCounter(&g_start);
        g_row = 0;
        log("recorder: %s - recording the next 3 seconds", padPressed ? "touchpad + D-pad Down" : "F9");
    } else {
        return;
    }
    for (int c = 0; c < REC_COLS; ++c) g_rows[g_row][c] = 0.0f;
    g_rows[g_row][REC_DRAW_MS] = -1.0f;
    g_rows[g_row][REC_T_MS] = RecNowMs();
}

bool RecorderOn() { return g_row >= 0; }

void RecSet(RecCol col, float value)
{
    if (g_row >= 0 && col >= 0 && col < REC_COLS) g_rows[g_row][col] = value;
}

void RecSet3(RecCol first, float x, float y, float z)
{
    RecSet(first, x);
    RecSet(static_cast<RecCol>(first + 1), y);
    RecSet(static_cast<RecCol>(first + 2), z);
}

} // namespace mp::gta
