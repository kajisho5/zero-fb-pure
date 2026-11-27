// テスト共通の道具。信号はすべてコードで作る（録音素材は使わない）。乱数の種は固定。
#pragma once
#include "dsp/Engine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace zfptest
{
using Buffer = std::vector<std::vector<float>>; // [チャンネル][サンプル]

// 種を固定した白色雑音（-1〜1 の一様乱数 × amplitude）。
inline std::vector<float> whiteNoise (std::size_t n, float amplitude, std::uint32_t seed)
{
    std::vector<float> v (n);
    std::uint32_t s = seed;
    for (auto& x : v)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        x = amplitude * (float (s >> 8) * (2.0f / 16777216.0f) - 1.0f);
    }
    return v;
}

inline std::vector<float> sine (std::size_t n, double freq, double fs, float amplitude)
{
    std::vector<float> v (n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = amplitude * float (std::sin (2.0 * std::numbers::pi * freq * double (i) / fs));
    return v;
}

// [begin, end) を blockSize ごとに区切って処理する。
inline void processRange (zfp::Engine& e, const Buffer& in, Buffer& out,
                          std::size_t begin, std::size_t end, std::size_t blockSize)
{
    for (std::size_t pos = begin; pos < end; pos += blockSize)
    {
        const std::size_t n = std::min (blockSize, end - pos);
        const float* inPtr[2]  = { in[0].data() + pos, in.size() > 1 ? in[1].data() + pos : nullptr };
        float* outPtr[2] = { out[0].data() + pos, out.size() > 1 ? out[1].data() + pos : nullptr };
        e.process (inPtr, outPtr, int (n));
    }
}

inline Buffer makeBuffer (std::size_t channels, std::size_t n)
{
    return Buffer (channels, std::vector<float> (n, 0.0f));
}

// [begin, end) の区間で、周波数 freq の成分の振幅を求める（直交検波）。
inline double toneAmplitude (const std::vector<float>& v, std::size_t begin, std::size_t end,
                             double freq, double fs)
{
    double re = 0.0, im = 0.0;
    for (std::size_t i = begin; i < end; ++i)
    {
        const double ph = 2.0 * std::numbers::pi * freq * double (i) / fs;
        re += double (v[i]) * std::cos (ph);
        im += double (v[i]) * std::sin (ph);
    }
    return 2.0 * std::sqrt (re * re + im * im) / double (end - begin);
}

inline double toDb (double ratio) { return 20.0 * std::log10 (ratio); }
} // namespace zfptest
