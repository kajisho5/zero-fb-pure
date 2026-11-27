// ZeroLatencyTest（SPEC §8.2）。ZL-1〜ZL-8。
#include "AllocCounter.h"
#include "TestHelpers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>
#include <limits>

using namespace zfptest;

namespace
{
struct Bus { int in, out; };

Buffer noiseInput (int channels, std::size_t n, float amplitude)
{
    Buffer b;
    for (int c = 0; c < channels; ++c)
        b.push_back (whiteNoise (n, amplitude, 1234567u + 7919u * std::uint32_t (c)));
    return b;
}

std::array<float, zfp::kNumBands> uniformTargets (float db)
{
    std::array<float, zfp::kNumBands> t;
    t.fill (db);
    return t;
}

bool identical (const std::vector<float>& a, const std::vector<float>& b, std::size_t begin = 0)
{
    for (std::size_t i = begin; i < a.size(); ++i)
        if (! (a[i] == b[i])) // 誤差 0.0 を要求（NaN も不一致として扱う）
            return false;
    return true;
}
} // namespace

TEST_CASE ("ZL-1 BYPASS=ON で入力とビット完全一致", "[ZL-1]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    const Bus bus   = GENERATE (Bus { 1, 1 }, Bus { 2, 2 }, Bus { 1, 2 });
    const auto n    = std::size_t (fs * 10.0);

    zfp::Engine e;
    e.setBypass (true);
    REQUIRE (e.prepare (fs, bus.in, bus.out));
    const auto targets = uniformTargets (-12.0f); // 処理側は動かしたままにする
    e.setTestTargetsDb (targets.data());

    const Buffer in = noiseInput (bus.in, n, 0.5f);
    Buffer out = makeBuffer (std::size_t (bus.out), n);
    processRange (e, in, out, 0, n, 64);

    CHECK (identical (out[0], in[0]));
    if (bus.out == 2)
        CHECK (identical (out[1], in[bus.in == 2 ? 1 : 0]));
}

TEST_CASE ("ZL-2 STRENGTH=0 の定常状態で入力とビット完全一致", "[ZL-2]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    const Bus bus   = GENERATE (Bus { 1, 1 }, Bus { 2, 2 }, Bus { 1, 2 });
    const auto warm = std::size_t (fs * 0.5);
    const auto wait = std::size_t (fs * 0.1);
    const auto n    = warm + wait + std::size_t (fs * 10.0);

    zfp::Engine e;
    REQUIRE (e.prepare (fs, bus.in, bus.out));
    const auto targets = uniformTargets (-12.0f);
    e.setTestTargetsDb (targets.data());

    const Buffer in = noiseInput (bus.in, n, 0.5f);
    Buffer out = makeBuffer (std::size_t (bus.out), n);
    processRange (e, in, out, 0, warm, 64);
    CHECK_FALSE (identical (out[0], in[0])); // 処理が実際に効いていたこと
    e.setStrengthPercent (0.0f);
    processRange (e, in, out, warm, n, 64);

    CHECK (identical (out[0], in[0], warm + wait));
    if (bus.out == 2)
        CHECK (identical (out[1], in[bus.in == 2 ? 1 : 0], warm + wait));
}

TEST_CASE ("ZL-3 遅延 0 サンプル・プリリンギングなし", "[ZL-3]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    const std::size_t n = 8192, n0 = 4000;

    zfp::Engine e;
    REQUIRE (e.prepare (fs, 2, 2));
    CHECK (e.getLatencySamples() == 0);
    const auto targets = uniformTargets (-12.0f);
    e.setTestTargetsDb (targets.data());

    Buffer in = makeBuffer (2, n), out = makeBuffer (2, n);
    in[0][n0] = 0.5f;
    in[1][n0] = 0.5f;
    processRange (e, in, out, 0, n, 16);

    for (int c = 0; c < 2; ++c)
    {
        bool silentBefore = true;
        for (std::size_t i = 0; i < n0; ++i)
            silentBefore = silentBefore && out[size_t (c)][i] == 0.0f;
        CHECK (silentBefore);
        CHECK (out[size_t (c)][n0] != 0.0f);
    }
}

