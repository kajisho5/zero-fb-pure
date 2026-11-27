#include "ui/PluginEditor.h"
#include "dsp/Decoupler.h"
#include <BinaryData.h>
#include <cmath>
#include <numbers>

namespace zfp
{
namespace
{
// ---- 座標の決まり（docs/ui_master.svg と同じ）
constexpr float kPlotLeft = 44.0f, kPlotRight = 540.0f, kPlotTop = 54.0f, kPlotBottom = 126.0f;
constexpr float kPlotLowHz = 50.0f, kPlotHighHz = 16000.0f;
constexpr float kPixelsPerDb = 2.0f; // 0 dB = y 54、-36 dB = y 126
constexpr int kMeterSegments = 25;
constexpr int kTimerHz = 30;

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
} // namespace

// ---- 書体
Fonts::Fonts()
{
    orbitron      = juce::Typeface::createSystemTypefaceFor (BinaryData::OrbitronBold_ttf, size_t (BinaryData::OrbitronBold_ttfSize));
    spaceMono     = juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceMonoRegular_ttf, size_t (BinaryData::SpaceMonoRegular_ttfSize));
    spaceMonoBold = juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceMonoBold_ttf, size_t (BinaryData::SpaceMonoBold_ttfSize));
    notoJp        = juce::Typeface::createSystemTypefaceFor (BinaryData::NotoSansJPRegularsubset_ttf, size_t (BinaryData::NotoSansJPRegularsubset_ttfSize));
}

static juce::Font makeFont (const juce::Typeface::Ptr& face, float size, float letterSpacing)
{
    return juce::Font (juce::FontOptions (face).withPointHeight (size).withKerningFactor (letterSpacing / size));
}

juce::Font Fonts::logo (float size) const                         { return makeFont (orbitron, size, 1.6f); }
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

