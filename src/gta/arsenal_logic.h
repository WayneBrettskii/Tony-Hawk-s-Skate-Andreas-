#pragma once
#include <cmath>
namespace mp::gta::arsenal {
inline int WheelSlot(float x, float y, int count, int previous) {
    if (count <= 0) return -1;
    if (x*x + y*y < 0.30f*0.30f) return previous >= 0 && previous < count ? previous : 0;
    float angle = std::atan2(x, -y);
    if (angle < 0) angle += 6.2831853f;
    return static_cast<int>(std::floor(angle * count / 6.2831853f + 0.5f)) % count;
}
inline bool WeaponType(int type) { return type >= 0 && type <= 46 && type != 19 && type != 20 && type != 21; }
inline int Wrap(int value, int count) { return count > 0 ? (value % count + count) % count : 0; }
}
