// ClosedLoopBench（SPEC §8.3）の道具：架空の会場の応答、畳み込み、ループの模擬、計測。
// 会場の名前と応答はすべて架空で、コードで生成する。乱数の種は固定。
#pragma once
#include "TestHelpers.h"
#include "dsp/Fft.h"
#include <optional>

namespace zfptest::loop
{
inline constexpr double kFs = 48000.0;

struct Xorshift
{
    std::uint32_t s;
    float uniform() noexcept // (0, 1)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float (s >> 8) + 0.5f) / 16777216.0f;
    }
    float gaussian() noexcept
    {
        const float u1 = uniform(), u2 = uniform();
        return std::sqrt (-2.0f * std::log (u1)) * std::cos (6.2831853f * u2);
    }
};

// 架空の会場の応答。直接音 1 + 指数減衰する雑音。80 Hz〜12 kHz に絞り、周波数応答の最大を 0 dB にそろえる。
inline std::vector<float> makeRoom (double rt60, float rho, std::uint32_t seed)
{
    const auto len = std::size_t (rt60 * kFs);
    int n = 1;
    while (std::size_t (n) < 2 * len) n <<= 1;

    std::vector<float> re (std::size_t (n), 0.0f), im (std::size_t (n), 0.0f);
    Xorshift rng { seed };
    re[0] = 1.0f;
    for (std::size_t i = 1; i < len; ++i)
        re[i] = rho * rng.gaussian() * float (std::exp (-6.91 * double (i) / (rt60 * kFs)));

    zfp::Fft fft;
    fft.prepare (n);
    fft.forward (re.data(), im.data());

    float peak = 0.0f;
    for (int k = 0; k < n; ++k)
    {
        const double f = kFs * double (k <= n / 2 ? k : n - k) / double (n);
        if (f < 80.0 || f > 12000.0)
            re[std::size_t (k)] = im[std::size_t (k)] = 0.0f;
        peak = std::max (peak, std::hypot (re[std::size_t (k)], im[std::size_t (k)]));
    }

    // 逆変換（共役 → 順変換 → 共役 → 1/N）。
    for (auto& v : im) v = -v;
    fft.forward (re.data(), im.data());
    std::vector<float> h (len);
    for (std::size_t i = 0; i < len; ++i)
        h[i] = re[i] / (float (n) * peak);
    return h;
}

// 実際の PA に近づけた架空の経路：直接音が主体＋数本の反射＋弱い残響＋機材の山（共振）3 つ。
// 残響だけの makeRoom より、発振しやすい周波数が少数の山に集まる。
inline std::vector<float> makePaPath (std::uint32_t seed)
{
    const double rt60 = 0.5;
    const auto len = std::size_t (0.35 * kFs);
    int n = 1;
    while (std::size_t (n) < 2 * len) n <<= 1;

    std::vector<float> re (std::size_t (n), 0.0f), im (std::size_t (n), 0.0f);
    Xorshift rng { seed };
    re[0] = 1.0f; // 直接音
    for (int r = 0; r < 8; ++r) // 反射 8 本：3〜40 ms、-6〜-16 dB、符号は交互
    {
        const auto at = std::size_t ((0.003 + 0.037 * double (rng.uniform())) * kFs);
        const float gain = std::pow (10.0f, (-6.0f - 10.0f * rng.uniform()) / 20.0f);
        re[at] += (r % 2 == 0 ? gain : -gain);
    }
    for (std::size_t i = std::size_t (0.02 * kFs); i < len; ++i) // 弱い残響
        re[i] += 0.006f * rng.gaussian() * float (std::exp (-6.91 * double (i) / (rt60 * kFs)));

    zfp::Fft fft;
    fft.prepare (n);
    fft.forward (re.data(), im.data());

    // 機材の山（スピーカーやマイクの共振に相当）：+6〜+10 dB、Q 6 のベルを 3 つ、振幅だけ掛ける。
    double peakHz[3], peakDb[3];
    for (int p = 0; p < 3; ++p)
    {
        peakHz[p] = 200.0 * std::pow (40.0, double (rng.uniform())); // 200 Hz〜8 kHz
        peakDb[p] = 6.0 + 4.0 * double (rng.uniform());
    }
    float peak = 0.0f;
    for (int k = 0; k < n; ++k)
    {
        const double f = kFs * double (k <= n / 2 ? k : n - k) / double (n);
        double gain = (f < 80.0 || f > 12000.0) ? 0.0 : 1.0;
        for (int p = 0; p < 3 && gain > 0.0; ++p)
        {
            const double x = 6.0 * (f / peakHz[p] - peakHz[p] / f);
            gain *= std::pow (10.0, peakDb[p] / 20.0 / (1.0 + x * x));
        }
        re[std::size_t (k)] *= float (gain);
        im[std::size_t (k)] *= float (gain);
        peak = std::max (peak, std::hypot (re[std::size_t (k)], im[std::size_t (k)]));
    }

    for (auto& v : im) v = -v;
    fft.forward (re.data(), im.data());
    std::vector<float> h (len);
    for (std::size_t i = 0; i < len; ++i)
        h[i] = re[i] / (float (n) * peak);
    return h;
}