namespace
{
// 目標を固定したときの、周波数 f での EQ の応答 [dB]。
// 全バンド 0 dB のときの出力を基準にする（DC Blocker の分を差し引くため）。
double measureEqResponseDb (double fs, const std::array<float, zfp::kNumBands>& targets, double f)
{
    const auto settle  = std::size_t (fs * 1.0);
    const auto measure = std::size_t (fs * 0.25);
    const auto n = settle + measure;
    const Buffer in { sine (n, f, fs, 0.1f) }; // -20 dBFS

    auto run = [&] (const float* t)
    {
        zfp::Engine e;
        e.setFloorDb (-36.0f);
        REQUIRE (e.prepare (fs, 1, 1));
        e.setTestTargetsDb (t);
        Buffer out = makeBuffer (1, n);
        processRange (e, in, out, 0, n, 512);
        return toneAmplitude (out[0], settle, n, f, fs);
    };

    const auto flat = uniformTargets (0.0f);
    return toDb (run (targets.data()) / run (flat.data()));
}
} // namespace

TEST_CASE ("ZL-4a 全バンド一様 -12 dB で全中心周波数が -12 ±1.0 dB", "[ZL-4]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    zfp::BandLayout l;
    REQUIRE (l.compute (fs));

    double worst = 0.0;
    for (int b = 0; b < zfp::kNumBands; ++b)
    {
        const double r = measureEqResponseDb (fs, uniformTargets (-12.0f), l.fc[size_t (b)]);
        worst = std::max (worst, std::abs (r + 12.0));
    }
    INFO ("fs=" << fs << " 最大誤差 [dB] = " << worst);
    CHECK (worst <= 1.0);
}

TEST_CASE ("ZL-4b 単一バンド -12 dB でその中心が -12 ±0.5 dB", "[ZL-4]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    zfp::BandLayout l;
    REQUIRE (l.compute (fs));

    double worst = 0.0;
    for (int b = 0; b < zfp::kNumBands; ++b)
    {
        auto t = uniformTargets (0.0f);
        t[size_t (b)] = -12.0f;
        const double r = measureEqResponseDb (fs, t, l.fc[size_t (b)]);
        worst = std::max (worst, std::abs (r + 12.0));
    }
    INFO ("fs=" << fs << " 最大誤差 [dB] = " << worst);
    CHECK (worst <= 0.5);
}

namespace
{
// ブロック長を変えても結果が同じかを見るための台本。途中で設定を切り替える。
Buffer runScenario (std::size_t blockSize, const Buffer& in)
{
    const double fs = 48000.0;
    const std::size_t n = in[0].size();
    zfp::Engine e;
    REQUIRE (e.prepare (fs, 2, 2));
    Buffer out = makeBuffer (2, n);

    auto t = uniformTargets (0.0f);
    std::size_t pos = 0;
    auto advance = [&] (std::size_t to) { processRange (e, in, out, pos, to, blockSize); pos = to; };

    advance (9000);
    t[15] = -18.0f; t[22] = -9.0f;  e.setTestTargetsDb (t.data());
    advance (30011);
    e.setMute (true);
    advance (33000);
    e.setMute (false);
    t[5] = -24.0f;                  e.setTestTargetsDb (t.data());
    advance (52345);
    e.setBypass (true);
    advance (60000);
    e.setBypass (false);
    e.setFloorDb (-12.0f);
    advance (75001);
    e.requestReset();
    advance (90000);
    e.setStrengthPercent (0.0f);
    advance (95000);
    e.setStrengthPercent (60.0f);
    advance (n);
    return out;
}
} // namespace

TEST_CASE ("ZL-5 ブロック長を変えても出力が完全に一致", "[ZL-5]")
{
    const std::size_t n = 144000; // 3 秒
    Buffer in = noiseInput (2, n, 0.25f);
    const auto tone = sine (n, 1000.0, 48000.0, 0.3f);
    for (std::size_t i = 48000; i < n; ++i) // 途中から 1 kHz のサイン波
    {
        in[0][i] += tone[i];
        in[1][i] += tone[i];
    }
    in[0][110000] = std::numeric_limits<float>::quiet_NaN(); // 異常ガードも通す

    const Buffer reference = runScenario (1, in);
    for (const std::size_t block : { std::size_t (7), std::size_t (16), std::size_t (31),
                                     std::size_t (64), std::size_t (5000) })
    {
        const Buffer out = runScenario (block, in);
        INFO ("ブロック長 = " << block);
        CHECK (identical (out[0], reference[0]));
        CHECK (identical (out[1], reference[1]));
    }
}

