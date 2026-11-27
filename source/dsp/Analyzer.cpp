#include "Analyzer.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace zfp
{
void Analyzer::setTuning (const Tuning& t) noexcept
{
    tune = t;
    stepUp      = frameSec / tune.scoreUpSec;
    stepUpAlert = frameSec / tune.scoreUpAlertSec;
    stepDown    = frameSec / tune.scoreDownSec;
    releaseStep = frameSec / tune.releaseSec;
    deepenStep  = frameSec / tune.deepenSec;
    kLo = std::max (3, int (std::ceil (tune.analysisLowHz / binHz)));
    kHi = std::min (half - 4, int (std::floor (tune.analysisHighHz / binHz)));
    combMin = std::max (3, int (std::lround (tune.combMinHz / binHz)));
    combMax = std::max (combMin, int (std::lround (tune.combMaxHz / binHz)));
    preCoef = float (1.0 - std::exp (-2.0 * std::numbers::pi * double (tune.preHighpassHz) / fs));
}

void Analyzer::prepare (double sampleRate, int numChannels, int hopSize, const BandLayout& layout)
{
    fs       = sampleRate;
    channels = numChannels;
    hop      = hopSize;
    fftSize  = hopSize * 8; // 48 kHz で 2048 点、96 kHz で 4096 点
    half     = fftSize / 2;
    frameSec = float (double (hop) / sampleRate);
    binHz    = float (sampleRate / double (fftSize));

    fft.prepare (fftSize);
    window.resize (size_t (fftSize));
    double sum = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        window[size_t (i)] = float (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * double (i) / double (fftSize)));
        sum += double (window[size_t (i)]);
    }
    norm2 = float ((2.0 / sum) * (2.0 / sum)); // サイン波の振幅がそのまま出る正規化（パワー）

    for (auto& r : ring)
        r.assign (size_t (fftSize), 0.0f);
    workRe.assign (size_t (fftSize), 0.0f);
    workIm.assign (size_t (fftSize), 0.0f);
    power.assign (size_t (half + 1), 0.0f);
    level.assign (size_t (half + 1), -200.0f);
    score.assign (size_t (half + 1), 0.0f);
    smooth.assign (size_t (half + 1), 0.0f);
    runMax.assign (size_t (half + 1), -200.0f);
    runStart.assign (size_t (half + 1), -200.0f);
    confirmedNow.assign (size_t (half + 1), 0);
    prominence.assign (size_t (half + 1), -100.0f);
    partials.clear();
    partials.reserve (size_t (half + 1));
    prefix.assign (size_t (half + 2), 0.0);
    streak.assign (size_t (half + 1), 0);
    peak.assign (size_t (half + 1), 0);
    localWidth.assign (size_t (half + 1), 8);
    for (int k = 0; k <= half; ++k)
        localWidth[size_t (k)] = std::clamp (int (std::lround (0.26 * double (k))), 8, 48);

    setTuning (tune);

    // バンドが担当するビンの範囲（隣との中間点で区切る。両端は解析範囲いっぱいまで）。
    for (int b = 0; b < kNumBands; ++b)
    {
        const double f  = layout.fc[size_t (b)];
        const double lo = b == 0 ? double (tune.analysisLowHz) : 0.5 * (layout.fc[size_t (b - 1)] + f);
        const double hi = b == kNumBands - 1 ? double (tune.analysisHighHz) + double (binHz)
                                             : 0.5 * (f + layout.fc[size_t (b + 1)]);
        int first = int (std::ceil (lo / double (binHz)));
        int last  = int (std::ceil (hi / double (binHz))) - 1;
        if (last < first)
            first = last = int (std::lround (f / double (binHz)));
        bandFirst[size_t (b)] = std::clamp (first, kLo, kHi);
        bandLast[size_t (b)]  = std::clamp (last, kLo, kHi);
    }

    reset();
}

void Analyzer::reset() noexcept
{
    for (auto& r : ring)
        std::fill (r.begin(), r.end(), 0.0f);
    pre = { 0.0f, 0.0f };
    writePos = 0;
    std::fill (score.begin(), score.end(), 0.0f);
    std::fill (smooth.begin(), smooth.end(), 0.0f);
    std::fill (streak.begin(), streak.end(), 0);
    std::fill (runMax.begin(), runMax.end(), -200.0f);
    bandScore.fill (0.0f);
    depth.fill (0.0f);
    holdLeft.fill (0.0f);
    idleFor.fill (0.0f);
    state.fill (BandState::idle);
    strikes.fill (0);
    everEngaged.fill (0);
    alertLeft = 0.0f;
    strongestHz = 0.0f;
}