// 等分割の周波数領域畳み込み（ブロック長 128）。
class Convolver
{
public:
    static constexpr int kBlock = 128;

    explicit Convolver (const std::vector<float>& h)
    {
        fft.prepare (2 * kBlock);
        parts = int ((h.size() + kBlock - 1) / kBlock);
        hRe.assign (std::size_t (parts) * 2 * kBlock, 0.0f);
        hIm.assign (std::size_t (parts) * 2 * kBlock, 0.0f);
        for (int p = 0; p < parts; ++p)
        {
            float* r = hRe.data() + std::size_t (p) * 2 * kBlock;
            float* i = hIm.data() + std::size_t (p) * 2 * kBlock;
            for (int k = 0; k < kBlock; ++k)
            {
                const std::size_t idx = std::size_t (p) * kBlock + std::size_t (k);
                r[k] = idx < h.size() ? h[idx] : 0.0f;
            }
            fft.forward (r, i);
        }
        xRe.assign (hRe.size(), 0.0f);
        xIm.assign (hIm.size(), 0.0f);
        prev.assign (kBlock, 0.0f);
        accRe.assign (2 * kBlock, 0.0f);
        accIm.assign (2 * kBlock, 0.0f);
    }

    // in: 128 サンプル → out: 128 サンプル。
    void process (const float* in, float* out)
    {
        const int n = 2 * kBlock;
        head = (head + parts - 1) % parts;
        float* r = xRe.data() + std::size_t (head) * std::size_t (n);
        float* i = xIm.data() + std::size_t (head) * std::size_t (n);
        for (int k = 0; k < kBlock; ++k)
        {
            r[k] = prev[std::size_t (k)];
            r[k + kBlock] = in[k];
            prev[std::size_t (k)] = in[k];
            i[k] = i[k + kBlock] = 0.0f;
        }
        fft.forward (r, i);

        std::fill (accRe.begin(), accRe.end(), 0.0f);
        std::fill (accIm.begin(), accIm.end(), 0.0f);
        for (int p = 0; p < parts; ++p)
        {
            const float* a = xRe.data() + std::size_t ((head + p) % parts) * std::size_t (n);
            const float* b = xIm.data() + std::size_t ((head + p) % parts) * std::size_t (n);
            const float* c = hRe.data() + std::size_t (p) * std::size_t (n);
            const float* d = hIm.data() + std::size_t (p) * std::size_t (n);
            for (int k = 0; k < n; ++k)
            {
                accRe[std::size_t (k)] += a[k] * c[k] - b[k] * d[k];
                accIm[std::size_t (k)] += a[k] * d[k] + b[k] * c[k];
            }
        }
        for (auto& v : accIm) v = -v;
        fft.forward (accRe.data(), accIm.data());
        for (int k = 0; k < kBlock; ++k)
            out[k] = accRe[std::size_t (k + kBlock)] / float (n);
    }

private:
    zfp::Fft fft;
    int parts = 0, head = 0;
    std::vector<float> hRe, hIm, xRe, xIm, prev, accRe, accIm;
};

// ピンクノイズ（-50 dBFS RMS）。
inline std::vector<float> pinkNoise (std::size_t n, std::uint32_t seed, float rmsDb = -50.0f)
{
    std::vector<float> v (n);
    Xorshift rng { seed };
    float b0 = 0, b1 = 0, b2 = 0;
    double sum = 0.0;
    for (auto& x : v)
    {
        const float w = rng.gaussian();
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        x = b0 + b1 + b2 + w * 0.1848f;
        sum += double (x) * double (x);
    }
    const float scale = std::pow (10.0f, rmsDb / 20.0f) / float (std::sqrt (sum / double (n)));
    for (auto& x : v) x *= scale;
    return v;
}

struct Run
{
    std::vector<float> x, y; // プラグインの入力と出力
    bool diverged = false;
};