TEST_CASE ("ZL-6 process の中でヒープ確保をしない", "[ZL-6]")
{
    const double fs = 48000.0;
    const std::size_t n = std::size_t (fs * 10.0), block = 16;
    zfp::Engine e;
    REQUIRE (e.prepare (fs, 2, 2));
    Buffer in = noiseInput (2, n, 0.3f);
    in[1][200000] = std::numeric_limits<float>::infinity();
    Buffer out = makeBuffer (2, n);
    auto t = uniformTargets (-6.0f);

    std::size_t allocations = 0;
    {
        const AllocScope scope;
        std::size_t step = 0;
        for (std::size_t pos = 0; pos < n; pos += block, ++step)
        {
            // パラメータを動かしながら処理する。
            if (step % 500 == 0)
            {
                e.setStrengthPercent (float (step % 3000) / 30.0f);
                e.setFloorDb (-36.0f + float (step % 2500) / 100.0f);
                t[step % zfp::kNumBands] = -float (step % 30);
                e.setTestTargetsDb (t.data());
                e.setMute ((step / 500) % 7 == 3);
                e.setBypass ((step / 500) % 11 == 5);
                if ((step / 500) % 13 == 6)
                    e.requestReset();
            }
            const float* inPtr[2] = { in[0].data() + pos, in[1].data() + pos };
            float* outPtr[2] = { out[0].data() + pos, out[1].data() + pos };
            e.process (inPtr, outPtr, int (std::min (block, n - pos)));
        }
        allocations = AllocScope::count();
    }
    CHECK (allocations == 0);
}

TEST_CASE ("ZL-7 NaN と +30 dBFS の注入で安全にミュートし、5 ms で復帰する", "[ZL-7]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    const int variant = GENERATE (0, 1); // 0: NaN を 1 サンプル、1: +30 dBFS を 10 ms
    const std::size_t n = std::size_t (fs * 3.0);
    const std::size_t faultBegin = std::size_t (fs * 1.0);
    const std::size_t faultLen   = variant == 0 ? 1 : std::size_t (fs * 0.010);
    const std::size_t faultEnd   = faultBegin + faultLen;
    const std::size_t fadeLen    = std::size_t (std::lround (fs * 0.005));

    Buffer in { sine (n, 1000.0, fs, 0.25f) };
    Buffer clean = in;
    for (std::size_t i = faultBegin; i < faultEnd; ++i)
        in[0][i] = variant == 0 ? std::numeric_limits<float>::quiet_NaN()
                                : 31.622776f * (i % 2 == 0 ? 1.0f : -1.0f); // +30 dBFS

    zfp::Engine e;
    REQUIRE (e.prepare (fs, 1, 1));
    Buffer out = makeBuffer (1, n);

    // 復帰の様子を 1 サンプルずつ追う。
    std::size_t recoverStart = 0, recoverEnd = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto before = e.getGuardState();
        processRange (e, in, out, i, i + 1, 1);
        const auto after = e.getGuardState();
        if (before == zfp::Engine::GuardState::muted && after == zfp::Engine::GuardState::recover)
            recoverStart = i;
        if (before == zfp::Engine::GuardState::recover && after == zfp::Engine::GuardState::normal)
            recoverEnd = i;
    }

    bool finite = true, belowCeiling = true, silentDuringFault = true;
    for (std::size_t i = 0; i < n; ++i)
    {
        finite = finite && std::isfinite (out[0][i]);
        belowCeiling = belowCeiling && std::abs (out[0][i]) < zfp::kLimiterCeiling;
    }
    for (std::size_t i = faultBegin; i < faultEnd + zfp::kGuardCleanCount - 1; ++i)
        silentDuringFault = silentDuringFault && out[0][i] == 0.0f;

    CHECK (finite);
    CHECK (belowCeiling);
    CHECK (silentDuringFault);
    // 入力が正常に戻ってから 64 サンプル目に復帰を始める。
    CHECK (recoverStart == faultEnd + zfp::kGuardCleanCount - 1);
    // そこから 5 ms（±1 サンプル）でフェードが 1.0 に達する。
    const auto fadeTook = recoverEnd - recoverStart;
    CHECK (fadeTook + 1 >= fadeLen);
    CHECK (fadeTook <= fadeLen + 1);

    // その後 1.2 秒以内に、異常がなかった場合とのレベル差が 1 dB 以内。
    zfp::Engine ref;
    REQUIRE (ref.prepare (fs, 1, 1));
    Buffer refOut = makeBuffer (1, n);
    processRange (ref, clean, refOut, 0, n, 64);
    const std::size_t from = recoverEnd + std::size_t (fs * 1.2);
    const std::size_t to   = from + std::size_t (fs * 0.2);
    REQUIRE (to <= n);
    const double diff = toDb (toneAmplitude (out[0], from, to, 1000.0, fs)
                            / toneAmplitude (refOut[0], from, to, 1000.0, fs));
    INFO ("復帰後のレベル差 [dB] = " << diff);
    CHECK (std::abs (diff) <= 1.0);
}

