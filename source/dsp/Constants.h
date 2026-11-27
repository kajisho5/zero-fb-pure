// ZERO-FB PURE — 共通の定数。値の根拠は docs/SPEC.md を参照。
#pragma once

namespace zfp
{
inline constexpr int    kNumBands        = 40;       // SPEC §4.3
inline constexpr int    kMaxChannels     = 2;
inline constexpr int    kNumNotches      = 12;       // 動くノッチの本数  SPEC §14
inline constexpr double kNotchQ          = 30.0;     // 幅 約 1/20 オクターブ
inline constexpr double kBandLowHz       = 60.0;
inline constexpr double kBandHighHz      = 15000.0;
inline constexpr double kKappa           = 1.2;      // SPEC §4.3
inline constexpr double kDcBlockHz       = 20.0;     // SPEC §4.2
inline constexpr float  kGuardThreshold  = 15.848932f; // 10^(24/20)  SPEC §4.6
inline constexpr int    kGuardCleanCount = 64;
inline constexpr double kFadeSeconds     = 0.005;    // 5 ms のフェード・クロスフェード
inline constexpr double kGuardRelSeconds = 1.0;      // ガード復帰後に通常の深さへ戻す時間
inline constexpr float  kLimiterCeiling  = 0.94406088f; // 10^(-0.5/20)  SPEC §4.7
inline constexpr float  kLimiterKnee     = 0.5f;
inline constexpr float  kFloorMinDb      = -36.0f;
inline constexpr float  kFloorMaxDb      = -6.0f;
inline constexpr double kDecoupleRefA    = -6.0;     // SPEC §5.10
inline constexpr double kDecoupleRefC    = -24.0;
inline constexpr int    kDecoupleIters   = 3;
inline constexpr float  kMaxDepthDb      = 36.0f;    // STRENGTH 100% のときの最大の深さ  SPEC §5.9
inline constexpr float  kStrengthPower   = 1.6f;
} // namespace zfp
