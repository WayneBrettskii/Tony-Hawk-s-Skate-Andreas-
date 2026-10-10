// my-passthrough: even motion from uneven reTHAWed frames (docs/DESIGN.md
// 8.19, 8.20, 8.29 B5). Moved out of skatemode.cpp unchanged so BMX mode can
// use it too.
//
// reTHAWed frames reach GTA unevenly (some GTA frames get none, the next two).
// So: keep the last few frames, and each GTA frame show the rider as he was
// kBlendDelayMs ago, blended between the two frames around that moment, at
// evenly spaced moments (an even frame clock). The rider's orientation is also
// smoothed a little (on curved ramps it changes in steps).

#pragma once

#include <windows.h>

#include "../common/link.h"

namespace mp::gta {

class RiderBlend {
public:
    // Forget everything (mode switched off / on).
    void Clear();

    // Keep `newest` and fill `out` with what to show this frame. `out` may be
    // `newest` itself. Returns the blend in percent (0-100), or -1 if a frame
    // was used as it is.
    float Show(const SkaterShared& newest, SkaterShared& out);

    int Count() const { return count_; }

private:
    static constexpr long long kBlendDelayMs = 40;
    static constexpr int kHistory = 8;
    static constexpr float kOrientSmoothMs = 40.0f;

    struct Hist {
        SkaterShared s;
        long long qpc = 0;
    };

    long long FrameClock(long long now, long long freq);
    void SmoothOrient(float* orient, long long nowQpc, long long qpcFreq);
    void Push(const SkaterShared& s);
    float At(long long when, SkaterShared& out) const;

    Hist hist_[kHistory];
    int count_ = 0;
    bool haveSmooth_ = false;
    float smooth_[9] = {};
    long long smoothQpc_ = 0;
    long long clock_ = 0, clockLast_ = 0;
    double avgFrame_ = 0.0;
};

} // namespace mp::gta