TEST_CASE ("ZL-8 全ゲイン 0 dB では DC Blocker 以外は何も変えない", "[ZL-8]")
{
    const double fs = 48000.0;
    const float peak = GENERATE (0.001f, 0.1f, 0.4954f); // -60 / -20 / -6.1 dBFS
    const std::size_t n = 96000;

    zfp::Engine e;
    REQUIRE (e.prepare (fs, 1, 1));
    const Buffer in { sine (n, 997.0, fs, peak) };
    Buffer out = makeBuffer (1, n);
    processRange (e, in, out, 0, n, 64);

    zfp::DcBlocker dc;
    dc.prepare (fs);
    bool same = true;
    for (std::size_t i = 0; i < n; ++i)
    {
        const float expected = dc.tick (in[0][i]);
        if (std::abs (expected) <= zfp::kLimiterKnee)
            same = same && out[0][i] == expected;
    }
    CHECK (same);
}

TEST_CASE ("ZL-8 リミッターは折れ点まで素通しで、天井を超えない", "[ZL-8]")
{
    for (float v = -0.5f; v <= 0.5f; v += 0.001f)
        REQUIRE (zfp::softLimit (v) == v);

    float previous = zfp::softLimit (0.5f);
    for (float v = 0.5f; v < 40.0f; v *= 1.01f)
    {
        const float y = zfp::softLimit (v);
        REQUIRE (y < zfp::kLimiterCeiling + 1.0e-6f);
        REQUIRE (y >= previous);            // 単調
        REQUIRE (zfp::softLimit (-v) == -y); // 対称
        previous = y;
    }
    // 折れ点のすぐ上で傾きがほぼ 1（つなぎ目が滑らか）。
    CHECK (std::abs ((zfp::softLimit (0.501f) - 0.5f) / 0.001f - 1.0f) < 0.01f);
}

TEST_CASE ("MUTE は 5 ms で無音になり、BYPASS 中は無視される", "[mute]")
{
    const double fs = 48000.0;
    const std::size_t n = 48000;
    const Buffer in = noiseInput (1, n, 0.3f);

    zfp::Engine e;
    REQUIRE (e.prepare (fs, 1, 1));
    Buffer out = makeBuffer (1, n);
    processRange (e, in, out, 0, 10000, 64);
    e.setMute (true);
    processRange (e, in, out, 10000, 20000, 64);
    bool silent = true;
    for (std::size_t i = 10000 + 241; i < 20000; ++i)
        silent = silent && out[0][i] == 0.0f;
    CHECK (silent);

    e.setBypass (true); // MUTE=ON のまま BYPASS=ON → 入力がそのまま出る
    processRange (e, in, out, 20000, n, 64);
    CHECK (identical (out[0], in[0], 20000 + 300));
}
