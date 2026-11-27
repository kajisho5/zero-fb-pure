// 小さな処理部品。DC Blocker（SPEC §4.2）とソフトリミッター（SPEC §4.7）。
#pragma once
#include "Constants.h"
#include <cmath>
#include <numbers>

namespace zfp
{
// 20 Hz・1 次ハイパス。
struct DcBlocker
{
    float coef  = 0.0f; // g / (1 + g)
    float state = 0.0f;

    void prepare (double sampleRate) noexcept
    {
        const double g = std::tan (std::numbers::pi * kDcBlockHz / sampleRate);
        coef  = float (g / (1.0 + g));
        state = 0.0f;
    }

    void reset() noexcept { state = 0.0f; }

    float tick (float x) noexcept
    {
        const float v  = (x - state) * coef;
        const float lp = v + state;
        state = lp + v;
        return x - lp;
    }
};

// 折れ点（-6.02 dBFS）までは完全に素通し。そこから上だけ tanh で丸め、天井（-0.5 dBFS）を超えない。
inline float softLimit (float v) noexcept
{
    const float mag = std::abs (v);
    if (mag <= kLimiterKnee)
        return v;

    const float range = kLimiterCeiling - kLimiterKnee;
    const float out   = kLimiterKnee + range * std::tanh ((mag - kLimiterKnee) / range);
    return std::copysign (out, v);
}
} // namespace zfp
