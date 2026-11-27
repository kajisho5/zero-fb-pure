#include "ui/PluginEditor.h"
#include "ui/LogoPaths.h"
#include "dsp/Decoupler.h"
#include <BinaryData.h>
#include <cmath>
#include <numbers>

namespace zfp
{
namespace
{
// ---- 座標の決まり（docs/ui_master.svg と同じ）
constexpr int kWidth = 720, kHeight = 360;
constexpr float kPlotLeft = 44.0f, kPlotRight = 686.0f, kPlotTop = 58.0f;
constexpr float kPixelsPerDb = 5.2f;                         // 0 dB = y 58、-36 dB = y 245.2
constexpr float kPlotBottom = kPlotTop + 36.0f * kPixelsPerDb;
constexpr float kSpectrumFloorDb = -90.0f;                   // 入力スペクトルの目盛り：下端 -90 dBFS、上端 0 dBFS
constexpr int kMeterSegments = 25;
constexpr int kTimerHz = 30;
const juce::Colour kPlotBackground { 0xff08080A };
const juce::Colour kSpectrumColour { 0xffC8C8D0 };

float xForHz (float hz) noexcept
{
    const float t = std::log (std::clamp (hz, kPlotLowHz, kPlotHighHz) / kPlotLowHz) / std::log (kPlotHighHz / kPlotLowHz);
    return kPlotLeft + t * (kPlotRight - kPlotLeft);
}

float hzForX (float x) noexcept
{
    return kPlotLowHz * std::pow (kPlotHighHz / kPlotLowHz, (x - kPlotLeft) / (kPlotRight - kPlotLeft));
}

float yForDb (float db) noexcept
{
    return std::clamp (kPlotTop - db * kPixelsPerDb, kPlotTop, kPlotBottom);
}

float yForSpectrum (float dbfs) noexcept
{
    const float t = (std::clamp (dbfs, kSpectrumFloorDb, 0.0f) - kSpectrumFloorDb) / -kSpectrumFloorDb;
    return kPlotBottom - t * (kPlotBottom - kPlotTop);
}

// ノッチ 1 本（Q = kNotchQ のベル）の応答 [dB]。Engine::computeNotchTargets と同じ式。
double notchResponseDb (double notchHz, double gainDb, double fHz, double sampleRate) noexcept
{
    const double a = std::pow (10.0, gainDb / 40.0);
    const double w = std::tan (std::numbers::pi * fHz / sampleRate) / std::tan (std::numbers::pi * notchHz / sampleRate);
    const double c = (1.0 - w * w) * (1.0 - w * w);
    const double num = c + (a * w / kNotchQ) * (a * w / kNotchQ);
    const double den = c + (w / (kNotchQ * a)) * (w / (kNotchQ * a));
    return 10.0 * std::log10 (num / den);
}

juce::String formatHzShort (float hz)
{
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + "k" : juce::String (juce::roundToInt (hz));
}

juce::String formatHzLong (float hz)
{
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
}

juce::String formatDb (float db)
{
    return db <= -99.0f ? juce::String ("--") : juce::String (db, 1);
}

float toDb (float amplitude) noexcept
{
    return amplitude > 1.0e-5f ? 20.0f * std::log10 (amplitude) : -100.0f;
}

juce::String jpText (const char* utf8)
{
    return juce::String::fromUTF8 (utf8);
}

bool isState (std::uint8_t value, Analyzer::BandState state) noexcept
{
    return value == std::uint8_t (state);
}

juce::Font makeFont (const juce::Typeface::Ptr& face, float size, float letterSpacing)
{
    return juce::Font (juce::FontOptions (face).withPointHeight (size).withKerningFactor (letterSpacing / size));
}
} // namespace

// ---- 書体
Fonts::Fonts()
{
    spaceMono     = juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceMonoRegular_ttf, size_t (BinaryData::SpaceMonoRegular_ttfSize));
    spaceMonoBold = juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceMonoBold_ttf, size_t (BinaryData::SpaceMonoBold_ttfSize));
    notoJp        = juce::Typeface::createSystemTypefaceFor (BinaryData::NotoSansJPRegularsubset_ttf, size_t (BinaryData::NotoSansJPRegularsubset_ttfSize));
}

