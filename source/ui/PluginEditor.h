// ZERO-FB PURE — 画面。見た目の正本は docs/ui_master.svg（560 × 280）。SPEC §7。
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/PluginProcessor.h"

namespace zfp
{
namespace Palette
{
inline const juce::Colour background { 0xff0A0A0C };
inline const juce::Colour panel      { 0xff111115 };
inline const juce::Colour control    { 0xff16161B };
inline const juce::Colour border     { 0xff26262E };
inline const juce::Colour accent     { 0xff18E0D0 };
inline const juce::Colour alert      { 0xffFF5A36 };
inline const juce::Colour text       { 0xffE8E8EC };
inline const juce::Colour textDim    { 0xff7C7C88 };
inline const juce::Colour meterOn    { 0xffC8C8D0 };
inline const juce::Colour zeroLine   { 0xff3A3A46 };
}

// 同梱の書体。SVG の font-size（em の大きさ）と同じ数値で指定する。
struct Fonts
{
    Fonts();
    juce::Font logo (float size) const;
    juce::Font mono (float size, float letterSpacing = 0.0f) const;
    juce::Font monoBold (float size, float letterSpacing = 0.0f) const;
    juce::Font jp (float size) const;

private:
    juce::Typeface::Ptr orbitron, spaceMono, spaceMonoBold, notoJp;
};

enum class Anchor { start, middle, end };
// 文字のベースラインの位置（SVG の x, y と同じ意味）で 1 行描く。描いた幅を返す。
float drawText (juce::Graphics&, const juce::Font&, const juce::String&, float x, float baselineY,
                juce::Colour, Anchor = Anchor::start);

// ヘッダーのボタン（RESET・MUTE・BYPASS）。
class HeaderButton final : public juce::Component
{
public:
    HeaderButton (const Fonts&, juce::String label, juce::Colour onColour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setOn (bool shouldBeOn);
    void setDimmed (bool shouldBeDimmed);
    std::function<void()> onClick;

private:
    const Fonts& fonts;
    juce::String label;
    juce::Colour onColour;
    bool on = false, dimmed = false;
};

// ノブ。上下ドラッグで増減、Shift で微調整、ダブルクリックで既定値、ホイール対応。
class Knob final : public juce::Component
{
public:
    Knob (juce::RangedAudioParameter&, float centreX, float centreY, float arcRadius, float arcWidth,
          float capRadius, float pointerFrom, float pointerTo, bool showDefaultTick);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool hitTest (int x, int y) override;
    float getValue() const { return param.convertFrom0to1 (param.getValue()); }

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::Point<float> centre;
    float arcRadius, arcWidth, capRadius, pointerFrom, pointerTo;
    bool showDefaultTick;
    float dragStartNormalised = 0.0f;
    float accumulated = 0.0f;
    juce::Point<float> lastDrag;
};

class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override {}

private:
    void timerCallback() override;
    void readState();
    void paintHeader (juce::Graphics&);
    void paintDisplay (juce::Graphics&);
    void paintLower (juce::Graphics&);
    void paintStatus (juce::Graphics&);
    void paintMeter (juce::Graphics&, const char* name, float y, int litFrom, int litTo, juce::Colour lit,
                     const juce::String& value);
    bool paramOn (const char* id) const;
    void setParam (const char* id, bool on);

    PluginProcessor& processor;
    Fonts fonts;
    HeaderButton resetButton, muteButton, bypassButton;
    Knob strengthKnob, floorKnob;
    int resetFlashFrames = 0;

    // 画面用に読んだ値
    struct View
    {
        std::array<float, kNumBands> bandGainDb {}, bandScore {};
        std::array<std::uint8_t, kNumBands> bandState {};
        std::array<float, kNumNotches> notchFreqHz {}, notchGainDb {};
        std::array<std::uint8_t, kNumNotches> notchState {};
        float holdSec = 0.0f;
        std::uint8_t guard = 0;
        double sampleRate = 48000.0;
        bool running = false;
        float inDb = -100.0f, outDb = -100.0f; // メーター（戻り 20 dB/秒）
        float grDb = 0.0f;                     // バンドとノッチのうち最も深いゲイン
        int activeBands = 0;                   // 抑制中の本数（バンドとノッチの合計）
        bool engaged = false, holding = false;
        float peakHz = 0.0f, peakGainDb = 0.0f; // いちばん深く抑制している所
        int peakBand = -1, peakNotch = -1;      // その場所（バンドかノッチのどちらか）
    } view;

    BandLayout layout;
    double layoutRate = 0.0;
    std::array<float, 497> curveDb {}; // x = 44〜540 を 1 px 刻み

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
} // namespace zfp
