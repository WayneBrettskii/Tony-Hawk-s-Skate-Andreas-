// SkateAndreas: converting reTHAWed (THAW) positions into GTA San Andreas ones.
// docs/DESIGN.md sections 4 and 8.6. Plain C++ (no Windows), so the unit test
// (convert_test.cpp) can run anywhere.
//
// THAW: Y is up (measured, phase 1-1). GTA: Z is up, 1 unit = 1 metre.
// Mapping, relative to an "anchor" taken when skate mode turns on:
//   GTA x = THAW x * scale
//   GTA y = THAW z * scale * ySign
//   GTA z = THAW y * scale
// scale 0.0254 assumes 1 THAW unit = 1 inch (fits phase 1 numbers; NOT proven).
// ySign: swapping two axes mirrors left/right; whether THAW also needs that
// mirror is unknown, so flip ySign to -1 if a left turn in reTHAWed makes CJ
// turn right. First skate test (2026-10-07): it did, so skate mode uses -1
// (src/gta/skatemode.cpp); the default here stays +1 for the unit test.

#pragma once

#include <cmath>

namespace mp {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Mapping {
    float scale = 0.0254f; // GTA metres per THAW unit
    float ySign = 1.0f;    // +1 or -1
};

// A THAW movement (difference between two positions, or a velocity) in GTA terms.
inline Vec3 ThawDeltaToGta(const Vec3& d, const Mapping& m)
{
    return Vec3{d.x * m.scale, d.z * m.scale * m.ySign, d.y * m.scale};
}

// Where CJ should be: GTA anchor + the skater's movement since the THAW anchor.
inline Vec3 MapPosition(const Vec3& thawPos, const Vec3& thawAnchor, const Vec3& gtaAnchor,
                        const Mapping& m)
{
    const Vec3 d{thawPos.x - thawAnchor.x, thawPos.y - thawAnchor.y, thawPos.z - thawAnchor.z};
    const Vec3 g = ThawDeltaToGta(d, m);
    return Vec3{gtaAnchor.x + g.x, gtaAnchor.y + g.y, gtaAnchor.z + g.z};
}

// ---- Phase 4: reTHAWed levels that are copies of part of GTA's world ----
// For such a level, the skater's position maps straight to a place in GTA
// (no anchor): GTA feet position = offset + scale * (x, ySign * z, y).
struct LevelAlignment {
    unsigned long levelId;  // reTHAWed's level checksum (thawLevelId in the link)
    const char* name;
    float scale;            // GTA metres per level unit
    float ySign;            // -1: same mirror as the anchor mapping
    Vec3 offset;            // GTA position of the level's origin (feet height)
};

// Los Santos East (thpsgoat.com mod 598). Worked out 2026-10-08 from 3 landmarks
// marked in both games (skatepark right corner, hospital steps, night club
// doors): 41.27 level units per metre, no rotation (0.04 degrees), errors 0.1 to
// 0.5 m sideways and under 1 cm in height.
constexpr LevelAlignment kLosSantosEast{0x0274F7DEul, "Los Santos East", 0.02422967f, -1.0f,
                                        Vec3{1789.2105f, -1397.8612f, 24.9750f}};

// Our own map made from GTA's collision by tools/gta_to_thaw_map.py (phase 4-2d):
// prefix MPGTALSE, level ID = checksum of "load_MPGTALSE". Built in inches
// (39.3701 units per metre) around the Los Santos East area's centre
// (1975, -1525), heights from GTA's 0: exact by construction.
constexpr LevelAlignment kGtaEastLs{0x9F014B98ul, "GTA East LS (SkateAndreas)", 0.0254f, -1.0f,
                                    Vec3{1975.0f, -1525.0f, 0.0f}};

// 8.35 notebook (2026-10-12): a script call from the front end does load our map
// (reTHAWed's debug.txt: `LOADING CUSTOM LEVEL...`, `Loading career level
// MPGTALSE...`), but the game's "current level" field (0x008A3B5C + 0x20, what
// ReadGameState reads) still says the FRONT-END level: its value 0x99CE4D72 is
// exactly qbkey("load_z_mainmenu"). So the plugin saw z_mainmenu, not our map:
// no alignment, "reTHAWed not in a level", and CJ floating. The fix is NOT an
// alignment entry for that ID (that would label the menu as our map) - it is to
// make the load happen the way the front end does it (DESIGN 8.35).

// The aligned level for this level ID, or nullptr.
inline const LevelAlignment* FindLevelAlignment(unsigned long levelId)
{
    if (levelId == kLosSantosEast.levelId) return &kLosSantosEast;
    if (levelId == kGtaEastLs.levelId) return &kGtaEastLs;
    return nullptr;
}

// GTA position of the feet for a skater position in an aligned level.
inline Vec3 MapAbsolute(const Vec3& thawPos, const LevelAlignment& a)
{
    return Vec3{a.offset.x + thawPos.x * a.scale, a.offset.y + thawPos.z * a.scale * a.ySign,
                a.offset.z + thawPos.y * a.scale};
}

// GTA heading (radians) for facing along the GTA-space direction (dx, dy).
// GTA's convention (believed, from GTA's CPlaceable::GetHeading): heading 0
// faces +Y (north), and heading = atan2(-dx, dy). To be confirmed in the game
// in phase 2-2 by logging CJ's facing direction.
inline float GtaHeadingFor(float dx, float dy)
{
    return std::atan2(-dx, dy);
}

// Heading for the skater's travel direction, or false if it's barely moving
// (THAW speed in the ground plane below minSpeed units per second).
inline bool GtaHeadingFromThawVelocity(const Vec3& thawVel, const Mapping& m, float minSpeed,
                                       float& heading)
{
    const float groundSpeed = std::sqrt(thawVel.x * thawVel.x + thawVel.z * thawVel.z);
    if (groundSpeed < minSpeed) return false;
    const Vec3 g = ThawDeltaToGta(thawVel, m);
    heading = GtaHeadingFor(g.x, g.y);
    return true;
}

} // namespace mp