// ---- ヘッダーのボタン
HeaderButton::HeaderButton (const Fonts& f, juce::String text, juce::Colour colourWhenOn)
    : fonts (f), label (std::move (text)), onColour (colourWhenOn)
{
    setTitle (label);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void HeaderButton::paint (juce::Graphics& g)
{
    const juce::Rectangle<float> box (0.5f, 0.5f, 62.0f, 20.0f);
    g.setOpacity (1.0f);
    const float alpha = dimmed ? 0.4f : 1.0f;
    g.setColour ((on ? onColour : Palette::control).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (box, 2.0f);
    g.setColour ((on ? onColour : Palette::border).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (box, 2.0f, 1.0f);
    drawText (g, fonts.monoBold (9.0f, 0.8f), label, 31.5f, 14.0f,
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

// ---- ノブ
Knob::Knob (juce::RangedAudioParameter& p, float centreX, float centreY, float arcR, float arcW, float capR,
            float pointerStart, float pointerEnd, bool defaultTick)
    : param (p),
      attachment (p, [this] (float) { repaint(); }),
      centre (centreX, centreY),
      arcRadius (arcR), arcWidth (arcW), capRadius (capR), pointerFrom (pointerStart), pointerTo (pointerEnd),
      showDefaultTick (defaultTick)
{
    setTitle (p.getName (32));
    const int reach = juce::roundToInt (arcR + 12.0f);
    setBounds (juce::roundToInt (centreX) - reach, juce::roundToInt (centreY) - reach, 2 * reach, 2 * reach);
    attachment.sendInitialUpdate();
}

bool Knob::hitTest (int x, int y)
{
    const auto local = juce::Point<float> (float (x + getX()), float (y + getY()));
    return local.getDistanceFrom (centre) <= arcRadius + arcWidth;
}

void Knob::paint (juce::Graphics& g)
{
    g.addTransform (juce::AffineTransform::translation (float (-getX()), float (-getY())));

    // SVG と同じ：左下（135°）から時計回りに 270°。JUCE の角度は 12 時から時計回りなので 90° ずらす。
    const float startAngle = juce::degreesToRadians (225.0f);
    const float sweep = juce::degreesToRadians (270.0f);
    const float value = param.getValue();
    const juce::PathStrokeType stroke (arcWidth, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, startAngle + sweep, true);
    g.setColour (Palette::border);
    g.strokePath (track, stroke);

    if (value > 0.0005f)
    {
        juce::Path filled;
        filled.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, startAngle + sweep * value, true);
        g.setColour (Palette::accent);
        g.strokePath (filled, stroke);
    }

    auto pointAt = [this, startAngle, sweep] (float normalised, float radius)
    {
        const float angle = startAngle + sweep * normalised;
        return juce::Point<float> (centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle));
    };

    if (showDefaultTick)
    {
        // 既定値の位置の目印。
        const float d = param.getDefaultValue();
        g.setColour (Palette::textDim);
        g.drawLine ({ pointAt (d, arcRadius + 4.5f), pointAt (d, arcRadius + 9.5f) }, 1.5f);
    }

    g.setColour (Palette::control);
    g.fillEllipse (centre.x - capRadius, centre.y - capRadius, 2.0f * capRadius, 2.0f * capRadius);
    g.setColour (Palette::border);
    g.drawEllipse (centre.x - capRadius, centre.y - capRadius, 2.0f * capRadius, 2.0f * capRadius, 1.0f);

    g.setColour (Palette::text);
    g.drawLine ({ pointAt (value, pointerFrom), pointAt (value, pointerTo) }, 2.0f);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    accumulated = param.getValue();
    lastDrag = e.position;
    attachment.beginGesture();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    // 200 px で全範囲。Shift を押している間は 1/10 の細かさ。
    const float step = (lastDrag.y - e.position.y) / 200.0f * (e.mods.isShiftDown() ? 0.1f : 1.0f);
    lastDrag = e.position;
    accumulated = std::clamp (accumulated + step, 0.0f, 1.0f);
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (accumulated));
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    attachment.endGesture();
}

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
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
      strengthKnob (*p.apvts.getParameter (ParamId::strength), 280.0f, 196.0f, 33.0f, 5.0f, 26.5f, 10.07f, 23.5f, true),
      floorKnob (*p.apvts.getParameter (ParamId::floor), 421.0f, 198.0f, 21.0f, 4.0f, 15.0f, 5.7f, 12.0f, false)
{
    setOpaque (true);
    setResizable (false, false);

    resetButton.setBounds (350, 8, 63, 21);
    muteButton.setBounds (418, 8, 63, 21);
    bypassButton.setBounds (486, 8, 63, 21);
    for (auto* c : std::initializer_list<juce::Component*> { &resetButton, &muteButton, &bypassButton, &strengthKnob, &floorKnob })
        addAndMakeVisible (c);

    resetButton.onClick = [this]
    {
        setParam (ParamId::reset, true);
        resetFlashFrames = 5; // 約 150 ms 点灯してから OFF に戻す
        resetButton.setOn (true);
    };
    muteButton.onClick   = [this] { setParam (ParamId::mute, ! paramOn (ParamId::mute)); };
    bypassButton.onClick = [this] { setParam (ParamId::bypass, ! paramOn (ParamId::bypass)); };

    setSize (560, 280);
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
    // BYPASS 中は表示部と下段を 40% の濃さにする。
    if (paramOn (ParamId::bypass))
    {
        g.setColour (Palette::background.withAlpha (0.6f));
        g.fillRect (12, 44, 536, 104);
        g.fillRect (12, 156, 536, 96);
    }
}

void PluginEditor::paintHeader (juce::Graphics& g)
{
    juce::Path mark;
    mark.startNewSubPath (12.0f, 18.0f);
    mark.lineTo (17.0f, 18.0f);
    mark.lineTo (21.0f, 26.0f);
    mark.lineTo (25.0f, 18.0f);
    mark.lineTo (30.0f, 18.0f);
    g.setColour (Palette::accent);
    g.strokePath (mark, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    g.setColour (Palette::text);
    g.fillRect (12.0f, 10.0f, 18.0f, 2.0f);

    const auto logoFont = fonts.logo (14.0f);
    const float w = drawText (g, logoFont, "ZERO-FB", 38.0f, 23.0f, Palette::text);
    drawText (g, logoFont, "PURE", 38.0f + w + 7.0f, 23.0f, Palette::accent);

    g.setColour (Palette::accent);
    g.drawRect (juce::Rectangle<float> (190.0f, 10.0f, 97.0f, 17.0f), 1.0f);
    drawText (g, fonts.monoBold (9.0f), "0.0ms / 0smp", 238.5f, 22.0f, Palette::accent, Anchor::middle);

    g.setColour (Palette::border);
    g.fillRect (0.0f, 36.0f, 560.0f, 1.0f);
}

void PluginEditor::paintDisplay (juce::Graphics& g)
{
    const juce::Rectangle<float> box (12.5f, 44.5f, 535.0f, 103.0f);
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (box, 2.0f);
    g.setColour (Palette::border);
    g.drawRoundedRectangle (box, 2.0f, 1.0f);

    const auto small = fonts.mono (8.0f);
    for (int i = 0; i < 4; ++i)
    {
        const float y = kPlotTop + 24.0f * float (i);
        g.setColour (i == 0 ? Palette::zeroLine : Palette::border);
        g.fillRect (kPlotLeft, y - 0.5f, kPlotRight - kPlotLeft, 1.0f);
        drawText (g, small, juce::String (-12 * i), 38.0f, y + 3.0f, Palette::textDim, Anchor::end);
    }
    static constexpr std::pair<float, const char*> ticks[] = {
        { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" }, { 1000.0f, "1k" },
        { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } };
    for (const auto& [hz, name] : ticks)
    {
        const float x = xForHz (hz);
        g.setColour (Palette::border);
        g.fillRect (x - 0.5f, kPlotTop, 1.0f, kPlotBottom - kPlotTop);
        drawText (g, small, name, x, 143.0f, Palette::textDim, Anchor::middle);
    }
    drawText (g, small, "Hz", 38.0f, 143.0f, Palette::textDim, Anchor::end);

    // FLOOR の線（破線）
    const float floorDb = processor.apvts.getRawParameterValue (ParamId::floor)->load();
    const float floorY = yForDb (floorDb);
    g.setColour (Palette::textDim);
    const float dashes[] = { 3.0f, 3.0f };
    g.drawDashedLine ({ kPlotLeft, floorY, kPlotRight, floorY }, dashes, 2, 1.0f);
    drawText (g, small, "FLOOR", 538.0f, floorY - 3.0f, Palette::textDim, Anchor::end);

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
    g.setColour (Palette::accent.withAlpha (0.14f));
    g.fillPath (fill);
    g.setColour (Palette::accent);
    g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    // 発振を掴んでいる所の目印（縦線と三角）。周波数の数字は、いちばん深い 1 本だけに出す。
    auto marker = [&] (float hz, bool withLabel)
    {
        const float x = xForHz (hz);
        const auto index = size_t (std::clamp (juce::roundToInt (x - kPlotLeft), 0, int (curveDb.size()) - 1));
        const float bottom = std::max (yForDb (curveDb[index]), kPlotTop + 6.0f);
        g.setColour (Palette::alert);
        g.fillRect (x - 0.5f, kPlotTop, 1.0f, bottom - kPlotTop);
        juce::Path triangle;
        triangle.addTriangle (x - 4.0f, 49.0f, x + 4.0f, 49.0f, x, 55.0f);
        g.fillPath (triangle);
        if (withLabel)
        {
            const bool room = x + 12.0f + 34.0f < kPlotRight;
            drawText (g, fonts.monoBold (9.0f), formatHzShort (hz), room ? x + 12.0f : x - 12.0f,
                      std::clamp (bottom - 8.0f, 66.0f, 120.0f), Palette::alert, room ? Anchor::start : Anchor::end);
        }
    };
    if (view.running)
    {
        for (size_t b = 0; b < size_t (kNumBands); ++b)
            if (isState (view.bandState[b], Analyzer::BandState::engaged) && view.bandScore[b] >= 0.5f)
                marker (float (layout.fc[b]), int (b) == view.peakBand);
        for (size_t n = 0; n < size_t (kNumNotches); ++n)
            if (isState (view.notchState[n], Analyzer::BandState::engaged) && view.notchFreqHz[n] > 0.0f)
                marker (view.notchFreqHz[n], int (n) == view.peakNotch);
    }

    // 40 バンドの状態を示す点の列
    for (size_t b = 0; b < size_t (kNumBands); ++b)
    {
        juce::Colour c = Palette::border;
        if (view.running)
        {
            if (isState (view.bandState[b], Analyzer::BandState::engaged) && view.bandScore[b] >= 0.5f)
                c = Palette::alert;
            else if (view.bandGainDb[b] < -0.5f)
                c = Palette::accent;
        }
        g.setColour (c);
        g.fillRect (xForHz (float (layout.fc[b])) - 1.5f, 130.0f, 3.0f, 3.0f);
    }
}

void PluginEditor::paintMeter (juce::Graphics& g, const char* name, float y, int litFrom, int litTo, juce::Colour lit,
                               const juce::String& value)
{
    drawText (g, fonts.monoBold (9.0f), name, 24.0f, y + 7.0f, Palette::textDim);
    for (int i = 0; i < kMeterSegments; ++i)
    {
        g.setColour (i >= litFrom && i < litTo ? lit : Palette::control);
        g.fillRect (48.0f + 4.0f * float (i), y, 3.0f, 8.0f);
    }
    drawText (g, fonts.mono (9.0f), value, 178.0f, y + 7.0f, Palette::text, Anchor::end);
}

void PluginEditor::paintLower (juce::Graphics& g)
{
    const juce::Rectangle<float> box (12.5f, 156.5f, 535.0f, 95.0f);
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (box, 2.0f);
    g.setColour (Palette::border);
    g.drawRoundedRectangle (box, 2.0f, 1.0f);
    g.fillRect (188.0f, 157.0f, 1.0f, 94.0f);
    g.fillRect (372.0f, 157.0f, 1.0f, 94.0f);

    // メーター：IN・OUT は -60〜0 dBFS を左から、GR は 0〜-36 dB を右から。
    auto levelSegments = [] (float db) { return std::clamp (juce::roundToInt ((db + 60.0f) / 60.0f * float (kMeterSegments)), 0, kMeterSegments); };
    const int grSegments = std::clamp (juce::roundToInt (-view.grDb / 36.0f * float (kMeterSegments)), 0, kMeterSegments);
    paintMeter (g, "IN", 177.0f, 0, levelSegments (view.inDb), Palette::meterOn, formatDb (view.inDb));
    paintMeter (g, "GR", 200.0f, kMeterSegments - grSegments, kMeterSegments, Palette::accent, juce::String (view.grDb, 1));
    paintMeter (g, "OUT", 223.0f, 0, levelSegments (view.outDb), Palette::meterOn, formatDb (view.outDb));

    // ノブの名前と値
    auto knobLabel = [&] (const char* name, const juce::String& value, float centreX)
    {
        const auto nameFont = fonts.monoBold (9.0f, 0.8f), valueFont = fonts.monoBold (9.0f);
        const float nameWidth = juce::GlyphArrangement::getStringWidth (nameFont, name);
        const float total = nameWidth + 6.0f + juce::GlyphArrangement::getStringWidth (valueFont, value);
        const float left = centreX - 0.5f * total;
        drawText (g, nameFont, name, left, 244.0f, Palette::textDim);
        drawText (g, valueFont, value, left + nameWidth + 6.0f, 244.0f, Palette::text);
    };
    knobLabel ("STRENGTH", juce::String (strengthKnob.getValue(), 1) + "%", 280.0f);
    knobLabel ("FLOOR", juce::String (floorKnob.getValue(), 1) + "dB", 421.0f);

    // 右の 3 行
    auto stat = [&] (const char* name, const juce::String& value, juce::Colour valueColour, float baseline)
    {
        drawText (g, fonts.monoBold (9.0f), name, 472.0f, baseline, Palette::textDim);
        drawText (g, fonts.mono (9.0f), value, 536.0f, baseline, valueColour, Anchor::end);
        g.setColour (Palette::border);
        g.fillRect (472.0f, baseline + 7.0f, 64.0f, 1.0f);
    };
    // 抑制中の本数。40 バンドと 12 本のノッチを合わせて数える。
    stat ("BANDS", juce::String (view.activeBands) + "/" + juce::String (kNumBands + kNumNotches), Palette::text, 184.0f);
    stat ("PEAK", view.engaged ? formatHzShort (view.peakHz) : juce::String ("--"),
          view.engaged ? Palette::alert : Palette::textDim, 207.0f);
    stat ("HOLD", view.holdSec > 0.0f ? juce::String (juce::roundToInt (std::ceil (view.holdSec))) + "s" : juce::String ("--"),
          view.holdSec > 0.0f ? Palette::text : Palette::textDim, 230.0f);
}

void PluginEditor::paintStatus (juce::Graphics& g)
{
    g.setColour (Palette::border);
    g.fillRect (0.0f, 260.0f, 560.0f, 1.0f);

    const auto label = fonts.monoBold (9.0f, 0.8f);
    const auto version = "v" + juce::String (JucePlugin_VersionString);

    if (view.running && view.guard != std::uint8_t (Engine::GuardState::normal))
    {
        // 異常ガードの作動中は、帯全体を警告色にする。
        g.setColour (Palette::alert);
        g.fillRect (0.0f, 261.0f, 560.0f, 19.0f);
        const float w = drawText (g, label, "SAFETY MUTE", 12.0f, 273.5f, Palette::background);
        drawText (g, fonts.jp (9.0f), jpText ("異常な入力を検出。出力を停止中"), 12.0f + w + 12.0f, 273.5f, Palette::background);
        drawText (g, fonts.mono (9.0f), version, 548.0f, 273.5f, Palette::background, Anchor::end);
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
    g.fillRect (12.0f, 267.0f, 6.0f, 6.0f);
    float x = 24.0f + drawText (g, label, name, 24.0f, 273.5f, colour);
    if (detail.isNotEmpty())
        x += 8.0f + drawText (g, fonts.mono (9.0f), detail, x + 8.0f, 273.5f, Palette::text);
    drawText (g, fonts.jp (9.0f), japanese, std::max (262.0f, x + 12.0f), 273.5f, Palette::textDim);
    drawText (g, fonts.mono (9.0f), version, 548.0f, 273.5f, Palette::textDim, Anchor::end);
}
} // namespace zfp