juce::Font Fonts::mono (float size, float letterSpacing) const     { return makeFont (spaceMono, size, letterSpacing); }
juce::Font Fonts::monoBold (float size, float letterSpacing) const { return makeFont (spaceMonoBold, size, letterSpacing); }
juce::Font Fonts::jp (float size) const                           { return makeFont (notoJp, size, 0.0f); }

float drawText (juce::Graphics& g, const juce::Font& font, const juce::String& text, float x, float baselineY,
                juce::Colour colour, Anchor anchor)
{
    const float width = juce::GlyphArrangement::getStringWidth (font, text);
    const float left = anchor == Anchor::start ? x : (anchor == Anchor::middle ? x - 0.5f * width : x - width);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, left, baselineY);
    g.setColour (colour);
    glyphs.draw (g);
    return width;
}

// ---- ヘッダーのボタン（60 × 20）
HeaderButton::HeaderButton (const Fonts& f, juce::String text, juce::Colour colourWhenOn)
    : fonts (f), label (std::move (text)), onColour (colourWhenOn)
{
    setTitle (label);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void HeaderButton::paint (juce::Graphics& g)
{
    const juce::Rectangle<float> box (0.5f, 0.5f, 60.0f, 20.0f);
    const float alpha = dimmed ? 0.4f : 1.0f;
    g.setColour ((on ? onColour : Palette::control).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (box, 2.0f);
    g.setColour ((on ? onColour : Palette::border).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (box, 2.0f, 1.0f);
    drawText (g, fonts.monoBold (9.0f, 0.8f), label, 30.5f, 13.4f,
              (on ? Palette::background : Palette::text).withMultipliedAlpha (alpha), Anchor::middle);
}

void HeaderButton::mouseDown (const juce::MouseEvent&)
{
    if (onClick != nullptr)
        onClick();
}

void HeaderButton::setOn (bool shouldBeOn)
{
    if (std::exchange (on, shouldBeOn) != shouldBeOn)
        repaint();
}

void HeaderButton::setDimmed (bool shouldBeDimmed)
{
    if (std::exchange (dimmed, shouldBeDimmed) != shouldBeDimmed)
        repaint();
}

// ---- 横スライダー
// 部品の範囲は、名前の行のベースラインの上 16 px から、つまみの下端（ベースライン +22）の少し下まで。
ParamSlider::ParamSlider (const Fonts& f, juce::RangedAudioParameter& p, int x, int baselineY, int width, juce::String unitText)
    : fonts (f),
      param (p),
      attachment (p, [this] (float) { repaint(); }),
      unit (std::move (unitText))
{
    setTitle (p.getName (32));
    setBounds (x, baselineY - 16, width, 40);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    attachment.sendInitialUpdate();
}

void ParamSlider::paint (juce::Graphics& g)
{
    const float w = float (getWidth()), base = 16.0f; // 名前の行のベースライン
    const float value = param.getValue();
    drawText (g, fonts.monoBold (9.0f, 1.2f), param.getName (32), 0.0f, base, Palette::textDim);
    drawText (g, fonts.monoBold (17.0f), juce::String (param.convertFrom0to1 (value), 1), w - 14.0f, base + 1.0f, Palette::text, Anchor::end);
    drawText (g, fonts.mono (9.0f), unit, w - 11.0f, base + 1.0f, Palette::textDim);

    g.setColour (Palette::control);
    g.fillRect (0.0f, base + 12.0f, w, 6.0f);
    g.setColour (Palette::accent);
    g.fillRect (0.0f, base + 12.0f, w * value, 6.0f);
    g.setColour (Palette::text);
    g.fillRect (std::clamp (w * value - 1.5f, 0.0f, w - 3.0f), base + 8.0f, 3.0f, 14.0f);
}

void ParamSlider::mouseDown (const juce::MouseEvent& e)
{
    accumulated = param.getValue();
    lastDrag = e.position;
    attachment.beginGesture();
}

void ParamSlider::mouseDrag (const juce::MouseEvent& e)
{
    // スライダーの幅で全範囲。Shift を押している間は 1/10 の細かさ。
    const float step = (e.position.x - lastDrag.x) / float (getWidth()) * (e.mods.isShiftDown() ? 0.1f : 1.0f);
    lastDrag = e.position;
    accumulated = std::clamp (accumulated + step, 0.0f, 1.0f);
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (accumulated));
}

void ParamSlider::mouseUp (const juce::MouseEvent&)
{
    attachment.endGesture();
}

void ParamSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void ParamSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float step = wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f) * (e.mods.isShiftDown() ? 0.02f : 0.2f);
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (std::clamp (param.getValue() + step, 0.0f, 1.0f)));
}

