// PF-1（SPEC §8.6）：処理の重さを測る。合否には使わず、数値を記録するだけ。
#include "dsp/Engine.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

int main()
{
    const double fs = 48000.0;
    const int block = 16;
    const double seconds = 60.0;
    const auto blocks = std::size_t (fs * seconds / block);

    zfp::Engine e;
    if (! e.prepare (fs, 2, 2))
        return 1;

    float targets[zfp::kNumBands];
    for (int b = 0; b < zfp::kNumBands; ++b)
        targets[b] = (b % 5 == 0) ? -12.0f : 0.0f;
    e.setTestTargetsDb (targets);

    std::vector<float> l (block), r (block), ol (block), orr (block);
    std::uint32_t s = 99u;
    using clock = std::chrono::steady_clock;
    double total = 0.0, worst = 0.0;
    float sink = 0.0f;

    for (std::size_t i = 0; i < blocks; ++i)
    {
        for (int n = 0; n < block; ++n)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            l[size_t (n)] = 0.2f * (float (s >> 8) * (2.0f / 16777216.0f) - 1.0f);
            r[size_t (n)] = -l[size_t (n)] * 0.7f;
        }
        const float* in[2] = { l.data(), r.data() };
        float* out[2] = { ol.data(), orr.data() };

        const auto t0 = clock::now();
        e.process (in, out, block);
        const double us = std::chrono::duration<double, std::micro> (clock::now() - t0).count();
        total += us;
        worst = std::max (worst, us);
        sink += ol[0] + orr[0];
    }

    const double period = 1.0e6 * block / fs;
    std::printf ("PF-1  48 kHz / %d samples / Stereo / %.0f s\n", block, seconds);
    std::printf ("  average load : %.3f %% of realtime\n", 100.0 * total / (seconds * 1.0e6));
    std::printf ("  worst block  : %.1f us (%.1f %% of the %.0f us buffer period)\n",
                 worst, 100.0 * worst / period, period);
    std::printf ("  note         : EQ targets fixed for the measurement; analysis path is running. checksum %g\n", double (sink));
    return 0;
}
