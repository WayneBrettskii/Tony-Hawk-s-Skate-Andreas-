// my-passthrough: converting reTHAWed (THAW) positions into GTA San Andreas ones.
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
// turn right.

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
