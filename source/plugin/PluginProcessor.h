// ZERO-FB PURE — JUCE のプロセッサ。SPEC §6。
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"
#include "plugin/UiState.h"

namespace zfp
{
namespace ParamId
{
inline constexpr const char* strength = "strength";
inline constexpr const char* floor    = "floor";
inline constexpr const char* reset    = "reset";
inline constexpr const char* mute     = "mute";
inline constexpr const char* bypass   = "bypass";
}

inline constexpr float kStrengthDefault = 75.0f;
inline constexpr float kFloorDefault    = -24.0f;

class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;
    void reset() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioParameterBool* getBypassParameter() const override { return bypassParam; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    UiState ui;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publish (const juce::AudioBuffer<float>& buffer, int numIn, int numOut, bool beforeProcess) noexcept;
    void copyMonoToStereo (juce::AudioBuffer<float>& buffer) const noexcept;

    Engine engine;
    bool engineReady = false;
    // 表示点ごとの、解析のビンの範囲（prepareToPlay で作る）。
    std::array<int, size_t (kSpectrumPoints)> spectrumFirstBin {}, spectrumLastBin {};
    // 範囲にビンが 1 本も入らない低い周波数では、両隣のビンの間をこの割合で補間する（0 未満なら補間しない）。
    std::array<float, size_t (kSpectrumPoints)> spectrumBlend {};
    unsigned lastSpectrumFrame = 0;
    bool lastReset = false;

    std::atomic<float>* strengthValue = nullptr;
    std::atomic<float>* floorValue = nullptr;
    std::atomic<float>* resetValue = nullptr;
    std::atomic<float>* muteValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
} // namespace zfp
