// オーディオスレッドから画面へ値を渡す入れ物。SPEC §6.4。
// すべて std::atomic（ロックなし・確保なし）。画面は 30 回/秒で読むだけなので、項目どうしの厳密な同時性は求めない。
#pragma once
#include "dsp/Constants.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace zfp
{
struct UiState
{
    std::array<std::atomic<float>, kNumBands> bandGainDb {};
    std::array<std::atomic<float>, kNumBands> bandScore {};
    std::array<std::atomic<std::uint8_t>, kNumBands> bandState {};   // Analyzer::BandState
    std::array<std::atomic<float>, kNumNotches> notchFreqHz {};
    std::array<std::atomic<float>, kNumNotches> notchGainDb {};
    std::array<std::atomic<std::uint8_t>, kNumNotches> notchState {};
    std::atomic<float> holdSec { 0.0f };
    std::atomic<std::uint8_t> guard { 0 };                           // Engine::GuardState
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> numInputs { 1 }, numOutputs { 1 };
    std::atomic<bool> running { false };                             // 対応できる設定で prepare 済みか

    // メーター用のピーク（振幅）。オーディオ側は「大きければ上書き」、画面側は読んで 0 に戻す。
    std::array<std::atomic<float>, kMaxChannels> inPeak {}, outPeak {};

    static void pushPeak (std::atomic<float>& slot, float value) noexcept
    {
        if (value > slot.load (std::memory_order_relaxed))
            slot.store (value, std::memory_order_relaxed);
    }
};
} // namespace zfp