// ---- 画面全体
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      resetButton (fonts, "RESET", Palette::accent),
      muteButton (fonts, "MUTE", Palette::alert),
      bypassButton (fonts, "BYPASS", Palette::text),
      strengthSlider (fonts, *p.apvts.getParameter (ParamId::strength), 20, 292, 200, "%"),
      floorSlider (fonts, *p.apvts.getParameter (ParamId::floor), 250, 292, 140, "dB")
{
    setOpaque (true);
    setResizable (false, false);

    logoMark   = juce::Drawable::parseSVGPath (LogoPaths::mark);
    logoZeroFb = juce::Drawable::parseSVGPath (LogoPaths::wordZeroFb);
    logoPure   = juce::Drawable::parseSVGPath (LogoPaths::wordPure);
    spectrumDb.fill (-180.0f);

    resetButton.setBounds (516, 8, 61, 21);
    muteButton.setBounds (582, 8, 61, 21);
    bypassButton.setBounds (648, 8, 61, 21);
    for (auto* c : std::initializer_list<juce::Component*> { &resetButton, &muteButton, &bypassButton, &strengthSlider, &floorSlider })
        addAndMakeVisible (c);

    resetButton.onClick = [this]
    {
        setParam (ParamId::reset, true);
        resetFlashFrames = 5; // 約 150 ms 点灯してから OFF に戻す
        resetButton.setOn (true);
    };
    muteButton.onClick   = [this] { setParam (ParamId::mute, ! paramOn (ParamId::mute)); };
    bypassButton.onClick = [this] { setParam (ParamId::bypass, ! paramOn (ParamId::bypass)); };

    setSize (kWidth, kHeight);
    readState();
    startTimerHz (kTimerHz);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
}

bool PluginEditor::paramOn (const char* id) const
{
    return processor.apvts.getRawParameterValue (id)->load() >= 0.5f;
}

