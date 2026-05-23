#include "PluginProcessor.h"
#include "PluginEditor.h"

KeyloAudioProcessor::KeyloAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{}

KeyloAudioProcessor::~KeyloAudioProcessor() {}

void KeyloAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    analyser.prepare(sampleRate, samplesPerBlock);
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
    analyser.pushBuffer(buffer);
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