// x[n] = s[n] + G * (h * y)[n - D],  y = Plugin(x)。engine が nullptr ならプラグインなし（素通し）。
inline Run simulate (const std::vector<float>& h, double delayMs, double gainDb, double seconds,
                     zfp::Engine* engine, std::uint32_t noiseSeed = 4242u)
{
    const int B = Convolver::kBlock;
    const auto n = (std::size_t (seconds * kFs) / std::size_t (B)) * std::size_t (B);
    const auto delay = std::size_t (std::lround (delayMs * 0.001 * kFs));
    const float g = float (std::pow (10.0, gainDb / 20.0));
    const std::vector<float> s = pinkNoise (n, noiseSeed);

    Run run;
    run.x.assign (n, 0.0f);
    run.y.assign (n, 0.0f);
    std::vector<float> c (n + std::size_t (B), 0.0f); // h * y
    Convolver conv (h);

    for (std::size_t pos = 0; pos < n; pos += std::size_t (B))
    {
        for (std::size_t i = pos; i < pos + std::size_t (B); ++i)
        {
            run.x[i] = s[i] + (i >= delay ? g * c[i - delay] : 0.0f);
            if (engine == nullptr && ! (std::abs (run.x[i]) <= 4.0f))
                run.diverged = true; // プラグインなしのときだけ、振幅 4.0 超えで発散とみなして打ち切る
        }
        if (run.diverged)
        {
            run.x.resize (pos);
            run.y.resize (pos);
            return run;
        }
        if (engine != nullptr)
        {
            const float* in[1] = { run.x.data() + pos };
            float* out[1] = { run.y.data() + pos };
            engine->process (in, out, B);
        }
        else
        {
            std::copy (run.x.begin() + std::ptrdiff_t (pos), run.x.begin() + std::ptrdiff_t (pos) + B,
                       run.y.begin() + std::ptrdiff_t (pos));
        }
        conv.process (run.y.data() + pos, c.data() + pos);
    }
    return run;
}

inline double rmsDb (const std::vector<float>& v, std::size_t begin, std::size_t end)
{
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sum += double (v[i]) * double (v[i]);
    return 10.0 * std::log10 (sum / double (std::max<std::size_t> (1, end - begin)) + 1.0e-20);
}

// 20 ms 窓の RMS を 1 ms ごとに求める [dBFS]。
inline std::vector<float> shortTermDb (const std::vector<float>& v)
{
    const std::size_t win = 960, step = 48;
    std::vector<double> ps (v.size() + 1, 0.0);
    for (std::size_t i = 0; i < v.size(); ++i)
        ps[i + 1] = ps[i] + double (v[i]) * double (v[i]);
    std::vector<float> out;
    for (std::size_t end = win; end <= v.size(); end += step)
        out.push_back (float (10.0 * std::log10 ((ps[end] - ps[end - win]) / double (win) + 1.0e-20)));
    return out; // out[i] は時刻 (win + i*step) / fs で終わる窓
}

struct Stability
{
    bool diverged = false;
    double tailRmsDb = 0.0;      // 最後の 1 秒
    double tail5RmsDb = 0.0;     // 最後の 5 秒
    std::optional<double> convergedAt; // t_c [秒]
    double steadyDb = 0.0;       // L_ss
    int huntingEvents = 0;
    double worstOverDb = 0.0;    // ハンチング時に L_ss を超えた最大量
};

// SPEC §8.3 の定義で、収束時刻・定常レベル・ハンチング回数を求める。windowSec は t_c からの観測時間。
inline Stability analyse (const Run& run, double windowSec)
{
    Stability st;
    st.diverged = run.diverged;
    const std::size_t n = run.y.size();
    if (n < 48000)
    {
        st.diverged = true;
        return st;
    }
    st.tailRmsDb  = rmsDb (run.y, n - 48000, n);
    st.tail5RmsDb = rmsDb (run.y, n >= 240000 ? n - 240000 : 0, n);
    if (st.tailRmsDb >= -20.0)
        st.diverged = true;

    const std::vector<float> lv = shortTermDb (run.y);
    // t_c: -30 dBFS 未満が 200 ms 続いた最初の時刻。
    int below = 0;
    std::size_t tc = lv.size();
    for (std::size_t i = 0; i < lv.size(); ++i)
    {
        below = lv[i] < -30.0f ? below + 1 : 0;
        if (below >= 200)
        {
            tc = i - 199;
            break;
        }
    }
    if (tc == lv.size())
        return st;
    st.convergedAt = (960.0 + double (tc) * 48.0) / kFs;

    const std::size_t end = std::min (lv.size(), tc + std::size_t (windowSec * 1000.0));
    std::vector<float> seg (lv.begin() + std::ptrdiff_t (tc), lv.begin() + std::ptrdiff_t (end));
    std::vector<float> sorted = seg;
    std::nth_element (sorted.begin(), sorted.begin() + std::ptrdiff_t (sorted.size() / 2), sorted.end());
    st.steadyDb = double (sorted[sorted.size() / 2]);

    bool above = false;
    for (const float v : seg)
    {
        const bool now = double (v) > st.steadyDb + 6.0;
        if (now && ! above)
            ++st.huntingEvents;
        above = now;
        st.worstOverDb = std::max (st.worstOverDb, double (v) - st.steadyDb);
    }
    return st;
}
// ---- 発振（モード）の追跡。プラグインの入力 x を短い STFT（512 点・ホップ 48）で見る。
struct ModeEvent
{
    double freqHz = 0.0;
    double emergeSec = 0.0; // レベルが -40 dBFS を超え、かつスペクトル中央値 +15 dB を超えた最初の時刻
    double peakSec = 0.0;   // レベルが最大になった時刻
    double endSec = 0.0;
    double peakDb = -200.0;
    bool repeat = false;    // 以前に収まった発振と同じ周波数（±8%）での再発 = ハンチング
};