void PluginEditor::setParam (const char* id, bool on)
{
    if (auto* param = processor.apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

void PluginEditor::timerCallback()
{
    if (resetFlashFrames > 0 && --resetFlashFrames == 0)
    {
        setParam (ParamId::reset, false);
        resetButton.setOn (false);
    }
    readState();
    repaint();
}

void PluginEditor::readState()
{
    auto& ui = processor.ui;
    for (size_t b = 0; b < size_t (kNumBands); ++b)
    {
        view.bandGainDb[b] = ui.bandGainDb[b].load (std::memory_order_relaxed);
        view.bandScore[b]  = ui.bandScore[b].load (std::memory_order_relaxed);
        view.bandState[b]  = ui.bandState[b].load (std::memory_order_relaxed);
    }
    for (size_t n = 0; n < size_t (kNumNotches); ++n)
    {
        view.notchFreqHz[n] = ui.notchFreqHz[n].load (std::memory_order_relaxed);
        view.notchGainDb[n] = ui.notchGainDb[n].load (std::memory_order_relaxed);
        view.notchState[n]  = ui.notchState[n].load (std::memory_order_relaxed);
    }
    view.holdSec    = ui.holdSec.load (std::memory_order_relaxed);
    view.guard      = ui.guard.load (std::memory_order_relaxed);
    view.sampleRate = ui.sampleRate.load (std::memory_order_relaxed);
    view.running    = ui.running.load (std::memory_order_relaxed);

    // メーター：ピーク表示、戻りは 20 dB/秒。
    const float fall = 20.0f / float (kTimerHz);
    float inPeak = 0.0f, outPeak = 0.0f;
    for (size_t ch = 0; ch < size_t (kMaxChannels); ++ch)
    {
        inPeak  = std::max (inPeak, ui.inPeak[ch].exchange (0.0f, std::memory_order_relaxed));
        outPeak = std::max (outPeak, ui.outPeak[ch].exchange (0.0f, std::memory_order_relaxed));
    }
    view.inDb  = std::max (toDb (inPeak), std::max (view.inDb - fall, -100.0f));
    view.outDb = std::max (toDb (outPeak), std::max (view.outDb - fall, -100.0f));

    // 入力のスペクトル：上がるときはすぐ、下がるときは 40 dB/秒でゆっくり（ちらつきを抑える）。
    const float spectrumFall = 40.0f / float (kTimerHz);
    for (size_t i = 0; i < spectrumDb.size(); ++i)
        spectrumDb[i] = std::max (ui.spectrumDb[i].load (std::memory_order_relaxed), spectrumDb[i] - spectrumFall);

    // バンド表は実際のサンプルレートで作る（処理部と同じ式）。
    if (! juce::exactlyEqual (view.sampleRate, layoutRate))
    {
        layoutRate = view.sampleRate;
        if (! layout.compute (layoutRate))
            view.running = false;
    }

    view.grDb = 0.0f;
    view.activeBands = 0;
    view.engaged = view.holding = false;
    view.peakHz = 0.0f;
    view.peakGainDb = 0.0f;
    view.peakBand = view.peakNotch = -1;
    if (! view.running)
    {
        curveDb.fill (0.0f);
        return;
    }

    for (size_t b = 0; b < size_t (kNumBands); ++b)
    {
        const float gain = view.bandGainDb[b];
        view.grDb = std::min (view.grDb, gain);
        if (gain < -0.5f)
        {
            ++view.activeBands;
            view.holding = true;
        }
        if (isState (view.bandState[b], Analyzer::BandState::engaged) && view.bandScore[b] >= 0.5f)
        {
            view.engaged = true;
            if (gain <= view.peakGainDb)
            {
                view.peakGainDb = gain;
                view.peakHz = float (layout.fc[b]);
                view.peakBand = int (b);
                view.peakNotch = -1;
            }
        }
    }
    for (size_t n = 0; n < size_t (kNumNotches); ++n)
    {
        const float gain = view.notchGainDb[n];
        if (isState (view.notchState[n], Analyzer::BandState::idle))
            continue;
        view.grDb = std::min (view.grDb, gain);
        if (gain < -0.5f)
        {
            ++view.activeBands;
            view.holding = true;
        }
        if (isState (view.notchState[n], Analyzer::BandState::engaged))
        {
            view.engaged = true;
            if (gain <= view.peakGainDb)
            {
                view.peakGainDb = gain;
                view.peakHz = view.notchFreqHz[n];
                view.peakNotch = int (n);
                view.peakBand = -1;
            }
        }
    }

    // 抑制カーブ：40 バンドと 12 本のノッチの応答の合計。
    const double nyquistLimit = 0.49 * view.sampleRate;
    for (size_t i = 0; i < curveDb.size(); ++i)
    {
        const double f = std::min (double (hzForX (kPlotLeft + float (i))), nyquistLimit);
        double sum = 0.0;
        for (int b = 0; b < kNumBands; ++b)
            if (view.bandGainDb[size_t (b)] < -0.05f)
                sum += Decoupler::bandResponseDb (layout, b, double (view.bandGainDb[size_t (b)]), f, view.sampleRate);
        for (size_t n = 0; n < size_t (kNumNotches); ++n)
            if (view.notchGainDb[n] < -0.05f && view.notchFreqHz[n] > 0.0f)
                sum += notchResponseDb (double (view.notchFreqHz[n]), double (view.notchGainDb[n]), f, view.sampleRate);
        curveDb[i] = float (sum);
    }
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
    g.setColour (kPlotBackground);
    g.fillRect (0, 0, kWidth, 262);

    muteButton.setOn (paramOn (ParamId::mute));
    bypassButton.setOn (paramOn (ParamId::bypass));
    muteButton.setDimmed (paramOn (ParamId::bypass)); // BYPASS 中は MUTE が効かない（SPEC §0.2 D10）

    paintHeader (g);
    paintDisplay (g);
    paintLower (g);
    paintStatus (g);
}

void PluginEditor::paintOverChildren (juce::Graphics& g)
{
    // BYPASS 中はグラフと下段を 40% の濃さにする（ヘッダーと下の帯は除く）。
    if (paramOn (ParamId::bypass))
    {
        g.setColour (kPlotBackground.withAlpha (0.6f));
        g.fillRect (0, 34, kWidth, 228);
        g.setColour (Palette::background.withAlpha (0.6f));
        g.fillRect (0, 263, kWidth, 73);
    }
}

void PluginEditor::paintHeader (juce::Graphics& g)
{
    g.setColour (Palette::accent);
    g.fillPath (logoMark);
    g.setColour (Palette::text);
    g.fillPath (logoZeroFb);
    g.setColour (Palette::accent);
    g.fillPath (logoPure);
    drawText (g, fonts.monoBold (9.0f), "0.0ms / 0smp", LogoPaths::wordEndX + 14.0f, 23.5f, Palette::accent);
}

void PluginEditor::paintDisplay (juce::Graphics& g)
{
    const auto small = fonts.mono (9.0f);
    for (int i = 0; i < 4; ++i)
    {
        const float y = kPlotTop + 12.0f * kPixelsPerDb * float (i);
        g.setColour (i == 0 ? Palette::zeroLine : Palette::border);
        g.fillRect (kPlotLeft, y - 0.5f, kPlotRight - kPlotLeft, 1.0f);
        drawText (g, small, juce::String (-12 * i), kPlotLeft - 6.0f, y + 3.0f, Palette::textDim, Anchor::end);
    }
    static constexpr std::pair<float, const char*> ticks[] = {
        { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" }, { 1000.0f, "1k" },
        { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } };
    for (const auto& [hz, name] : ticks)
    {
        const float x = xForHz (hz);
        g.setColour (Palette::border);
        g.fillRect (x - 0.5f, kPlotTop, 1.0f, kPlotBottom - kPlotTop);
        drawText (g, small, name, x, kPlotBottom + 14.0f, Palette::textDim, Anchor::middle);
    }

    // 入力のスペクトル（灰色）。目盛りは右側（dBFS）で、抑制カーブの目盛りとは別。
    if (view.running)
    {
        juce::Path line, fill;
        fill.startNewSubPath (kPlotLeft, kPlotBottom);
        for (size_t i = 0; i < spectrumDb.size(); ++i)
        {
            const float x = kPlotLeft + (kPlotRight - kPlotLeft) * float (i) / float (spectrumDb.size() - 1);
            const float y = yForSpectrum (spectrumDb[i]);
            if (i == 0)
                line.startNewSubPath (x, y);
            else
                line.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (kPlotRight, kPlotBottom);
        fill.closeSubPath();
        g.setColour (kSpectrumColour.withAlpha (0.10f));
        g.fillPath (fill);
        g.setColour (kSpectrumColour.withAlpha (0.45f));
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }
    for (int d = 0; d >= -90; d -= 30)
        drawText (g, fonts.mono (7.0f), juce::String (d), kPlotRight + 4.0f, yForSpectrum (float (d)) + 3.0f, juce::Colour (0xff5A5A66));
    drawText (g, fonts.mono (8.0f), "INPUT dBFS", kPlotRight - 2.0f, kPlotBottom - 6.0f, juce::Colour (0xff8A8A96), Anchor::end);

    // 抑制カーブ
    juce::Path line, fill;
    fill.startNewSubPath (kPlotLeft, kPlotTop);
    for (size_t i = 0; i < curveDb.size(); ++i)
    {
        const float x = kPlotLeft + float (i), y = yForDb (curveDb[i]);
        if (i == 0)
            line.startNewSubPath (x, y);
        else
            line.lineTo (x, y);
        fill.lineTo (x, y);
    }
    fill.lineTo (kPlotRight, kPlotTop);
    fill.closeSubPath();
    g.setColour (Palette::accent.withAlpha (0.13f));
    g.fillPath (fill);
    g.setColour (Palette::accent);
    g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    // FLOOR の線（破線）
    const float floorDb = processor.apvts.getRawParameterValue (ParamId::floor)->load();
    const float floorY = yForDb (floorDb);
    g.setColour (Palette::textDim);
    const float dashes[] = { 3.0f, 3.0f };
    g.drawDashedLine ({ kPlotLeft, floorY, kPlotRight, floorY }, dashes, 2, 1.0f);
    drawText (g, fonts.mono (8.0f), "FLOOR " + juce::String (juce::roundToInt (floorDb)), kPlotRight - 2.0f, floorY - 4.0f,
              Palette::textDim, Anchor::end);

    paintTags (g);
}

// 抑制している場所の札（周波数）。掴んでいる最中は警告色で塗り、保持中はアクセント色の枠。
// 近すぎる札は重なるので、掴んでいる最中のもの・深いものを優先して、44 px 以内の後のものは線だけにする。
void PluginEditor::paintTags (juce::Graphics& g)
{
    if (! view.running)
        return;

    struct Tag { float hz, gainDb; bool hot; };
    std::array<Tag, size_t (kNumNotches + kNumBands)> tags {};
    size_t count = 0;
    for (size_t n = 0; n < size_t (kNumNotches); ++n)
        if (! isState (view.notchState[n], Analyzer::BandState::idle) && view.notchGainDb[n] < -0.5f && view.notchFreqHz[n] > 0.0f)
            tags[count++] = { view.notchFreqHz[n], view.notchGainDb[n], isState (view.notchState[n], Analyzer::BandState::engaged) };
    for (size_t b = 0; b < size_t (kNumBands); ++b)
        if (isState (view.bandState[b], Analyzer::BandState::engaged) && view.bandScore[b] >= 0.5f)
            tags[count++] = { float (layout.fc[b]), view.bandGainDb[b], true };

    std::sort (tags.begin(), tags.begin() + std::ptrdiff_t (count), [] (const Tag& a, const Tag& b)
    {
        return a.hot != b.hot ? a.hot : a.gainDb < b.gainDb;
    });

    std::array<float, size_t (kNumNotches + kNumBands)> placed {};
    size_t numPlaced = 0;
    bool depthShown = false;
    for (size_t i = 0; i < count; ++i)
    {
        const auto& tag = tags[i];
        const float x = xForHz (tag.hz);
        const auto colour = tag.hot ? Palette::alert : Palette::accent;
        g.setColour (colour);
        g.fillRect (x - 0.5f, kPlotTop - 14.0f, 1.0f, 14.0f);

        bool free = true;
        for (size_t k = 0; k < numPlaced; ++k)
            free = free && std::abs (placed[k] - x) >= 44.0f;
        if (! free)
            continue;
        placed[numPlaced++] = x;

        const float left = std::clamp (x - 21.0f, 2.0f, float (kWidth) - 44.0f);
        const juce::Rectangle<float> box (left, kPlotTop - 30.0f, 42.0f, 16.0f);
        g.setColour (tag.hot ? colour : kPlotBackground);
        g.fillRoundedRectangle (box, 2.0f);
        g.setColour (colour);
        g.drawRoundedRectangle (box, 2.0f, 1.0f);
        drawText (g, fonts.monoBold (9.0f), formatHzShort (tag.hz), left + 21.0f, kPlotTop - 18.5f,
                  tag.hot ? Palette::background : colour, Anchor::middle);

        // いちばん深く掴んでいる 1 本には、深さの数字も出す。
        if (tag.hot && ! depthShown)
        {
            depthShown = true;
            const bool room = x + 10.0f + 60.0f < kPlotRight;
            drawText (g, fonts.monoBold (11.0f), juce::String (tag.gainDb, 1) + " dB", room ? x + 10.0f : x - 10.0f,
                      std::clamp (yForDb (tag.gainDb) + 2.0f, kPlotTop + 14.0f, kPlotBottom - 16.0f), Palette::alert,
                      room ? Anchor::start : Anchor::end);
        }
    }
}

void PluginEditor::paintMeter (juce::Graphics& g, const char* name, float y, int litFrom, int litTo, juce::Colour lit,
                               const juce::String& value)
{
    drawText (g, fonts.monoBold (9.0f), name, 416.0f, y + 8.0f, Palette::textDim);
    for (int i = 0; i < kMeterSegments; ++i)
    {
        g.setColour (i >= litFrom && i < litTo ? lit : Palette::control);
        g.fillRect (444.0f + 5.0f * float (i), y, 4.0f, 8.0f);
    }
    drawText (g, fonts.mono (9.0f), value, 606.0f, y + 8.0f, Palette::text, Anchor::end);
}

void PluginEditor::paintLower (juce::Graphics& g)
{
    g.setColour (Palette::border);
    g.fillRect (0.0f, 262.0f, float (kWidth), 1.0f);

    // メーター：IN・OUT は -60〜0 dBFS を左から、GR は 0〜-36 dB を右から。
    auto levelSegments = [] (float db) { return std::clamp (juce::roundToInt ((db + 60.0f) / 60.0f * float (kMeterSegments)), 0, kMeterSegments); };
    const int grSegments = std::clamp (juce::roundToInt (-view.grDb / 36.0f * float (kMeterSegments)), 0, kMeterSegments);
    paintMeter (g, "IN", 280.0f, 0, levelSegments (view.inDb), Palette::meterOn, formatDb (view.inDb));
    paintMeter (g, "GR", 296.0f, kMeterSegments - grSegments, kMeterSegments, Palette::accent, juce::String (view.grDb, 1));
    paintMeter (g, "OUT", 312.0f, 0, levelSegments (view.outDb), Palette::meterOn, formatDb (view.outDb));

    g.setColour (Palette::border);
    g.fillRect (622.0f, 276.0f, 1.0f, 46.0f);
    // 抑制中の本数（40 バンドと 12 本のノッチの合計）と、いちばん長く残っている保持の残り秒。
    drawText (g, fonts.monoBold (9.0f), "ACTIVE", 634.0f, 288.0f, Palette::textDim);
    drawText (g, fonts.mono (9.0f), juce::String (view.activeBands) + "/" + juce::String (kNumBands + kNumNotches), 704.0f, 288.0f,
              Palette::text, Anchor::end);
    drawText (g, fonts.monoBold (9.0f), "HOLD", 634.0f, 304.0f, Palette::textDim);
    const bool holding = view.holdSec > 0.0f;
    drawText (g, fonts.mono (9.0f), holding ? juce::String (juce::roundToInt (std::ceil (view.holdSec))) + "s" : juce::String ("--"),
              704.0f, 304.0f, holding ? Palette::text : Palette::textDim, Anchor::end);
}

void PluginEditor::paintStatus (juce::Graphics& g)
{
    g.setColour (Palette::border);
    g.fillRect (0.0f, 336.0f, float (kWidth), 1.0f);

    const auto label = fonts.monoBold (9.0f, 0.8f);
    const auto version = "v" + juce::String (JucePlugin_VersionString);

    if (view.running && view.guard != std::uint8_t (Engine::GuardState::normal))
    {
        // 異常ガードの作動中は、帯全体を警告色にする。
        g.setColour (Palette::alert);
        g.fillRect (0.0f, 337.0f, float (kWidth), 23.0f);
        const float w = drawText (g, label, "SAFETY MUTE", 20.0f, 352.0f, Palette::background);
        drawText (g, fonts.jp (9.0f), jpText ("異常な入力を検出。出力を停止中"), 20.0f + w + 12.0f, 352.0f, Palette::background);
        drawText (g, fonts.mono (9.0f), version, 700.0f, 352.0f, Palette::background, Anchor::end);
        return;
    }

    juce::Colour colour = Palette::accent;
    juce::String name = "READY", detail, japanese = jpText ("待機中");
    if (! view.running)
    {
        colour = Palette::textDim;
        name = "UNSUPPORTED";
        japanese = jpText ("対応外の設定（処理なし）");
    }
    else if (paramOn (ParamId::bypass))
    {
        colour = Palette::text;
        name = "BYPASS";
        japanese = jpText ("バイパス中（処理なし）");
    }
    else if (paramOn (ParamId::mute))
    {
        colour = Palette::alert;
        name = "MUTED";
        japanese = jpText ("ミュート中");
    }
    else if (view.engaged)
    {
        colour = Palette::alert;
        name = "SUPPRESSING";
        detail = formatHzLong (view.peakHz) + "  " + juce::String (view.peakGainDb, 1) + " dB";
        japanese = jpText ("発振を検出して抑制中");
    }
    else if (view.holding)
    {
        name = "HOLDING";
        detail = juce::String (view.grDb, 1) + " dB";
        japanese = jpText ("抑制を保持中");
    }

    g.setColour (colour);
    g.fillRect (20.0f, 345.0f, 6.0f, 6.0f);
    float x = 32.0f + drawText (g, label, name, 32.0f, 351.5f, colour);
    if (detail.isNotEmpty())
        x += 8.0f + drawText (g, fonts.mono (9.0f), detail, x + 8.0f, 351.5f, Palette::text);
    drawText (g, fonts.jp (9.0f), japanese, std::max (250.0f, x + 14.0f), 351.5f, Palette::textDim);
    drawText (g, fonts.mono (9.0f), version, 700.0f, 351.5f, Palette::textDim, Anchor::end);
}
} // namespace zfp
