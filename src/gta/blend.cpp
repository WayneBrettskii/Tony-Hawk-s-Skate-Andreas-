// my-passthrough: even motion from uneven reTHAWed frames. See blend.h.
// The code is skate mode's (5-3f, 5-3g, 5-3k), moved here unchanged.

#include "blend.h"

#include <cmath>

namespace mp::gta {
namespace {

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

void Norm3(float* v)
{
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

} // namespace

void RiderBlend::Clear()
{
    count_ = 0;
    haveSmooth_ = false;
    clock_ = 0;
}

// 5-3k: an even frame clock. Each frame it moves on by the average frame
// time (measured), and is only nudged a little towards the real time, so the
// moments at which we show reTHAWed are evenly spaced. Restarts after a hitch.
long long RiderBlend::FrameClock(long long now, long long freq)
{
    if (clock_ == 0) {
        clock_ = clockLast_ = now;
        avgFrame_ = freq / 60.0;
        return now;
    }
    const double real = static_cast<double>(now - clockLast_);
    clockLast_ = now;
    if (real > 0.0 && real < freq / 10.0) avgFrame_ += (real - avgFrame_) * 0.02; // average frame time
    long long next = clock_ + static_cast<long long>(avgFrame_);
    const double err = static_cast<double>(now - next);
    if (err > 3.0 * avgFrame_ || err < -3.0 * avgFrame_) next = now; // hitch: start again
    else next += static_cast<long long>(err * 0.05);                  // drift towards real time slowly
    clock_ = next;
    return next;
}

// 5-3g: blur the steps in the rider's orientation (see kOrientSmoothMs).
void RiderBlend::SmoothOrient(float* orient, long long nowQpc, long long qpcFreq)
{
    if (!haveSmooth_) {
        for (int k = 0; k < 9; ++k) smooth_[k] = orient[k];
        haveSmooth_ = true;
    } else {
        const float dtMs = static_cast<float>(static_cast<double>(nowQpc - smoothQpc_) * 1000.0 / qpcFreq);
        const float a = dtMs <= 0.0f ? 0.0f : 1.0f - std::exp(-dtMs / kOrientSmoothMs);
        for (int k = 3; k < 9; ++k) smooth_[k] += (orient[k] - smooth_[k]) * a; // up and facing
        float* u = smooth_ + 3;
        float* f = smooth_ + 6;
        Norm3(u);
        const float d = f[0] * u[0] + f[1] * u[1] + f[2] * u[2];
        for (int k = 0; k < 3; ++k) f[k] -= u[k] * d;
        Norm3(f);
        smooth_[0] = u[1] * f[2] - u[2] * f[1]; // right = up x facing (as reTHAWed's rows)
        smooth_[1] = u[2] * f[0] - u[0] * f[2];
        smooth_[2] = u[0] * f[1] - u[1] * f[0];
        for (int k = 0; k < 9; ++k) orient[k] = smooth_[k];
    }
    smoothQpc_ = nowQpc;
}

void RiderBlend::Push(const SkaterShared& s)
{
    if (s.frameQpc == 0) return;
    if (count_ > 0 && hist_[count_ - 1].qpc == s.frameQpc) {
        hist_[count_ - 1].s = s; // same frame, maybe read later: keep the newer read
        return;
    }
    if (count_ > 0 && s.frameQpc < hist_[count_ - 1].qpc) return; // older: ignore
    if (count_ == kHistory) {
        for (int i = 1; i < kHistory; ++i) hist_[i - 1] = hist_[i];
        --count_;
    }
    hist_[count_].s = s;
    hist_[count_].qpc = s.frameQpc;
    ++count_;
}

// The rider as he was at `when` (QueryPerformanceCounter time): blended from
// the two frames around it. Returns the blend in percent (0-100), or -1 if
// the newest (or oldest) frame was used as it is.
float RiderBlend::At(long long when, SkaterShared& out) const
{
    if (count_ == 0) return -1.0f;
    if (when >= hist_[count_ - 1].qpc) { out = hist_[count_ - 1].s; return -1.0f; }
    if (when <= hist_[0].qpc) { out = hist_[0].s; return -1.0f; }
    int i = 0;
    while (i + 1 < count_ && hist_[i + 1].qpc <= when) ++i;
    const SkaterShared& a = hist_[i].s;
    const SkaterShared& b = hist_[i + 1].s;
    const float t = static_cast<float>(when - hist_[i].qpc) / static_cast<float>(hist_[i + 1].qpc - hist_[i].qpc);
    out = b; // flags, level, sample numbers from the newer one
    for (int k = 0; k < 3; ++k) {
        out.pos[k] = Lerp(a.pos[k], b.pos[k], t);
        out.vel[k] = Lerp(a.vel[k], b.vel[k], t);
    }
    if ((a.flags & kPoseValid) && (b.flags & kPoseValid)) {
        for (int k = 0; k < 9; ++k) out.orient[k] = Lerp(a.orient[k], b.orient[k], t);
        // square it up again: up, then facing at right angles to it, right = up x facing
        float* r = out.orient;
        float* u = out.orient + 3;
        float* f = out.orient + 6;
        Norm3(u);
        const float d = f[0] * u[0] + f[1] * u[1] + f[2] * u[2];
        for (int k = 0; k < 3; ++k) f[k] -= u[k] * d;
        Norm3(f);
        r[0] = u[1] * f[2] - u[2] * f[1];
        r[1] = u[2] * f[0] - u[0] * f[2];
        r[2] = u[0] * f[1] - u[1] * f[0];
        for (int k = 0; k < 12; ++k) out.board[k] = Lerp(a.board[k], b.board[k], t);
        Norm3(out.board + 0);
        Norm3(out.board + 3);
        Norm3(out.board + 6);
    }
    if ((a.flags & kBodyValid) && (b.flags & kBodyValid))
        for (int k = 0; k < kLinkBodyBones * 3; ++k) out.body[k] = Lerp(a.body[k], b.body[k], t);
    return t * 100.0f;
}

float RiderBlend::Show(const SkaterShared& newest, SkaterShared& out)
{
    Push(newest);
    LARGE_INTEGER qf, qn;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&qn);
    // 5-3k: show reTHAWed at EVENLY spaced moments, one frame apart, not at
    // the moment this update happens to run: those come 13.5 / 19.5 ms apart
    // in turn even with the pacer (recording 14:35), while the picture reaches
    // the screen at an even rate.
    const long long when = FrameClock(qn.QuadPart, qf.QuadPart);
    SkaterShared b;
    const float pct = At(when - qf.QuadPart * kBlendDelayMs / 1000, b);
    if (count_ > 0) out = b;
    else if (&out != &newest) out = newest;
    if (out.flags & kPoseValid) SmoothOrient(out.orient, when, qf.QuadPart);
    return pct;
}

} // namespace mp::gta
