#include "Fft.h"
#include <cmath>
#include <numbers>
#include <utility>

#if defined(__GNUC__) || defined(__clang__)
 #define ZFP_RESTRICT __restrict__
#elif defined(_MSC_VER)
 #define ZFP_RESTRICT __restrict
#else
 #define ZFP_RESTRICT
#endif

namespace zfp
{
void Fft::prepare (int size)
{
    n = size;
    swapA.clear();
    swapB.clear();
    twRe.clear();
    twIm.clear();
    stageOffset.clear();

    int bits = 0;
    while ((1 << bits) < n)
        ++bits;

    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        if (i < r)
        {
            swapA.push_back (i);
            swapB.push_back (r);
        }
    }

    for (int len = 2; len <= n; len <<= 1)
    {
        stageOffset.push_back (int (twRe.size()));
        const int half = len >> 1;
        for (int k = 0; k < half; ++k)
        {
            const double a = -2.0 * std::numbers::pi * double (k) / double (len);
            twRe.push_back (float (std::cos (a)));
            twIm.push_back (float (std::sin (a)));
        }
    }
}

void Fft::forward (float* reIn, float* imIn) const noexcept
{
    float* ZFP_RESTRICT re = reIn;
    float* ZFP_RESTRICT im = imIn;

    for (size_t i = 0; i < swapA.size(); ++i)
    {
        std::swap (re[swapA[i]], re[swapB[i]]);
        std::swap (im[swapA[i]], im[swapB[i]]);
    }

    int stage = 0;
    for (int len = 2; len <= n; len <<= 1, ++stage)
    {
        const int half = len >> 1;
        const float* ZFP_RESTRICT wr = twRe.data() + stageOffset[size_t (stage)];
        const float* ZFP_RESTRICT wi = twIm.data() + stageOffset[size_t (stage)];

        for (int start = 0; start < n; start += len)
        {
            float* ZFP_RESTRICT lr = re + start;
            float* ZFP_RESTRICT li = im + start;
            float* ZFP_RESTRICT hr = lr + half;
            float* ZFP_RESTRICT hi = li + half;
            for (int k = 0; k < half; ++k)
            {
                const float tr = hr[k] * wr[k] - hi[k] * wi[k];
                const float ti = hr[k] * wi[k] + hi[k] * wr[k];
                const float ar = lr[k], ai = li[k];
                lr[k] = ar + tr;
                li[k] = ai + ti;
                hr[k] = ar - tr;
                hi[k] = ai - ti;
            }
        }
    }
}
} // namespace zfp