void Analyzer::stepBand (int b, float s, bool howlNow) noexcept
{
    const size_t i = size_t (b);
    auto engage = [&]
    {
        state[i] = BandState::engaged;
        alertLeft = tune.alertSec;
    };
    // 発振がいま確認できている間だけ、深さを一定の速さで進める（収まり始めたら止まる）。
    auto deepen = [&]
    {
        if (howlNow)
            depth[i] = std::min (1.0f, depth[i] + deepenStep);
    };

    switch (state[i])
    {
        case BandState::idle:
            depth[i] = 0.0f;
            idleFor[i] += frameSec;
            if (idleFor[i] >= tune.forgetSec)
                strikes[i] = 0;
            if (s >= tune.engageOn)
            {
                if (everEngaged[i] && idleFor[i] <= tune.retriggerSec)
                    strikes[i] = std::uint8_t (std::min<int> (strikes[i] + 1, tune.holdMaxDoublings));
                everEngaged[i] = 1;
                engage();
                deepen();
            }
            break;

        case BandState::engaged:
            deepen();
            if (s < tune.engageOff)
            {
                holdLeft[i] = std::min (tune.holdBaseSec * float (1 << strikes[i]), tune.holdMaxSec);
                state[i] = BandState::hold;
            }
            break;

        case BandState::hold:
            holdLeft[i] -= frameSec;
            if (s >= tune.engageOff && howlNow) // 一度抑えた帯域は、低いしきい値ですぐ再開する
            {
                engage();
                deepen();
            }
            else if (holdLeft[i] <= 0.0f)
                state[i] = BandState::release;
            break;

        case BandState::release:
            if (s >= tune.engageOff && howlNow)
            {
                strikes[i] = std::uint8_t (std::min<int> (strikes[i] + 1, tune.holdMaxDoublings));
                engage();
                deepen();
            }
            else
            {
                depth[i] = std::max (0.0f, depth[i] - releaseStep);
                if (depth[i] <= 0.0f)
                {
                    idleFor[i] = 0.0f;
                    state[i] = BandState::idle;
                }
            }
            break;
    }
}

// 候補 k が「等間隔に並ぶ 3 本以上の山」の一員かどうか。声や楽器の倍音はこの形になる。
bool Analyzer::inHarmonicSeries (int k, float levelDb) const noexcept
{
    const float need = levelDb - tune.combPartnerDb;
    auto matches = [] (int a, int b, int spacing) { return std::abs (a - b) <= 2 + spacing / 64; };

    for (const int a : partials)
    {
        const int d = std::abs (a - k);
        if (d < combMin || d > combMax || level[size_t (a)] < need)
            continue;

        for (const int b : partials)
        {
            if (b == a || b == k || level[size_t (b)] < need)
                continue;
            // k が真ん中（a と b が反対側に同じ間隔）、または k が端（同じ側に 1 倍・2 倍の間隔）。
            if (matches (b, 2 * k - a, d) || matches (b, 2 * a - k, d))
                return true;
        }
    }
    return false;
}

