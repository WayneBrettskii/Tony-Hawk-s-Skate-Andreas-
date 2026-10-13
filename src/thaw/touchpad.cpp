// SkateAndreas: direct DualSense read for the test shortcuts. See touchpad.h.

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>

#include "touchpad.h"

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace mp::thaw {
namespace {

constexpr USHORT kSonyVid = 0x054C;
constexpr USHORT kDualSensePid = 0x0CE6;
constexpr USHORT kDualSenseEdgePid = 0x0DF2;

LinkBlock* g_link = nullptr;
volatile LONG g_stop = 0;
volatile LONG g_bits = 0;

// Status for the log, written by the reading thread, logged by the main loop.
enum Status : LONG { kSearching = 0, kNotFound, kOpenFailed, kReading, kLost };
volatile LONG g_status = kSearching;
volatile LONG g_error = 0;     // Windows error of the last failed open
volatile LONG g_reports = 0;   // reports read (to log the first one)
volatile LONG g_firstId = 0;   // report id and size of the first report
volatile LONG g_firstLen = 0;

// One report -> kPadRaw* bits. Layouts (byte 0 = report id):
//   USB 0x01 (64 bytes): hat = [8] & 0x0F, touchpad = [10] & 0x02
//   Bluetooth 0x31 (78 bytes): the same one byte later: [9], [11]
//   Bluetooth short 0x01 (~10 bytes, before a driver asks for the full one):
//     hat = [5] & 0x0F, touchpad = [7] & 0x02
// Hat: 0 = up, 1 = up-right, ... 4 = down, ... 7 = up-left, 8 = none.
LONG Decode(const BYTE* r, DWORD n)
{
    int hatAt = -1, tpAt = -1;
    if (n >= 12 && r[0] == 0x31) { hatAt = 9; tpAt = 11; }
    else if (n >= 40 && r[0] == 0x01) { hatAt = 8; tpAt = 10; }
    else if (n >= 8 && r[0] == 0x01) { hatAt = 5; tpAt = 7; }
    else return 0;
    const int hat = r[hatAt] & 0x0F;
    LONG bits = 0;
    if (r[tpAt] & 0x02) bits |= kPadRawTouch;
    if (hat == 7 || hat == 0 || hat == 1) bits |= kPadRawUp;
    if (hat == 3 || hat == 4 || hat == 5) bits |= kPadRawDown;
    if (hat == 5 || hat == 6 || hat == 7) bits |= kPadRawLeft;
    if (hat == 1 || hat == 2 || hat == 3) bits |= kPadRawRight;
    return bits;
}

void Publish(LONG bits)
{
    InterlockedExchange(&g_bits, bits);
    if (g_link) InterlockedExchange(&g_link->reserved[0], bits);
}

// Find a DualSense and open it for reading (shared with DualSenseX and Windows).
HANDLE OpenDualSense()
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    HANDLE found = INVALID_HANDLE_VALUE;
    LONG lastError = 0;
    bool sawSony = false;
    SP_DEVICE_INTERFACE_DATA ifd = {sizeof(ifd)};
    for (DWORD i = 0; found == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); ++i) {
        alignas(8) BYTE buf[1024];
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, sizeof(buf), nullptr, nullptr)) continue;
        // First open without read access just to check who it is.
        HANDLE probe = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (probe == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES a = {sizeof(a)};
        const bool ok = HidD_GetAttributes(probe, &a) && a.VendorID == kSonyVid &&
                        (a.ProductID == kDualSensePid || a.ProductID == kDualSenseEdgePid);
        CloseHandle(probe);
        if (!ok) continue;
        sawSony = true;
        HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) lastError = static_cast<LONG>(GetLastError());
        else found = h;
    }
    SetupDiDestroyDeviceInfoList(set);
    if (found == INVALID_HANDLE_VALUE) {
        InterlockedExchange(&g_error, lastError);
        InterlockedExchange(&g_status, sawSony ? kOpenFailed : kNotFound);
    }
    return found;
}

DWORD WINAPI ReadThread(LPVOID)
{
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    while (!g_stop) {
        HANDLE h = OpenDualSense();
        if (h == INVALID_HANDLE_VALUE) {
            for (int i = 0; i < 20 && !g_stop; ++i) Sleep(100);
            continue;
        }
        InterlockedExchange(&g_status, kReading);
        BYTE report[128];
        while (!g_stop) {
            DWORD n = 0;
            ResetEvent(ov.hEvent);
            if (!ReadFile(h, report, sizeof(report), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) break;
            const DWORD w = WaitForSingleObject(ov.hEvent, 200);
            if (w == WAIT_TIMEOUT) {
                CancelIo(h);
                GetOverlappedResult(h, &ov, &n, TRUE);
                continue; // no report for 200 ms (nothing changed on some connections)
            }
            if (!GetOverlappedResult(h, &ov, &n, FALSE)) break; // unplugged
            if (InterlockedIncrement(&g_reports) == 1) {
                InterlockedExchange(&g_firstId, report[0]);
                InterlockedExchange(&g_firstLen, static_cast<LONG>(n));
            }
            Publish(Decode(report, n));
        }
        CancelIo(h);
        CloseHandle(h);
        Publish(0);
        if (!g_stop) InterlockedExchange(&g_status, kLost);
    }
    CloseHandle(ov.hEvent);
    return 0;
}

} // namespace

void TouchpadStart(LinkBlock* link)
{
    g_link = link;
    Publish(0);
    HANDLE t = CreateThread(nullptr, 0, ReadThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
}

LONG TouchpadTick(void (*log)(const char*, ...))
{
    static LONG loggedStatus = -1;
    static bool loggedFirst = false;
    const LONG st = g_status;
    if (st != loggedStatus && st != kSearching) {
        loggedStatus = st;
        switch (st) {
        case kNotFound: log("touchpad: no PS5 controller (DualSense) found; looking again every 2 s"); break;
        case kOpenFailed:
            log("touchpad: found the PS5 controller but could not open it (Windows error %ld); maybe hidden by "
                "DualSenseX / HidHide. Looking again every 2 s", g_error);
            break;
        case kReading: log("touchpad: reading the PS5 controller (touchpad + D-pad Up = F11 here, + D-pad Down = F9 in GTA)"); break;
        case kLost: log("touchpad: lost the PS5 controller; looking again"); break;
        }
    }
    if (!loggedFirst && g_reports > 0) {
        loggedFirst = true;
        log("touchpad: first report: id 0x%02lX, %ld bytes", g_firstId, g_firstLen);
    }
    static LONG lastBits = 0;
    const LONG bits = g_bits;
    if ((bits & kPadRawTouch) && !(lastBits & kPadRawTouch)) log("touchpad: pressed");
    lastBits = bits;
    return bits;
}

void TouchpadStop()
{
    InterlockedExchange(&g_stop, 1);
    if (g_link) InterlockedExchange(&g_link->reserved[0], 0);
}

} // namespace mp::thaw
