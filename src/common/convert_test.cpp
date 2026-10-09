// Unit test for convert.h. Builds as a small console program; prints each
// check and ends with "ALL PASSED" or the number of failures (exit code 1).
//   cloud/Linux:  g++ -std=c++17 src/common/convert_test.cpp -o convert_test && ./convert_test
//   Windows:      build\Release\convert_test.exe  (built by CMakeLists.txt)

#include "convert.h"

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;
const float kPi = 3.14159265358979f;

void Check(const char* what, float got, float expected, float tolerance = 1e-4f)
{
    const bool ok = std::fabs(got - expected) <= tolerance;
    std::printf("%s  %-55s got %10.5f expected %10.5f\n", ok ? "PASS" : "FAIL", what, got, expected);
    if (!ok) ++g_failures;
}

void CheckTrue(const char* what, bool value)
{
    std::printf("%s  %s\n", value ? "PASS" : "FAIL", what);
    if (!value) ++g_failures;
}

} // namespace

int main()
{
    using mp::Vec3;
    const mp::Mapping m;          // scale 0.0254, ySign +1
    mp::Mapping mirrored;
    mirrored.ySign = -1.0f;

    // Axis mapping of a movement: THAW (x, y up, z) -> GTA (x, y, z up).
    const Vec3 d = mp::ThawDeltaToGta(Vec3{100.0f, 50.0f, 200.0f}, m);
    Check("delta: THAW x 100 -> GTA x (m)", d.x, 2.54f);
    Check("delta: THAW z 200 -> GTA y (m)", d.y, 5.08f);
    Check("delta: THAW y (up) 50 -> GTA z (up) (m)", d.z, 1.27f);
    const Vec3 dm = mp::ThawDeltaToGta(Vec3{100.0f, 50.0f, 200.0f}, mirrored);
    Check("delta mirrored: GTA y flips sign", dm.y, -5.08f);
    Check("delta mirrored: GTA x unchanged", dm.x, 2.54f);

    // Position relative to anchors.
    const Vec3 thawAnchor{-2336.49f, 96.38f, -2482.72f}; // a real phase 1-1 sample
    const Vec3 gtaAnchor{1500.0f, -2400.0f, 13.5f};
    const Vec3 p0 = mp::MapPosition(thawAnchor, thawAnchor, gtaAnchor, m);
    Check("anchor maps to GTA anchor x", p0.x, gtaAnchor.x);
    Check("anchor maps to GTA anchor y", p0.y, gtaAnchor.y);
    Check("anchor maps to GTA anchor z", p0.z, gtaAnchor.z);
    // Skating ~500 units along THAW z for 1 s (phase 1-1 data): ~12.7 m along GTA y.
    const Vec3 p1 = mp::MapPosition(Vec3{-2336.49f, 96.38f, -1982.72f}, thawAnchor, gtaAnchor, m);
    Check("500 units along THAW z -> GTA y + 12.7 m", p1.y - gtaAnchor.y, 12.7f, 1e-3f);
    Check("... GTA x unchanged", p1.x - gtaAnchor.x, 0.0f, 1e-3f);
    // An ollie 75 units up -> 1.905 m up in GTA.
    const Vec3 p2 = mp::MapPosition(Vec3{-2336.49f, 171.38f, -2482.72f}, thawAnchor, gtaAnchor, m);
    Check("ollie 75 units up -> GTA z + 1.905 m", p2.z - gtaAnchor.z, 1.905f, 1e-3f);

    // GTA heading convention: 0 = +Y (north), +X (east) = -90 degrees.
    Check("heading facing +Y", mp::GtaHeadingFor(0.0f, 1.0f), 0.0f);
    Check("heading facing +X", mp::GtaHeadingFor(1.0f, 0.0f), -kPi / 2.0f);
    Check("heading facing -X", mp::GtaHeadingFor(-1.0f, 0.0f), kPi / 2.0f);

    // Heading from the skater's velocity.
    float h = 99.0f;
    CheckTrue("moving along THAW +z gives a heading",
              mp::GtaHeadingFromThawVelocity(Vec3{0.0f, 0.0f, 500.0f}, m, 20.0f, h));
    Check("... which faces GTA +Y", h, 0.0f);
    CheckTrue("moving along THAW +x gives a heading",
              mp::GtaHeadingFromThawVelocity(Vec3{500.0f, 0.0f, 0.0f}, m, 20.0f, h));
    Check("... which faces GTA +X", h, -kPi / 2.0f);
    CheckTrue("barely moving (5 < 20) gives no heading",
              !mp::GtaHeadingFromThawVelocity(Vec3{3.0f, 0.0f, 4.0f}, m, 20.0f, h));
    CheckTrue("falling straight down gives no heading",
              !mp::GtaHeadingFromThawVelocity(Vec3{0.0f, -400.0f, 0.0f}, m, 20.0f, h));

    // Phase 4-0: Los Santos East, the three landmarks marked in both games.
    {
        const mp::Vec3 thaw[3] = {{7694.87f, -515.66f, 2136.50f}, {9748.89f, -370.69f, 1063.16f},
                                  {3640.20f, -513.87f, 14186.43f}};
        const mp::Vec3 gtaFeet[3] = {{1976.103f, -1449.686f, 12.484f}, {2024.917f, -1423.471f, 15.992f},
                                     {1877.469f, -1741.686f, 12.522f}};
        const char* names[3] = {"LSE landmark 1 (skatepark corner)", "LSE landmark 2 (hospital steps)",
                                "LSE landmark 3 (night club doors)"};
        const mp::LevelAlignment* a = mp::FindLevelAlignment(0x0274F7DEul);
        Check("LSE: level ID found", a ? 1.0f : 0.0f, 1.0f);
        for (int k = 0; a && k < 3; ++k) {
            const mp::Vec3 g = mp::MapAbsolute(thaw[k], *a);
            const float dx = g.x - gtaFeet[k].x, dy = g.y - gtaFeet[k].y;
            Check(names[k], std::sqrt(dx * dx + dy * dy), 0.0f, 0.6f);   // within 0.6 m sideways
            Check(names[k], g.z, gtaFeet[k].z, 0.05f);                    // within 5 cm in height
        }
        Check("other level: no alignment", mp::FindLevelAlignment(0x12345678ul) ? 1.0f : 0.0f, 0.0f);
        // Our own map (tools/gta_to_thaw_map.py): its start point, written at THAW
        // (43.3071, 502.1263, -2964.5686), is GTA (1976.1, -1449.7, 12.5) + 10 units up.
        const mp::LevelAlignment* own = mp::FindLevelAlignment(0x9F014B98ul);
        Check("own map: level ID found", own ? 1.0f : 0.0f, 1.0f);
        if (own) {
            const mp::Vec3 g = mp::MapAbsolute(mp::Vec3{43.3071f, 502.1263f - 10.0f, -2964.5686f}, *own);
            Check("own map: start point x", g.x, 1976.1f, 0.01f);
            Check("own map: start point y", g.y, -1449.7f, 0.01f);
            Check("own map: start point z", g.z, 12.5f, 0.01f);
        }
    }

    if (g_failures == 0) {
        std::printf("ALL PASSED\n");
        return 0;
    }
    std::printf("%d FAILED\n", g_failures);
    return 1;
}
