#include "PluginProcessor.h"
#include "PluginEditor.h"

#if defined(_WIN32)
  #include <windows.h>
#endif

KeyloAudioProcessor::KeyloAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
#if defined(_WIN32)
    // onnxruntime.dll is bundled in the same directory as Keylo.vst3.
    // Windows does NOT search the loaded DLL's directory by default, so hosts
    // (FL Studio, Ableton, etc.) cannot find it via normal DLL search order.
    // We use delay-loading (CMakeLists /DELAYLOAD) so the host can load
    // Keylo.vst3 without needing onnxruntime.dll upfront, then load it
    // explicitly with a full path before creating the Analyser (which
    // triggers the first ORT call).
    auto pluginDir = juce::File::getSpecialLocation(
        juce::File::currentApplicationFile).getParentDirectory();
    auto ortPath = pluginDir.getChildFile("onnxruntime.dll");
    LoadLibraryW(ortPath.getFullPathName().toWideCharPointer());
#endif

    analyser = std::make_unique<Analyser>();
}

KeyloAudioProcessor::~KeyloAudioProcessor() {}

void KeyloAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    analyser->prepare(sampleRate, samplesPerBlock);
    // Auto-start: no user action needed
}

void KeyloAudioProcessor::releaseResources() {}

bool KeyloAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() &&
        out != juce::AudioChannelSet::stereo()) return false;
    return in == out;
}

void KeyloAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;
    analyser->pushBuffer(buffer);
    // Pure analyser — audio unmodified, passes through untouched
}

juce::AudioProcessorEditor* KeyloAudioProcessor::createEditor()
{
    return new KeyloAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KeyloAudioProcessor();
}
