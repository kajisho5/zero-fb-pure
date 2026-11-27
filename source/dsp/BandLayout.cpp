#include "BandLayout.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace zfp
{
namespace
{
double erbRate (double f) noexcept        { return 21.4 * std::log10 (1.0 + 0.00437 * f); }
double erbRateInverse (double e) noexcept { return (std::pow (10.0, e / 21.4) - 1.0) / 0.00437; }
}

bool BandLayout::isSampleRateSupported (double sampleRate) noexcept
{
    return std::isfinite (sampleRate) && kBandHighHz < 0.49 * sampleRate;
}

bool BandLayout::compute (double sampleRate) noexcept
{
    if (! isSampleRateSupported (sampleRate))
        return false;

    const double eLo  = erbRate (kBandLowHz);
    const double eHi  = erbRate (kBandHighHz);
    const double step = (eHi - eLo) / double (kNumBands - 1);

    // 両端の外側に仮想の隣を 1 本ずつ置く（添字 0 と kNumBands+1）。
    std::array<double, kNumBands + 2> ext {};
    for (int i = 0; i < kNumBands + 2; ++i)
        ext[size_t (i)] = erbRateInverse (eLo + double (i - 1) * step);

    const double pi = std::numbers::pi;

    for (int b = 0; b < kNumBands; ++b)
    {
        const double f  = ext[size_t (b + 1)];
        const double gb = std::tan (pi * f / sampleRate);

        double widest = 0.0;
        for (const double neighbour : { ext[size_t (b)], ext[size_t (b + 2)] })
        {
            const double fMid = std::min (0.5 * (f + neighbour), 0.49 * sampleRate);
            const double w    = std::tan (pi * fMid / sampleRate) / gb;
            widest = std::max (widest, std::abs (w - 1.0 / w));
        }

        fc[size_t (b)] = f;
        g[size_t (b)]  = gb;
        q[size_t (b)]  = 1.0 / (kKappa * widest);
    }

    return true;
}
} // namespace zfp
