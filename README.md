# ZERO-FB PURE

Zero-latency feedback (howling) suppressor built on plain DSP. No AI inference, no training, cut-only.

**Status: pre-release (v0.1.0).** Signed and notarized macOS builds (VST3 / AU) and unsigned Windows builds (VST3) are on the [Releases](https://github.com/kajisho5/zero-fb-pure/releases) page. The plug-in passes pluginval (strictness 8) and auval in CI, but has not been checked in a DAW or at a real venue yet, and some closed-loop acceptance criteria are still unmet (sections 13–16 of `docs/SPEC.md`).

![ZERO-FB PURE](docs/screens/ui_suppressing.png)

![UI master](docs/ui_master_preview.png)

## What is here

| Path | Contents |
|---|---|
| `docs/SPEC.md` | The single source of truth: formulas, constants, state machine, test criteria (Japanese) |
| `docs/ui_master.svg` | UI design master, 560 x 280 |
| `source/dsp/` | Signal path: DC blocker, 40-band serial SVF EQ, band decoupling, safety guard, soft limiter. No JUCE dependency |
| `tests/` | `ZeroLatencyTest` ZL-1 to ZL-8, level / voice tests, closed-loop bench |
| `bench/` | PF-1 load measurement |

## Build and test

Requires CMake 3.22+ and a C++20 compiler. Catch2 v3.16.1 is fetched automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## 日本語

設計のすべては [`docs/SPEC.md`](docs/SPEC.md) にあります。v0.1.0 はプレリリースで、Mac 用（署名・公証つき）と Windows 用（署名なし）を [Releases](https://github.com/kajisho5/zero-fb-pure/releases) に置いています。自動テストで pluginval（厳しさ 8）と auval に通っていますが、DAW や実際の会場ではまだ確認していません。閉ループの合格基準の一部も未達です（設計書の §13〜§16）。

## License / ライセンス

ZERO-FB PURE is free software, licensed under the **GNU Affero General Public License v3.0** (AGPL-3.0-only). See [`LICENSE`](LICENSE).
Copyright (C) 2026 SEVENTHWELL.

ZERO-FB PURE は AGPLv3 で公開しています。全文は [`LICENSE`](LICENSE) にあります。

Third-party components / 同梱・利用している外部の部品:

| Component | License | Where |
|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) 9.0.6 | AGPLv3 | fetched at build time (plug-in) |
| DM Sans (outlines used for the logo lettering) | SIL OFL 1.1 | `assets/logo/OFL-DMSans.txt` |
| Space Mono | SIL OFL 1.1 | `assets/fonts/OFL-SpaceMono.txt` |
| Noto Sans JP (subset) | SIL OFL 1.1 | `assets/fonts/OFL-NotoSansJP.txt` |
| [Catch2](https://github.com/catchorg/Catch2) v3.16.1 | BSL-1.0 | fetched at build time (tests only, not distributed) |
