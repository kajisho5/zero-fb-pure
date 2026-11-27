// Control Path：入力を解析して、バンドごとの「抑制の深さ e（0〜1）」を決める。SPEC §5。
// frame() はオーディオスレッドから呼ぶ。確保は prepare() の中だけ。
#pragma once
#include "BandLayout.h"
#include "Fft.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace zfp
{
// 検出と保持の定数（SPEC §5.5〜§5.8）。時間は秒。
struct Tuning
{
    float preHighpassHz   = 40.0f;
    float analysisLowHz   = 50.0f;
    float analysisHighHz  = 16000.0f;
    float gateDb          = -72.0f;  // 絶対ゲート
    float prominenceDb    = 12.0f;   // 突出度
    // 倍音列の判定（声や楽器の倍音を発振と間違えないため）。SPEC §5.5
    float partialProminenceDb = 3.0f; // 倍音の候補とみなす山の高さ（±2 ビン離れた所との差）
    float combPartnerDb   = 15.0f;   // 候補より何 dB 下までを仲間の倍音とみなすか
    float combMinHz       = 70.0f;   // 倍音の間隔の下限
    float combMaxHz       = 1200.0f; // 倍音の間隔の上限
    float emergencyLevelDb = -12.0f; // 緊急の例外
    float emergencyProminenceDb = 20.0f;
    float drawdownDb      = 3.0f;    // 落ち込み量の上限
    float scoreUpSec      = 0.080f;  // スコア 0→1（通常）
    float scoreUpAlertSec = 0.016f;  // スコア 0→1（警戒中）
    float scoreDownSec    = 0.400f;  // スコア 1→0
    // 「伸び続けている」＝ growthFrames フレームの区間を 2 つ続けて見て、どちらも growthMinDb〜growthMaxDb だけ上がっている。
    // 声の立ち上がり（一気に上がって止まる）と、発振（同じ調子で上がり続ける）を分けるための条件。
    float growthMinDb     = 3.0f;
    float growthMaxDb     = 30.0f;
    float growthSteadiness = 0.6f;   // 新しい区間の伸びが、古い区間の伸びのこの割合以上（失速していない）
    int   growthFrames    = 8;       // 1 区間の長さ（48 kHz で約 43 ms）
    float deepenSec       = 0.080f;  // 発振が伸びている間、深さ 0→1 に進む時間
    float deepenFastSec   = 0.060f;  // 音量が大きいとき（deepenFastLevelDb 以上）の速い進み方
    float deepenFastLevelDb = 0.0f;   // 0 dBFS 以上＝実質的に使わない（調整の余地として残す）
    float risingSlackDb   = 0.6f;    // 1 フレームでこれ以上下がったら「収まり始めた」とみなす
    float freqToleranceHz = 4.0f;    // ならした周波数がこの幅を超えて動いたら「同じ音ではない」とみなす
    float freqWobbleHz    = 16.0f;   // その場の周波数の揺れはこの幅まで許す（近すぎる 2 本の発振のうなり）
    float notchMatchRatio = 0.0175f; // ノッチと山を同じものとみなす周波数の差（割合。約 1/40 オクターブ）
    float notchMatchMinHz = 6.0f;    // 同上（下限）
    float settleBack      = 0.60f;   // 保持に移るとき、深さをこの割合に戻す（行きすぎた分を返す）
    float holdSpread      = 0.5f;    // ノッチの保持の長さを、番号に応じて最大この割合だけ延ばす
    float notchQuietSec   = 0.30f;   // 発振が確認できない時間がこれだけ続いたら保持に移る
    float alertSec        = 1.5f;
    float engageOn        = 0.50f;
    float engageOff       = 0.25f;
    float holdBaseSec     = 15.0f;
    int   holdMaxDoublings = 3;
    float holdMaxSec      = 120.0f;
    float releaseSec      = 8.0f;
    float retriggerSec    = 10.0f;
    float forgetSec       = 60.0f;
};

class Analyzer
{
public:
    enum class BandState : std::uint8_t { idle, engaged, hold, release };

    void prepare (double sampleRate, int numChannels, int hopSize, const BandLayout& layout);
    void reset() noexcept;
    void setTuning (const Tuning& t) noexcept;

    // 1 サンプルずつ入力を渡す（ch1 はモノのとき無視）。
    void push (float ch0, float ch1) noexcept
    {
        pre[0] += preCoef * (ch0 - pre[0]);
        ring[0][size_t (writePos)] = ch0 - pre[0];
        if (channels == 2)
        {
            pre[1] += preCoef * (ch1 - pre[1]);
            ring[1][size_t (writePos)] = ch1 - pre[1];
        }
        writePos = (writePos + 1) & (fftSize - 1);
    }

    // ホップごとに 1 回呼ぶ。バンドごとの深さ e（0〜1）を更新する。
    void frame() noexcept;

    const float* depths() const noexcept { return depth.data(); }
    // ノッチ n の周波数 [Hz]（未使用なら 0）と深さ（0〜1）。
    float notchFreqHz (int n) const noexcept { return notches[size_t (n)].freqHz; }
    float notchDepth (int n) const noexcept  { return notches[size_t (n)].depth; }
    BandState notchState (int n) const noexcept { return notches[size_t (n)].state; }
    const float* bandScores() const noexcept { return bandScore.data(); }
    BandState bandState (int b) const noexcept { return state[size_t (b)]; }
    bool alertActive() const noexcept { return alertLeft > 0.0f; }
    float peakHz() const noexcept { return strongestHz; }
    int getFftSize() const noexcept { return fftSize; }
    // 保持（Hold）中のバンド・ノッチのうち、いちばん長く残っている残り時間 [秒]。無ければ 0。画面表示用。
    float longestHoldSec() const noexcept
    {
        float longest = 0.0f;
        for (int b = 0; b < kNumBands; ++b)
            if (state[size_t (b)] == BandState::hold)
                longest = std::max (longest, holdLeft[size_t (b)]);
        for (const auto& n : notches)
            if (n.state == BandState::hold)
                longest = std::max (longest, n.holdLeft);
        return longest;
    }

private:
    void stepBand (int b, float s, bool howlNow, bool loud) noexcept;
    void stepNotches() noexcept;

    struct Notch
    {
        float freqHz = 0.0f, depth = 0.0f, holdLeft = 0.0f, quietFor = 0.0f, idleFor = 1.0e9f;
        BandState state = BandState::idle;
        std::uint8_t strikes = 0;
        bool matched = false;
    };
    std::array<Notch, kNumNotches> notches {};

    Tuning tune;
    double fs = 48000.0;
    int channels = 1, hop = 256, fftSize = 2048, half = 1024;
    float frameSec = 0.0f, binHz = 0.0f, norm2 = 1.0f;
    int kLo = 1, kHi = 1, combMin = 3, combMax = 51;
    float stepUp = 0, stepUpAlert = 0, stepDown = 0, releaseStep = 0, deepenStep = 0, deepenFastStep = 0;

    Fft fft;
    std::vector<float> window;
    std::array<std::vector<float>, 2> ring;
    std::array<float, 2> pre { 0.0f, 0.0f };
    float preCoef = 0.0f;
    int writePos = 0;

    std::vector<float> workRe, workIm;
    std::vector<float> power, level, score, smooth, runMax, runStart, lastL5, peakFreq, runFmin, runFmax, runFlo, runFhi, runFavg;
    std::vector<double> prefix;
    std::vector<std::int32_t> streak;
    std::vector<std::uint8_t> peak, confirmedNow, handled;
    std::vector<int> localWidth, partials;
    // 直近のレベルの履歴（伸び続けているかどうかを見るため）。history[f] は 1 フレーム分。
    static constexpr int kMaxHistory = 24;
    std::array<std::vector<float>, kMaxHistory> history;
    int historyPos = 0;

    bool isGrowing (int k) const noexcept;
    std::vector<float> prominence;

    bool inHarmonicSeries (int k, float levelDb) const noexcept;

    std::array<int, kNumBands> bandFirst {}, bandLast {};
    std::array<float, kNumBands> bandScore {}, depth {}, holdLeft {}, idleFor {};
    std::array<BandState, kNumBands> state {};
    std::array<std::uint8_t, kNumBands> strikes {}, everEngaged {};
    float alertLeft = 0.0f;
    float strongestHz = 0.0f;
};
} // namespace zfp
