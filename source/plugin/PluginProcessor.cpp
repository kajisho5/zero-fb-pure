#include "plugin/PluginProcessor.h"
#include "ui/PluginEditor.h"

namespace zfp
{
namespace
{
constexpr int kStateVersion = 1;
const juce::Identifier kStateVersionId { "stateVersion" };
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamId::strength, 1 }, "STRENGTH",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), kStrengthDefault,
        AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamId::floor, 1 }, "FLOOR",
        NormalisableRange<float> (kFloorMinDb, kFloorMaxDb, 0.1f), kFloorDefault,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamId::reset, 1 }, "RESET", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamId::mute, 1 }, "MUTE", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamId::bypass, 1 }, "BYPASS", false));
    return layout;
}

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ZeroFbPure", createLayout())
{
    strengthValue = apvts.getRawParameterValue (ParamId::strength);
    floorValue    = apvts.getRawParameterValue (ParamId::floor);
    resetValue    = apvts.getRawParameterValue (ParamId::reset);
    muteValue     = apvts.getRawParameterValue (ParamId::mute);
    bypassValue   = apvts.getRawParameterValue (ParamId::bypass);
    bypassParam   = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamId::bypass));
    jassert (bypassParam != nullptr);
}

// Mono→Mono、Stereo→Stereo、Mono→Stereo だけを受け付ける。SPEC §1。
bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    return (in == mono && out == mono) || (in == stereo && out == stereo) || (in == mono && out == stereo);
}

void PluginProcessor::prepareToPlay (double sampleRate, int)
{
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    engine.setStrengthPercent (strengthValue->load());
    engine.setFloorDb (floorValue->load());
    engine.setMute (muteValue->load() >= 0.5f);
    engine.setBypass (bypassValue->load() >= 0.5f);
    // 対応できない設定（サンプルレートが低すぎる等）のときは、音をそのまま通す。
    engineReady = engine.prepare (sampleRate, numIn, numOut);
    lastReset = resetValue->load() >= 0.5f;
    setLatencySamples (0);

    ui.sampleRate.store (sampleRate);
    ui.numInputs.store (numIn);
    ui.numOutputs.store (numOut);
    ui.running.store (engineReady);
}

void PluginProcessor::reset()
{
    if (engineReady)
        engine.requestReset();
}

void PluginProcessor::copyMonoToStereo (juce::AudioBuffer<float>& buffer) const noexcept
{
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
}

void PluginProcessor::publish (const juce::AudioBuffer<float>& buffer, int numIn, int numOut, bool beforeProcess) noexcept
{
    const int n = buffer.getNumSamples();
    if (beforeProcess)
    {
        for (int ch = 0; ch < std::min (numIn, kMaxChannels); ++ch)
            UiState::pushPeak (ui.inPeak[size_t (ch)], buffer.getMagnitude (ch, 0, n));
        return;
    }

    for (int ch = 0; ch < std::min (numOut, kMaxChannels); ++ch)
        UiState::pushPeak (ui.outPeak[size_t (ch)], buffer.getMagnitude (ch, 0, n));

    const auto& analyzer = engine.getAnalyzer();
    const float* gains  = engine.getBandGainsDb();
    const float* scores = analyzer.bandScores();
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto i = size_t (b);
        ui.bandGainDb[i].store (gains[b], std::memory_order_relaxed);
        ui.bandScore[i].store (scores[b], std::memory_order_relaxed);
        ui.bandState[i].store (std::uint8_t (analyzer.bandState (b)), std::memory_order_relaxed);
    }
    for (int k = 0; k < kNumNotches; ++k)
    {
        const auto i = size_t (k);
        ui.notchFreqHz[i].store (engine.getNotchFreqHz (k), std::memory_order_relaxed);
        ui.notchGainDb[i].store (engine.getNotchGainDb (k), std::memory_order_relaxed);
        ui.notchState[i].store (std::uint8_t (analyzer.notchState (k)), std::memory_order_relaxed);
    }
    ui.holdSec.store (analyzer.longestHoldSec(), std::memory_order_relaxed);
    ui.guard.store (std::uint8_t (engine.getGuardState()), std::memory_order_relaxed);
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();

    if (! engineReady || n == 0 || numIn < 1 || numOut < 1 || buffer.getNumChannels() < numOut)
    {
        copyMonoToStereo (buffer);
        return;
    }

    engine.setStrengthPercent (strengthValue->load (std::memory_order_relaxed));
    engine.setFloorDb (floorValue->load (std::memory_order_relaxed));
    engine.setMute (muteValue->load (std::memory_order_relaxed) >= 0.5f);
    engine.setBypass (bypassValue->load (std::memory_order_relaxed) >= 0.5f);

    // RESET は OFF→ON になった瞬間に 1 回だけ実行する。
    const bool resetNow = resetValue->load (std::memory_order_relaxed) >= 0.5f;
    if (resetNow && ! lastReset)
        engine.requestReset();
    lastReset = resetNow;

    publish (buffer, numIn, numOut, true);

    const float* in[kMaxChannels]  = { buffer.getReadPointer (0), numIn > 1 ? buffer.getReadPointer (1) : nullptr };
    float* out[kMaxChannels]       = { buffer.getWritePointer (0), numOut > 1 ? buffer.getWritePointer (1) : nullptr };
    engine.process (in, out, n);

    publish (buffer, numIn, numOut, false);
}

// ホストが自前でバイパスするとき。入力をそのまま出す（Mono→Stereo は両方へ同じ音）。
void PluginProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    copyMonoToStereo (buffer);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (kStateVersionId, kStateVersion, nullptr);
    // RESET は保存しない（読み込み時は常に OFF）。
    for (int i = 0; i < state.getNumChildren(); ++i)
        if (auto child = state.getChild (i); child.getProperty ("id").toString() == ParamId::reset)
            child.setProperty ("value", 0.0f, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return; // 読めないデータは無視して、今の設定を保つ

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid())
        return;
    if (int (state.getProperty (kStateVersionId, kStateVersion)) > kStateVersion)
        return; // 新しいバージョンで保存されたデータは読まない

    apvts.replaceState (state);
    if (auto* p = apvts.getParameter (ParamId::reset))
        p->setValueNotifyingHost (0.0f);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}
} // namespace zfp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new zfp::PluginProcessor();
}