void Analyzer::frame() noexcept
{
    // 1) 窓掛け。ステレオは L を実部、R を虚部に入れて 1 回の FFT で両方のスペクトルを得る。
    const int mask = fftSize - 1;
    if (channels == 2)
    {
        for (int i = 0; i < fftSize; ++i)
        {
            const size_t src = size_t ((writePos + i) & mask);
            workRe[size_t (i)] = ring[0][src] * window[size_t (i)];
            workIm[size_t (i)] = ring[1][src] * window[size_t (i)];
        }
    }
    else
    {
        for (int i = 0; i < fftSize; ++i)
        {
            workRe[size_t (i)] = ring[0][size_t ((writePos + i) & mask)] * window[size_t (i)];
            workIm[size_t (i)] = 0.0f;
        }
    }
    fft.forward (workRe.data(), workIm.data());

    // 2) パワー（ステレオは左右の大きい方。振幅だけを見るので逆相でも打ち消し合わない）とレベル。
    prefix[0] = 0.0;
    for (int k = 0; k <= half; ++k)
    {
        float p;
        const float zr = workRe[size_t (k)], zi = workIm[size_t (k)];
        if (channels == 2)
        {
            // Z[k] と conj(Z[N-k]) の和と差から、L と R のスペクトルを取り出す。
            const size_t m = size_t ((fftSize - k) & mask);
            const float mr = workRe[m], mi = -workIm[m];
            const float lr = zr + mr, li = zi + mi, rr = zr - mr, ri = zi - mi;
            p = 0.25f * std::max (lr * lr + li * li, rr * rr + ri * ri);
        }
        else
        {
            p = zr * zr + zi * zi;
        }
        p *= norm2;
        power[size_t (k)]      = p;
        prefix[size_t (k + 1)] = prefix[size_t (k)] + double (p);
    }
    for (int k = kLo - 2; k <= kHi + 2; ++k)
        level[size_t (k)] = 10.0f * std::log10 (power[size_t (k)] + 1.0e-18f);

    // 3) ピークの判定。まず全ての山の突出度を求め、倍音の候補（partials）を集める。
    const bool alert = alertLeft > 0.0f;
    std::fill (peak.begin(), peak.end(), std::uint8_t (0));
    partials.clear();

    for (int k = kLo; k <= kHi; ++k)
    {
        const size_t i = size_t (k);
        prominence[i] = -100.0f;
        if (level[i] < tune.gateDb || power[i] < power[i - 1] || power[i] < power[i + 1])
            continue;

        // 局所平均（自分の周り ±2 ビンを除く）。
        const int w  = localWidth[i];
        const int lo = std::max (1, k - w), hi = std::min (half, k + w);
        const double sum = (prefix[size_t (hi + 1)] - prefix[size_t (lo)])
                         - (prefix[size_t (k + 3)] - prefix[size_t (k - 2)]);
        const int count = (hi - lo + 1) - 5;
        const float meanDb = 10.0f * std::log10 (float (std::max (sum, 0.0) / double (count)) + 1.0e-18f);
        prominence[i] = level[i] - meanDb;
        if (prominence[i] >= tune.partialProminenceDb)
            partials.push_back (k); // 容量は prepare で確保済み
    }

    float strongest = -200.0f;
    for (const int k : partials)
    {
        const size_t i = size_t (k);
        if (prominence[i] < tune.prominenceDb)
            continue;

        const bool emergency = level[i] >= tune.emergencyLevelDb
                            && prominence[i] >= tune.emergencyProminenceDb;
        if (! emergency && ! alert && inHarmonicSeries (k, level[i]))
            continue;

        peak[i] = 1;
        if (level[i] > strongest)
        {
            strongest = level[i];
            strongestHz = float (k) * binHz;
        }
    }

    // 4) ±2 ビンへ広げて、落ち込み量で安定を判定し、スコアを固定ステップで上下させる。
    const float up = alert ? stepUpAlert : stepUp;
    for (int k = kLo; k <= kHi; ++k)
    {
        const size_t i = size_t (k);
        const bool near = peak[i - 2] | peak[i - 1] | peak[i] | peak[i + 1] | peak[i + 2];
        bool confirmed = false, growing = false;
        if (near)
        {
            const float l5 = std::max ({ level[i - 2], level[i - 1], level[i], level[i + 1], level[i + 2] });
            if (streak[i] == 0)
                runMax[i] = runStart[i] = l5;
            runMax[i] = std::max (runMax[i], l5);
            if (runMax[i] - l5 > tune.drawdownDb)
                streak[i] = 0;
            else
            {
                ++streak[i];
                confirmed = true;
                growing = l5 - runStart[i] >= tune.growthFastDb;
            }
        }
        else
        {
            streak[i] = 0;
        }
        confirmedNow[i] = confirmed ? 1 : 0;
        score[i] = std::clamp (score[i] + (confirmed ? (growing ? stepUpAlert : up) : -stepDown), 0.0f, 1.0f);
    }

    // 5) 空間の平滑化（三角 5 点。広げた後なので中心は 1.0 まで届く）とバンドへの割り当て。
    for (int k = kLo; k <= kHi; ++k)
    {
        const size_t i = size_t (k);
        smooth[i] = (score[i - 2] + 2.0f * score[i - 1] + 3.0f * score[i]
                     + 2.0f * score[i + 1] + score[i + 2]) * (1.0f / 9.0f);
    }

    if (alertLeft > 0.0f)
        alertLeft -= frameSec;

    for (int b = 0; b < kNumBands; ++b)
    {
        float s = 0.0f;
        bool howlNow = false;
        for (int k = bandFirst[size_t (b)]; k <= bandLast[size_t (b)]; ++k)
        {
            s = std::max (s, smooth[size_t (k)]);
            howlNow = howlNow || confirmedNow[size_t (k)] != 0;
        }
        bandScore[size_t (b)] = s;
        stepBand (b, s, howlNow);
    }
}
} // namespace zfp