inline std::vector<ModeEvent> trackModes (const std::vector<float>& x)
{
    const int n = 512, hopSize = 48, halfN = n / 2;
    zfp::Fft fft;
    fft.prepare (n);
    const auto un = std::size_t (n);
    std::vector<float> win (un, 0.0f), re (un, 0.0f), im (un, 0.0f), lv (un / 2, 0.0f);
    double wsum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        win[std::size_t (i)] = float (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / double (n)));
        wsum += double (win[std::size_t (i)]);
    }
    const float norm = float (2.0 / wsum);

    std::vector<ModeEvent> events;
    struct Active { ModeEvent ev; int quiet = 0; };
    std::vector<Active> active;
    auto same = [&] (double a, double b) { return std::abs (a - b) <= 0.08 * b + kFs / double (n); };

    for (std::size_t start = 0; start + std::size_t (n) <= x.size(); start += std::size_t (hopSize))
    {
        for (int i = 0; i < n; ++i)
        {
            re[std::size_t (i)] = x[start + std::size_t (i)] * win[std::size_t (i)];
            im[std::size_t (i)] = 0.0f;
        }
        fft.forward (re.data(), im.data());
        for (int k = 1; k < halfN; ++k)
            lv[std::size_t (k)] = 20.0f * std::log10 (norm * std::hypot (re[std::size_t (k)], im[std::size_t (k)]) + 1.0e-12f);
        std::vector<float> sorted (lv.begin() + 1, lv.end());
        std::nth_element (sorted.begin(), sorted.begin() + std::ptrdiff_t (sorted.size() / 2), sorted.end());
        const float median = sorted[sorted.size() / 2];
        const double t = double (start + std::size_t (n)) / kFs;

        for (auto& a : active)
            ++a.quiet;

        // 同時に鳴っている発振をすべて拾う（山の頂点で、-40 dBFS 以上、かつ中央値 +15 dB 以上）。
        for (int k = 2; k < halfN - 1; ++k)
        {
            const float v = lv[std::size_t (k)];
            if (v < -40.0f || v < median + 15.0f || v < lv[std::size_t (k - 1)] || v < lv[std::size_t (k + 1)])
                continue;
            const double f = kFs * double (k) / double (n);
            Active* hit = nullptr;
            for (auto& a : active)
                if (same (f, a.ev.freqHz))
                    hit = &a;
            if (hit == nullptr)
            {
                active.push_back ({});
                hit = &active.back();
                hit->ev.freqHz = f;
                hit->ev.emergeSec = t;
            }
            hit->quiet = 0;
            if (double (v) > hit->ev.peakDb)
            {
                hit->ev.peakDb = double (v);
                hit->ev.peakSec = t;
            }
        }

        for (std::size_t i = 0; i < active.size();)
        {
            if (active[i].quiet >= 20) // 約 20 ms 静かなら終わり
            {
                active[i].ev.endSec = t;
                events.push_back (active[i].ev);
                active.erase (active.begin() + std::ptrdiff_t (i));
            }
            else
                ++i;
        }
    }
    for (auto& a : active)
    {
        a.ev.endSec = double (x.size()) / kFs;
        events.push_back (a.ev);
    }
    std::sort (events.begin(), events.end(), [] (const ModeEvent& a, const ModeEvent& b) { return a.emergeSec < b.emergeSec; });

    for (std::size_t i = 0; i < events.size(); ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (std::abs (events[i].freqHz - events[j].freqHz) <= 0.08 * events[j].freqHz + kFs / double (n)
                && events[i].emergeSec - events[j].endSec >= 0.1)
                events[i].repeat = true;
    return events;
}
} // namespace zfptest::loop
