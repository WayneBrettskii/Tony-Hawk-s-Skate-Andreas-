// SkateAndreas: checked reading of GTA's road files (data\Paths\NODES*.DAT) for the GPS.
// 8.44 F2 (audit, 2026-10-15): the old check worked out the file's expected size in 64-bit
// numbers but kept it in a size_t, which is 32-bit in our build, so a damaged file with a
// huge node count could pass the check and the reads then ran past the end of the data.
// Everything here stays 64-bit until it has been compared with the real file size, and
// every read is checked. No Windows headers: the tests (src/tests) use this file as is.

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace mp::gta {

// Read a T at byte `at` of `b`. False (and `out` untouched) if any of it is past the end.
template <class T> bool ReadAt(const std::vector<unsigned char>& b, uint64_t at, T& out)
{
    const uint64_t size = b.size();
    if (at > size || sizeof(T) > size - at) return false;
    std::memcpy(&out, b.data() + static_cast<size_t>(at), sizeof(T));
    return true;
}

// The counts at the start of a NODES file.
struct RoadHeader {
    uint32_t n = 0;  // all path nodes (28 bytes each)
    uint32_t nv = 0; // of those, car nodes (they come first)
    uint32_t nn = 0; // navi nodes (14 bytes each)
    uint32_t nl = 0; // links (4 bytes each)
};

inline uint64_t RoadFileNeeds(const RoadHeader& h)
{
    return 20ull + 28ull * h.n + 14ull * h.nn + 4ull * h.nl;
}

// Byte where node i starts, and where link k starts.
inline uint64_t RoadNodeAt(uint32_t i) { return 20ull + 28ull * i; }
inline uint64_t RoadLinkAt(const RoadHeader& h, uint64_t k) { return 20ull + 28ull * h.n + 14ull * h.nn + 4ull * k; }

// True when `b` is a whole NODES file: a header, no more car nodes than nodes, and room
// for every node, navi node and link the header promises.
inline bool ParseRoadHeader(const std::vector<unsigned char>& b, RoadHeader& h)
{
    if (!ReadAt(b, 0, h.n) || !ReadAt(b, 4, h.nv) || !ReadAt(b, 12, h.nn) || !ReadAt(b, 16, h.nl)) return false;
    if (h.nv > h.n) return false;
    return RoadFileNeeds(h) <= static_cast<uint64_t>(b.size());
}

} // namespace mp::gta
