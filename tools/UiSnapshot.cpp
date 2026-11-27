// 画面を PNG に書き出す確認用プログラム。実際のプロセッサに試験用の音を通して、その時点の画面を保存する。
// 使い方: zfp_ui_snapshot <出力フォルダ>
#include "plugin/PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <numbers>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

// 雑音（-50 dBFS 相当）に、指定した周波数の伸びていく音を足して通す。
void feed (juce::AudioProcessor& processor, double seconds, double howlHz, double startDb, double dbPerSecond,
           juce::Random& random, double& phase, double& elapsed)
{
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    const int blocks = int (seconds * kRate / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const double level = howlHz > 0.0 ? std::pow (10.0, std::min (-6.0, startDb + dbPerSecond * elapsed) / 20.0) : 0.0;
            const float noise = (random.nextFloat() * 2.0f - 1.0f) * 0.005f;
            const float v = noise + float (level * std::sin (phase));
            phase += 2.0 * std::numbers::pi * howlHz / kRate;
            elapsed += 1.0 / kRate;
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        processor.processBlock (buffer, midi);
    }
}

void save (juce::AudioProcessorEditor& editor, const juce::File& file)
{
    // タイマーの代わりに、ここで画面の更新を回す。
    juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream stream (file);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
    {
        std::fprintf (stderr, "書き出しに失敗: %s\n", file.getFullPathName().toRawUTF8());
        std::exit (1);
    }
    std::printf ("%s\n", file.getFullPathName().toRawUTF8());
}

void setBool (zfp::PluginProcessor& p, const char* id, bool on)
{
    p.apvts.getParameter (id)->setValueNotifyingHost (on ? 1.0f : 0.0f);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "使い方: zfp_ui_snapshot <出力フォルダ>\n");
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    dir.createDirectory();

    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& processor = dynamic_cast<zfp::PluginProcessor&> (*base);
    processor.setPlayConfigDetails (2, 2, kRate, kBlock);
    processor.prepareToPlay (kRate, kBlock);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setVisible (true);

    juce::Random random (7);
    double phase = 0.0, elapsed = 0.0;

    feed (processor, 1.0, 0.0, 0.0, 0.0, random, phase, elapsed);
    save (*editor, dir.getChildFile ("ui_ready.png"));

    // 2.51 kHz の音が伸びていく → 掴んでいる最中
    elapsed = 0.0;
    feed (processor, 0.45, 2510.0, -60.0, 60.0, random, phase, elapsed);
    save (*editor, dir.getChildFile ("ui_suppressing.png"));

    // 音が止まる → 保持
    feed (processor, 2.0, 0.0, 0.0, 0.0, random, phase, elapsed);
    save (*editor, dir.getChildFile ("ui_holding.png"));

    setBool (processor, zfp::ParamId::mute, true);
    feed (processor, 0.2, 0.0, 0.0, 0.0, random, phase, elapsed);
    save (*editor, dir.getChildFile ("ui_mute.png"));
    setBool (processor, zfp::ParamId::mute, false);

    setBool (processor, zfp::ParamId::bypass, true);
    feed (processor, 0.2, 0.0, 0.0, 0.0, random, phase, elapsed);
    save (*editor, dir.getChildFile ("ui_bypass.png"));
    setBool (processor, zfp::ParamId::bypass, false);

    // 異常な入力（大きすぎる音）→ 安全のためのミュート
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        for (int i = 0; i < kBlock; ++i)
        {
            buffer.setSample (0, i, 40.0f);
            buffer.setSample (1, i, 40.0f);
        }
        processor.processBlock (buffer, midi);
    }
    save (*editor, dir.getChildFile ("ui_guard.png"));
    return 0;
}
