// 帯域どうしの裾の重なりを補正する。SPEC §5.10。
// 「バンド中心での合計応答が目標どおりになる」各バンドのゲイン（0 dB 以下）を解く。
#pragma once
#include "BandLayout.h"

namespace zfp
{
class Decoupler
{
public:
    // バンド j を gainDb にしたときの、周波数 fHz での応答 [dB]（1 バンド分の正確な式）。
    static double bandResponseDb (const BandLayout& layout, int j, double gainDb,
                                  double fHz, double sampleRate) noexcept;

    void build (const BandLayout& layout, double sampleRate) noexcept;

    // targetDb: バンド中心での目標応答（0 以下）。gainDb: 前回の解を入れて渡し、新しい解を受け取る。
    void solve (const float* targetDb, float* gainDb, int iterations) const noexcept;

private:
    alignas (32) float alpha[kNumBands][kNumBands] {};
    alignas (32) float beta[kNumBands][kNumBands] {};
};
} // namespace zfp
