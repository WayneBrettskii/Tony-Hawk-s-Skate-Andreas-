// SkateAndreas: tests for the checked road-file reader (8.44 F2, audit 2026-10-15).
// Every file here is made up in memory; no game file is read.
// Built with the project (32-bit, like the plugins): run build\Release\parser_tests.exe.
// Prints one line per test and exits with 0 when all pass.

#include "../gta/roadfile.h"

#include <cstdio>
#include <vector>

using mp::gta::ParseRoadHeader;
using mp::gta::ReadAt;
using mp::gta::RoadHeader;
using mp::gta::RoadLinkAt;
using mp::gta::RoadNodeAt;

namespace {

int g_failed = 0;

void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failed;
}

void Put32(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<unsigned char>(v >> (8 * i));
}

std::vector<unsigned char> Header(uint32_t n, uint32_t nv, uint32_t nn, uint32_t nl, size_t total)
{
    std::vector<unsigned char> b(total, 0);
    if (total >= 20) {
        Put32(b, 0, n);
        Put32(b, 4, nv);
        Put32(b, 8, n - nv);
        Put32(b, 12, nn);
        Put32(b, 16, nl);
    }
    return b;
}

} // namespace

int main()
{
    std::printf("size_t is %u bits here (the plugins are 32-bit)\n", static_cast<unsigned>(sizeof(size_t) * 8));
    RoadHeader h;

    // The audit's case: n = 0x40000000, nv = 1, nn = 0, nl = 0 in a 20-byte file.
    // 20 + 28 * 0x40000000 wraps to 20 in 32 bits, which the old check accepted.
    Check(!ParseRoadHeader(Header(0x40000000u, 1, 0, 0, 20), h), "audit case (n = 0x40000000 in 20 bytes) refused");

    // A minimal valid file: 2 nodes (1 car node), 1 navi node, 3 links.
    {
        const size_t size = 20 + 28 * 2 + 14 * 1 + 4 * 3;
        std::vector<unsigned char> b = Header(2, 1, 1, 3, size);
        Check(ParseRoadHeader(b, h) && h.n == 2 && h.nv == 1 && h.nn == 1 && h.nl == 3, "minimal valid file accepted");
        uint32_t lastLink = 0;
        Check(ReadAt(b, RoadLinkAt(h, 2), lastLink), "last link readable");
        Check(!ReadAt(b, RoadLinkAt(h, 3), lastLink), "one link past the end refused");
        short x = 0;
        Check(ReadAt(b, RoadNodeAt(1) + 8, x), "last node's position readable");
        // one byte short
        std::vector<unsigned char> s(b.begin(), b.end() - 1);
        Check(!ParseRoadHeader(s, h), "file one byte too short refused");
    }

    Check(!ParseRoadHeader(Header(1, 2, 0, 0, 20 + 28), h), "more car nodes than nodes refused");
    Check(!ParseRoadHeader(std::vector<unsigned char>(19, 0), h), "file shorter than its header refused");
    Check(!ParseRoadHeader(std::vector<unsigned char>(), h), "empty file refused");
    Check(!ParseRoadHeader(Header(0xFFFFFFFFu, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 20), h), "all counts at maximum refused");
    Check(!ParseRoadHeader(Header(0, 0, 0, 0x40000000u, 20), h), "link count that wraps in 32 bits refused");
    Check(ParseRoadHeader(Header(0, 0, 0, 0, 20), h), "header-only file with no nodes accepted");

    // ReadAt edges
    {
        std::vector<unsigned char> b(8, 0);
        uint32_t v = 0;
        Check(ReadAt(b, 4, v), "read ending exactly at the end allowed");
        Check(!ReadAt(b, 5, v), "read crossing the end refused");
        Check(!ReadAt(b, 0xFFFFFFFFFFFFFFF0ull, v), "read at a huge offset refused");
    }

    std::printf("%s: %d failed\n", g_failed ? "FAILED" : "ALL PASSED", g_failed);
    return g_failed ? 1 : 0;
}
