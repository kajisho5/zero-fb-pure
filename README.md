# ZERO-FB PURE

Zero-latency feedback (howling) suppressor built on plain DSP. No AI inference, no training, cut-only.

**Status: work in progress (v0.1.0 not released yet).** Step 1 of 3 is done: the zero-latency signal path and its automated tests. Detection (Step 2) and the plug-in UI / VST3 / AU builds (Step 3) are not implemented yet.

![UI master](docs/ui_master_preview.png)

## What is here

| Path | Contents |
|---|---|
| `docs/SPEC.md` | The single source of truth: formulas, constants, state machine, test criteria (Japanese) |
| `docs/ui_master.svg` | UI design master, 560 x 280 |
| `source/dsp/` | Signal path: DC blocker, 40-band serial SVF EQ, band decoupling, safety guard, soft limiter. No JUCE dependency |
| `tests/` | `ZeroLatencyTest` ZL-1 to ZL-8 |
| `bench/` | PF-1 load measurement |

## Build and test

Requires CMake 3.22+ and a C++20 compiler. Catch2 v3.16.1 is fetched automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## 日本語

設計のすべては [`docs/SPEC.md`](docs/SPEC.md) にあります。現在は 3 段階のうち Step 1（遅延 0 の信号経路と自動テスト）まで完了しています。
