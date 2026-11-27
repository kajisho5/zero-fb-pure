// EQ バンドの中心周波数と Q を決める。SPEC §4.3。
#pragma once
#include "Constants.h"
#include <array>

namespace zfp
{
struct BandLayout
{
    std::array<double, kNumBands> fc {};  // 中心周波数 [Hz]
    std::array<double, kNumBands> q {};   // Q
    std::array<double, kNumBands> g {};   // tan(pi * fc / fs)

    // 対応できるサンプルレートかどうか（15 kHz がナイキストの 0.49 倍以内に収まること）。
    static bool isSampleRateSupported (double sampleRate) noexcept;

    // 表を計算する。対応できないレートなら false を返し、中身は変えない。
    bool compute (double sampleRate) noexcept;
};
} // namespace zfp
