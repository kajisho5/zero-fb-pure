#include "Decoupler.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace zfp
{
double Decoupler::bandResponseDb (const BandLayout& layout, int j, double gainDb,
                                  double fHz, double sampleRate) noexcept
{
    const double a  = std::pow (10.0, gainDb / 40.0);
    const double qj = layout.q[size_t (j)];
    const double w  = std::tan (std::numbers::pi * fHz / sampleRate) / layout.g[size_t (j)];
    const double c  = (1.0 - w * w) * (1.0 - w * w);
    const double num = c + (a * w / qj) * (a * w / qj);
    const double den = c + (w / (qj * a)) * (w / (qj * a));
    return 10.0 * std::log10 (num / den);
}

void Decoupler::build (const BandLayout& layout, double sampleRate) noexcept
{
    const double ga = kDecoupleRefA, gc = kDecoupleRefC;

    for (int b = 0; b < kNumBands; ++b)
    {
        for (int j = 0; j < kNumBands; ++j)
        {
            if (b == j)
            {
                alpha[b][j] = 1.0f;
                beta[b][j]  = 0.0f;
                continue;
            }

            const double f    = layout.fc[size_t (b)];
            const double rhoA = bandResponseDb (layout, j, ga, f, sampleRate) / ga;
            const double rhoC = bandResponseDb (layout, j, gc, f, sampleRate) / gc;
            const double bt   = (rhoC - rhoA) / (std::abs (gc) - std::abs (ga));
            alpha[b][j] = float (rhoA - bt * std::abs (ga));
            beta[b][j]  = float (bt);
        }
    }
}

void Decoupler::solve (const float* targetDb, float* gainDb, int iterations) const noexcept
{
    for (int it = 0; it < iterations; ++it)
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            // 自分以外のバンドからの漏れ込み。対角は alpha=1, beta=0 なので、全部足してから自分の分を引く。
            float leak = 0.0f;
            for (int j = 0; j < kNumBands; ++j)
                leak += gainDb[j] * (alpha[b][j] + beta[b][j] * std::abs (gainDb[j]));
            leak -= gainDb[b];

            gainDb[b] = std::min (0.0f, targetDb[b] - leak);
        }
    }
}
} // namespace zfp
