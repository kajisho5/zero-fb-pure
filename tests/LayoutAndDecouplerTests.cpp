// 帯域表（SPEC §4.3）と干渉補正（SPEC §5.10）の単体テスト。
#include "TestHelpers.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using Catch::Approx;

TEST_CASE ("帯域表が設計書の参考値と合う", "[layout]")
{
    zfp::BandLayout l;
    REQUIRE (l.compute (48000.0));
    CHECK (l.fc[0]  == Approx (60.0).margin (0.05));
    CHECK (l.fc[15] == Approx (1098.4).margin (0.05));
    CHECK (l.fc[39] == Approx (15000.0).margin (0.05));
    CHECK (l.q[0]   == Approx (1.55).margin (0.005));
    CHECK (l.q[20]  == Approx (7.10).margin (0.005));
    CHECK (l.q[39]  == Approx (3.51).margin (0.005));

    REQUIRE (l.compute (44100.0));
    CHECK (l.q[39] == Approx (2.90).margin (0.005));
    REQUIRE (l.compute (96000.0));
    CHECK (l.q[39] == Approx (6.60).margin (0.005));

    for (int b = 1; b < zfp::kNumBands; ++b)
        CHECK (l.fc[size_t (b)] > l.fc[size_t (b - 1)]);
}

TEST_CASE ("対応できないサンプルレートは断る", "[layout]")
{
    zfp::BandLayout l;
    CHECK_FALSE (l.compute (22050.0));
    CHECK_FALSE (l.compute (0.0));
    zfp::Engine e;
    CHECK_FALSE (e.prepare (22050.0, 2, 2));
    CHECK_FALSE (e.prepare (48000.0, 2, 1));
    CHECK_FALSE (e.prepare (48000.0, 3, 3));
}

TEST_CASE ("干渉補正の解は 0 dB 以下で、目標より深くならない", "[decoupler]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    zfp::BandLayout l;
    REQUIRE (l.compute (fs));
    zfp::Decoupler d;
    d.build (l, fs);

    std::uint32_t s = 20261127u;
    auto next = [&s] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return float (s >> 8) / 16777216.0f; };

    double worstError = 0.0;
    for (int trial = 0; trial < 200; ++trial)
    {
        float target[zfp::kNumBands] = {};
        const int active = 1 + int (next() * 7.0f);
        for (int i = 0; i < active; ++i)
            target[int (next() * 39.99f)] = -3.0f - 21.0f * next();

        float gain[zfp::kNumBands] = {};
        d.solve (target, gain, 3);

        for (int b = 0; b < zfp::kNumBands; ++b)
        {
            REQUIRE (gain[b] <= 0.0f);
            REQUIRE (gain[b] >= target[b] - 1.0e-4f);
        }

        // 実際の合計応答（各バンドの正確な式の和）と目標の差を、ゲインが 0 でないバンドで確かめる。
        for (int b = 0; b < zfp::kNumBands; ++b)
        {
            if (gain[b] == 0.0f)
                continue;
            double total = 0.0;
            for (int j = 0; j < zfp::kNumBands; ++j)
                if (gain[j] != 0.0f)
                    total += zfp::Decoupler::bandResponseDb (l, j, double (gain[j]), l.fc[size_t (b)], fs);
            worstError = std::max (worstError, std::abs (total - double (target[b])));
        }
    }
    INFO ("最大誤差 [dB] = " << worstError);
    CHECK (worstError < 0.6); // 設計書の検算値は 0.44 dB
}
