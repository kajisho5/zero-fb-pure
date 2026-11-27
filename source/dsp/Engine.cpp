#include "Engine.h"
#include "Denormals.h"
#include <algorithm>
#include <cmath>

namespace zfp
{
namespace
{
// NaN・Inf・しきい値超えをまとめて「異常」と判定する（NaN は比較が必ず偽になる性質を使う）。
inline bool isBad (float v) noexcept { return ! (std::abs (v) <= kGuardThreshold); }
}

bool Engine::prepare (double sampleRate, int numInputChannels, int numOutputChannels) noexcept
{
    const bool layoutOk = (numInputChannels == 1 && (numOutputChannels == 1 || numOutputChannels == 2))
                       || (numInputChannels == 2 && numOutputChannels == 2);
    if (! layoutOk || ! layout.compute (sampleRate))
        return false;

    fs     = sampleRate;
    numIn  = numInputChannels;
    numOut = numOutputChannels;
    hop    = sampleRate <= 50000.0 ? 256 : 512; // SPEC §2.1, §6.3

    fadeSamples  = std::max (1, int (std::lround (kFadeSeconds * sampleRate)));
    fadeStep     = 1.0f / float (fadeSamples);
    guardRelStep = float ((double (hop) / sampleRate) / kGuardRelSeconds);

    decoupler.build (layout, sampleRate);
    analyzer.prepare (sampleRate, numIn, hop, layout);

    for (auto& d : dc)
        d.prepare (sampleRate);

    gainDb.fill (0.0f);
    notchFreq.fill (0.0f);
    notchGainDb.fill (0.0f);
    clearFilterStates();
    computeTargetCoefs();
    computeNotchTargets();
    snapCoefsToTargets();

    hopCount   = 0;
    guard      = GuardState::normal;
    guardClean = 0;
    guardFade  = 1.0f;
    guardRel   = 0.0f;

    const bool wantDry = bypass.load (std::memory_order_relaxed)
                      || strength.load (std::memory_order_relaxed) <= 0.0f;
    wet      = wantDry ? 0.0f : 1.0f;
    muteGain = mute.load (std::memory_order_relaxed) ? 0.0f : 1.0f;
    resetHandled = resetRequests.load (std::memory_order_relaxed);
    return true;
}

void Engine::setTestTargetsDb (const float* targetsDb) noexcept
{
    useTestTargets = targetsDb != nullptr;
    if (useTestTargets)
        std::copy (targetsDb, targetsDb + kNumBands, testTargets.begin());
}

void Engine::setTestNotch (int index, float freqHz, float gainDb) noexcept
{
    testNotchIndex  = index;
    testNotchFreq   = freqHz;
    testNotchGainDb = gainDb;
}

void Engine::clearFilterStates() noexcept
{
    for (int c = 0; c < kMaxChannels; ++c)
    {
        std::fill (std::begin (nic1[c]), std::end (nic1[c]), 0.0f);
        std::fill (std::begin (nic2[c]), std::end (nic2[c]), 0.0f);
        std::fill (std::begin (ic1[c]), std::end (ic1[c]), 0.0f);
        std::fill (std::begin (ic2[c]), std::end (ic2[c]), 0.0f);
        dc[size_t (c)].reset();
    }
}

void Engine::computeTargetCoefs() noexcept
{
    for (int b = 0; b < kNumBands; ++b)
    {
        const size_t i = size_t (b);
        const double a  = std::pow (10.0, double (gainDb[i]) / 40.0);
        const double g  = layout.g[i];
        const double k  = 1.0 / (layout.q[i] * a);
        const double c1 = 1.0 / (1.0 + g * (g + k));
        ta1[i] = float (c1);
        ta2[i] = float (g * c1);
        ta3[i] = float (g * g * c1);
        tm1[i] = float (k * (a * a - 1.0)); // 0 dB のとき厳密に 0（そのバンドは素通し）
    }
}

// ノッチの目標係数を、notchFreq と notchGainDb から作る。
void Engine::computeNotchTargets() noexcept
{
    for (int n = 0; n < kNumNotches; ++n)
    {
        const size_t i = size_t (n);
        const double f  = std::clamp (double (notchFreq[i] > 0.0f ? notchFreq[i] : 1000.0f), 20.0, 0.45 * fs);
        const double a  = std::pow (10.0, double (notchGainDb[i]) / 40.0);
        const double g  = std::tan (3.14159265358979323846 * f / fs);
        const double k  = 1.0 / (kNotchQ * a);
        const double c1 = 1.0 / (1.0 + g * (g + k));
        tna1[i] = float (c1);
        tna2[i] = float (g * c1);
        tna3[i] = float (g * g * c1);
        tnm1[i] = float (k * (a * a - 1.0));
    }
}

void Engine::snapCoefsToTargets() noexcept
{
    a1 = ta1; a2 = ta2; a3 = ta3; m1 = tm1;
    na1 = tna1; na2 = tna2; na3 = tna3; nm1 = tnm1;
    dna1.fill (0.0f); dna2.fill (0.0f); dna3.fill (0.0f); dnm1.fill (0.0f);
    da1.fill (0.0f); da2.fill (0.0f); da3.fill (0.0f); dm1.fill (0.0f);
    rampLeft = 0;
    updateDerivedCoefs();
}

void Engine::updateDerivedCoefs() noexcept
{
    for (int b = 0; b < kNumBands; ++b)
    {
        const size_t i = size_t (b);
        c0[i] = 1.0f + m1[i] * a2[i];
    }
    for (int n = 0; n < kNumNotches; ++n)
    {
        const size_t i = size_t (n);
        nc0[i] = 1.0f + nm1[i] * na2[i];
    }
}

void Engine::enterMuted() noexcept
{
    clearFilterStates();
    analyzer.reset();
    guard      = GuardState::muted;
    guardClean = 0;
    guardFade  = 0.0f;
}

void Engine::beginRecover() noexcept
{
    // いちばん深く切った状態から戻す：全バンドの目標を FLOOR にして解き、係数を即セットする。
    const float fl = std::clamp (floorDb.load (std::memory_order_relaxed), kFloorMinDb, kFloorMaxDb);
    targetDb.fill (fl);
    gainDb.fill (0.0f);
    decoupler.solve (targetDb.data(), gainDb.data(), 4 * kDecoupleIters);
    computeTargetCoefs();
    notchFreq.fill (0.0f);
    notchGainDb.fill (0.0f);
    computeNotchTargets();
    snapCoefsToTargets();

    guardRel  = 1.0f;
    guardFade = 0.0f;
    hopCount  = 0;
    guard     = GuardState::recover;
}

void Engine::applyReset() noexcept
{
    clearFilterStates();
    analyzer.reset();
    gainDb.fill (0.0f);
    notchFreq.fill (0.0f);
    notchGainDb.fill (0.0f);
    computeNotchTargets();
    na1 = tna1; na2 = tna2; na3 = tna3; nm1 = tnm1;
    dna1.fill (0.0f); dna2.fill (0.0f); dna3.fill (0.0f); dnm1.fill (0.0f);
    updateDerivedCoefs();
    guardRel = 0.0f;
    // ゲインは次の制御フレームから 1 フレームかけて目標へ動く。
}

void Engine::controlFrame() noexcept
{
    if (guard == GuardState::muted)
        return;

    const float fl = std::clamp (floorDb.load (std::memory_order_relaxed), kFloorMinDb, kFloorMaxDb);

    // 検出は常に動かす（バイパス中も会場の状態を追い続ける）。
    analyzer.frame();
    const float s01 = std::clamp (strength.load (std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f);
    const float maxDepthDb = kMaxDepthDb * std::pow (s01, kStrengthPower); // SPEC §5.9
    const float* depth = analyzer.depths();

    for (int b = 0; b < kNumBands; ++b)
    {
        const size_t i = size_t (b);
        float t = useTestTargets ? testTargets[i] : -depth[i] * maxDepthDb;
        t = std::min (t, fl * guardRel);                  // ガード復帰直後の深いカット
        targetDb[i] = std::clamp (t, fl, 0.0f);
    }

    const float guardRelBefore = guardRel;
    if (guardRel > 0.0f)
        guardRel = std::max (0.0f, guardRel - guardRelStep);

    decoupler.solve (targetDb.data(), gainDb.data(), kDecoupleIters);
    computeTargetCoefs();

    // ノッチ：周波数の付け替えは、そのノッチが 0 dB のときだけ行う（動かすときのノイズを出さないため）。
    for (int n = 0; n < kNumNotches; ++n)
    {
        const size_t i = size_t (n);
        float wantFreq = analyzer.notchFreqHz (n);
        float wantDb   = useTestTargets ? 0.0f : -analyzer.notchDepth (n) * maxDepthDb;
        if (n == testNotchIndex)
        {
            wantFreq = testNotchFreq;
            wantDb   = testNotchGainDb;
        }
        if (wantFreq != notchFreq[i])
        {
            if (notchGainDb[i] == 0.0f && nm1[i] == 0.0f)
            {
                notchFreq[i] = wantFreq;
                notchGainDb[i] = 0.0f;
                // 0 dB のまま周波数だけ替える：係数を即セットし、状態は 0 から始める。
                const double f = std::clamp (double (wantFreq > 0.0f ? wantFreq : 1000.0f), 20.0, 0.45 * fs);
                const double g = std::tan (3.14159265358979323846 * f / fs);
                const double c1 = 1.0 / (1.0 + g * (g + 1.0 / kNotchQ));
                na1[i] = float (c1); na2[i] = float (g * c1); na3[i] = float (g * g * c1); nm1[i] = 0.0f;
                for (int c = 0; c < kMaxChannels; ++c)
                    nic1[c][n] = nic2[c][n] = 0.0f;
            }
            else
            {
                wantDb = 0.0f; // まず 0 dB に戻してから付け替える
            }
        }
        notchGainDb[i] = std::clamp (std::min (wantDb, fl * guardRelBefore), fl, 0.0f);
    }
    computeNotchTargets();

    const float inv = 1.0f / float (hop);
    for (int n = 0; n < kNumNotches; ++n)
    {
        const size_t i = size_t (n);
        dna1[i] = (tna1[i] - na1[i]) * inv;
        dna2[i] = (tna2[i] - na2[i]) * inv;
        dna3[i] = (tna3[i] - na3[i]) * inv;
        dnm1[i] = (tnm1[i] - nm1[i]) * inv;
    }
    for (int b = 0; b < kNumBands; ++b)
    {
        const size_t i = size_t (b);
        da1[i] = (ta1[i] - a1[i]) * inv;
        da2[i] = (ta2[i] - a2[i]) * inv;
        da3[i] = (ta3[i] - a3[i]) * inv;
        dm1[i] = (tm1[i] - m1[i]) * inv;
    }
    rampLeft = hop;
}

void Engine::process (const float* const* in, float* const* out, int numSamples) noexcept
{
    const ScopedNoDenormals noDenormals;

    // ブロックの先頭で読む設定（切り替え系）。
    const bool bypassOn = bypass.load (std::memory_order_relaxed);
    const bool muteOn   = mute.load (std::memory_order_relaxed);
    const bool wantDry  = bypassOn || strength.load (std::memory_order_relaxed) <= 0.0f;
    const float wetTarget  = wantDry ? 0.0f : 1.0f;
    const float muteTarget = (muteOn && ! bypassOn) ? 0.0f : 1.0f; // BYPASS 中は MUTE を無視

    const unsigned resetNow = resetRequests.load (std::memory_order_relaxed);
    if (resetNow != resetHandled)
    {
        resetHandled = resetNow;
        applyReset();
    }

    // サンプルのループを必ず一番外側にする（ブロック長で結果が変わらないように）。
    for (int n = 0; n < numSamples; ++n)
    {
        float x[kMaxChannels];
        bool inputBad = false;
        for (int c = 0; c < numIn; ++c)
        {
            x[c] = in[c][n];
            inputBad = inputBad || isBad (x[c]);
        }

        float p[kMaxChannels] = { 0.0f, 0.0f };

        // 係数を 1 サンプル分進める。
        if (rampLeft > 0)
        {
            if (--rampLeft == 0)
            {
                a1 = ta1; a2 = ta2; a3 = ta3; m1 = tm1;
                na1 = tna1; na2 = tna2; na3 = tna3; nm1 = tnm1;
                updateDerivedCoefs();
            }
            else
            {
                for (int b = 0; b < kNumBands; ++b)
                {
                    const size_t i = size_t (b);
                    a1[i] += da1[i]; a2[i] += da2[i]; a3[i] += da3[i]; m1[i] += dm1[i];
                    c0[i] = 1.0f + m1[i] * a2[i];
                }
                for (int nn = 0; nn < kNumNotches; ++nn)
                {
                    const size_t i = size_t (nn);
                    na1[i] += dna1[i]; na2[i] += dna2[i]; na3[i] += dna3[i]; nm1[i] += dnm1[i];
                    nc0[i] = 1.0f + nm1[i] * na2[i];
                }
            }
        }

        if (guard == GuardState::muted)
        {
            // 無音のまま、入力が正常に戻るのを待つ。
            guardClean = inputBad ? 0 : guardClean + 1;
            if (guardClean >= kGuardCleanCount)
                beginRecover();
        }
        else if (inputBad)
        {
            enterMuted();
        }
        else
        {
            bool eqBad = false;
            float v[kMaxChannels] = { 0.0f, 0.0f };
            for (int c = 0; c < numIn; ++c)
                v[c] = dc[size_t (c)].tick (x[c]);

            // バンドを外側、チャンネルを内側にして、左右の計算を同時に進められるようにする。
            // 出力は out = v + m1*v1 と同じ値を、c0*v + m1*(a1*s1 - a2*s2) の形で求める
            // （次のバンドへ渡る計算を短くするため。m1 = 0 のときは厳密に out = v）。
            for (int b = 0; b < kNumBands; ++b)
            {
                const size_t i = size_t (b);
                const float ca1 = a1[i], ca2 = a2[i], ca3 = a3[i], cm1 = m1[i], cc0 = c0[i];
                for (int c = 0; c < numIn; ++c)
                {
                    const float s1 = ic1[c][b], s2 = ic2[c][b];
                    const float in0 = v[c];
                    const float v3 = in0 - s2;
                    const float v1 = ca1 * s1 + ca2 * v3;
                    const float v2 = s2 + ca2 * s1 + ca3 * v3;
                    ic1[c][b] = 2.0f * v1 - s1;
                    ic2[c][b] = 2.0f * v2 - s2;
                    v[c] = cc0 * in0 + cm1 * (ca1 * s1 - ca2 * s2);
                }
            }

            // 動くノッチ（直列）。計算の形は帯域と同じ。
            for (int nn = 0; nn < kNumNotches; ++nn)
            {
                const size_t i = size_t (nn);
                const float ca1 = na1[i], ca2 = na2[i], ca3 = na3[i], cm1 = nm1[i], cc0 = nc0[i];
                for (int c = 0; c < numIn; ++c)
                {
                    const float s1 = nic1[c][nn], s2 = nic2[c][nn];
                    const float in0 = v[c];
                    const float v3 = in0 - s2;
                    const float v1 = ca1 * s1 + ca2 * v3;
                    const float v2 = s2 + ca2 * s1 + ca3 * v3;
                    nic1[c][nn] = 2.0f * v1 - s1;
                    nic2[c][nn] = 2.0f * v2 - s2;
                    v[c] = cc0 * in0 + cm1 * (ca1 * s1 - ca2 * s2);
                }
            }

            for (int c = 0; c < numIn; ++c)
            {
                eqBad = eqBad || isBad (v[c]);
                p[c] = v[c];
            }

            if (eqBad)
            {
                enterMuted();
                p[0] = p[1] = 0.0f;
            }
            else
            {
                for (int c = 0; c < numIn; ++c)
                    p[c] = softLimit (p[c]);

                if (guard == GuardState::recover)
                {
                    guardFade += fadeStep;
                    if (guardFade >= 1.0f)
                    {
                        guardFade = 1.0f;
                        guard = GuardState::normal;
                    }
                    else
                    {
                        for (int c = 0; c < numIn; ++c)
                            p[c] *= guardFade;
                    }
                }
            }
        }

        // 解析用に入力を貯める（異常値は 0 として渡す）。
        if (guard != GuardState::muted)
            analyzer.push (inputBad ? 0.0f : x[0], (inputBad || numIn < 2) ? 0.0f : x[1]);

        // 制御フレーム（ホップ境界のサンプルの処理直後）。結果は次のサンプルから反映される。
        if (++hopCount >= hop)
        {
            hopCount = 0;
            controlFrame();
        }

        // バイパスのクロスフェード。
        if (wet != wetTarget)
        {
            wet = wetTarget > wet ? std::min (wetTarget, wet + fadeStep)
                                  : std::max (wetTarget, wet - fadeStep);
        }

        float y[kMaxChannels];
        if (wet <= 0.0f)
        {
            for (int c = 0; c < numIn; ++c)
                y[c] = x[c]; // 代入のみ（ビット完全一致）
        }
        else if (wet >= 1.0f)
        {
            for (int c = 0; c < numIn; ++c)
                y[c] = p[c];
        }
        else
        {
            for (int c = 0; c < numIn; ++c)
            {
                const float dry = isBad (x[c]) ? 0.0f : x[c]; // 混ぜている最中は異常値を通さない
                y[c] = (1.0f - wet) * dry + wet * p[c];
            }
        }

        // MUTE。
        if (muteGain != muteTarget)
        {
            muteGain = muteTarget > muteGain ? std::min (muteTarget, muteGain + fadeStep)
                                             : std::max (muteTarget, muteGain - fadeStep);
        }
        if (muteGain != 1.0f)
        {
            for (int c = 0; c < numIn; ++c)
                y[c] *= muteGain;
        }

        out[0][n] = y[0];
        if (numOut == 2)
            out[1][n] = numIn == 2 ? y[1] : y[0];
    }
}
} // namespace zfp
