// ZERO-FB PURE の信号経路（遅延 0 サンプル）。SPEC §4。
// JUCE に依存しない。process() の中ではメモリ確保・ロックを行わない。
#pragma once
#include "BandLayout.h"
#include "Decoupler.h"
#include "Primitives.h"
#include <array>
#include <atomic>

namespace zfp
{
class Engine
{
public:
    enum class GuardState { normal, muted, recover };

    // 入力 1ch→出力 1ch、2ch→2ch、1ch→2ch に対応。対応できない設定なら false。
    bool prepare (double sampleRate, int numInputChannels, int numOutputChannels) noexcept;

    // どのスレッドから呼んでもよい（ロックなし）。
    void setStrengthPercent (float percent) noexcept { strength.store (percent, std::memory_order_relaxed); }
    void setFloorDb (float db) noexcept              { floorDb.store (db, std::memory_order_relaxed); }
    void setMute (bool on) noexcept                  { mute.store (on, std::memory_order_relaxed); }
    void setBypass (bool on) noexcept                { bypass.store (on, std::memory_order_relaxed); }
    void requestReset() noexcept                     { resetRequests.fetch_add (1, std::memory_order_relaxed); }

    // オーディオスレッドから呼ぶ。in と out は同じバッファでもよい。
    void process (const float* const* in, float* const* out, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return 0; }
    int getHopSize() const noexcept        { return hop; }
    double getSampleRate() const noexcept  { return fs; }
    const BandLayout& getLayout() const noexcept { return layout; }

    // ---- テスト用の口（SPEC §8.2）。製品の画面からは触れない。process() と同じスレッドから呼ぶこと。
    // バンド中心での目標応答 [dB] を外から固定する。nullptr で解除。
    void setTestTargetsDb (const float* targetsDb) noexcept;
    GuardState getGuardState() const noexcept { return guard; }
    float getGuardFade() const noexcept       { return guardFade; }
    const float* getBandGainsDb() const noexcept { return gainDb.data(); }

private:
    void controlFrame() noexcept;
    void computeTargetCoefs() noexcept;
    void snapCoefsToTargets() noexcept;
    void clearFilterStates() noexcept;
    void enterMuted() noexcept;
    void beginRecover() noexcept;
    void applyReset() noexcept;

    // 設定
    double fs = 48000.0;
    int numIn = 1, numOut = 1;
    int hop = 128;
    int fadeSamples = 240;
    float fadeStep = 1.0f / 240.0f;
    float guardRelStep = 0.0f;

    BandLayout layout;
    Decoupler decoupler;

    // パラメータ
    std::atomic<float> strength { 75.0f };
    std::atomic<float> floorDb { -24.0f };
    std::atomic<bool> mute { false };
    std::atomic<bool> bypass { false };
    std::atomic<unsigned> resetRequests { 0 };
    unsigned resetHandled = 0;

    // SVF の係数（現在値・1 サンプルあたりの増分・目標値）。SPEC §4.4, §4.5
    template <typename T> using BandArray = std::array<T, kNumBands>;
    alignas (32) BandArray<float> a1 {}, a2 {}, a3 {}, m1 {};
    alignas (32) BandArray<float> da1 {}, da2 {}, da3 {}, dm1 {};
    alignas (32) BandArray<float> ta1 {}, ta2 {}, ta3 {}, tm1 {};
    int rampLeft = 0;

    // フィルタの状態
    alignas (32) float ic1[kMaxChannels][kNumBands] {};
    alignas (32) float ic2[kMaxChannels][kNumBands] {};
    std::array<DcBlocker, kMaxChannels> dc {};

    // 制御
    alignas (32) BandArray<float> gainDb {};    // 解いた各バンドのゲイン
    alignas (32) BandArray<float> targetDb {};  // 作業用
    alignas (32) BandArray<float> testTargets {};
    bool useTestTargets = false;
    int hopCount = 0;

    // 異常ガード  SPEC §4.6
    GuardState guard = GuardState::normal;
    int guardClean = 0;
    float guardFade = 1.0f;
    float guardRel = 0.0f; // 1→0。復帰直後に全バンドを深く切っておく度合い

    // バイパスのクロスフェードと MUTE  SPEC §4.1, §4.8
    float wet = 1.0f;
    float muteGain = 1.0f;
};
} // namespace zfp
