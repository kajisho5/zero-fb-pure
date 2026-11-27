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
    hop    = sampleRate <= 50000.0 ? 128 : 256; // SPEC §2.1, §6.3

    fadeSamples  = std::max (1, int (std::lround (kFadeSeconds * sampleRate)));
    fadeStep     = 1.0f / float (fadeSamples);
    guardRelStep = float ((double (hop) / sampleRate) / kGuardRelSeconds);

    decoupler.build (layout, sampleRate);

    for (auto& d : dc)
        d.prepare (sampleRate);

    gainDb.fill (0.0f);
    clearFilterStates();
    computeTargetCoefs();
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

void Engine::clearFilterStates() noexcept
{
    for (int c = 0; c < kMaxChannels; ++c)
    {
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

void Engine::snapCoefsToTargets() noexcept
{
    a1 = ta1; a2 = ta2; a3 = ta3; m1 = tm1;
    da1.fill (0.0f); da2.fill (0.0f); da3.fill (0.0f); dm1.fill (0.0f);
    rampLeft = 0;
}

void Engine::enterMuted() noexcept
{
    clearFilterStates();
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
    snapCoefsToTargets();

    guardRel  = 1.0f;
    guardFade = 0.0f;
    hopCount  = 0;
    guard     = GuardState::recover;
}

void Engine::applyReset() noexcept
{
    clearFilterStates();
    gainDb.fill (0.0f);
    guardRel = 0.0f;
    // ゲインは次の制御フレームから 1 フレームかけて目標へ動く。
}

void Engine::controlFrame() noexcept
{
    if (guard == GuardState::muted)
        return;

    const float fl = std::clamp (floorDb.load (std::memory_order_relaxed), kFloorMinDb, kFloorMaxDb);

    for (int b = 0; b < kNumBands; ++b)
    {
        const size_t i = size_t (b);
        float t = useTestTargets ? testTargets[i] : 0.0f; // Step 2 で検出結果に置き換わる
        t = std::min (t, fl * guardRel);                  // ガード復帰直後の深いカット
        targetDb[i] = std::clamp (t, fl, 0.0f);
    }

    if (guardRel > 0.0f)
        guardRel = std::max (0.0f, guardRel - guardRelStep);

    decoupler.solve (targetDb.data(), gainDb.data(), kDecoupleIters);
    computeTargetCoefs();

    const float inv = 1.0f / float (hop);
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
            }
            else
            {
                for (int b = 0; b < kNumBands; ++b)
                {
                    const size_t i = size_t (b);
                    a1[i] += da1[i]; a2[i] += da2[i]; a3[i] += da3[i]; m1[i] += dm1[i];
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
            for (int c = 0; c < numIn; ++c)
            {
                float v = dc[size_t (c)].tick (x[c]);
                float* s1 = ic1[c];
                float* s2 = ic2[c];

                for (int b = 0; b < kNumBands; ++b)
                {
                    const size_t i = size_t (b);
                    const float v3 = v - s2[b];
                    const float v1 = a1[i] * s1[b] + a2[i] * v3;
                    const float v2 = s2[b] + a2[i] * s1[b] + a3[i] * v3;
                    s1[b] = 2.0f * v1 - s1[b];
                    s2[b] = 2.0f * v2 - s2[b];
                    v = v + m1[i] * v1;
                }

                eqBad = eqBad || isBad (v);
                p[c] = v;
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
