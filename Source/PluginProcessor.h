#pragma once
#include <JuceHeader.h>
#include "Analyser.h"
#include <memory>

class KeyloAudioProcessor : public juce::AudioProcessor
{
public:
    KeyloAudioProcessor();
    ~KeyloAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName()    const override { return "Keylo"; }
    bool acceptsMidi()              const override { return false; }
    bool producesMidi()             const override { return false; }
    bool isMidiEffect()             const override { return false; }
    double getTailLengthSeconds()   const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int)  override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int)   override {}

    Analyser& getAnalyser() { return *analyser; }

    void         setGenre(GenreProfile g)   { analyser->setGenre(g); }
    GenreProfile getGenre() const           { return analyser->getGenre(); }

    void      setInputMode(InputMode m)  { analyser->setInputMode(m); }
    InputMode getInputMode() const       { return analyser->getInputMode(); }

    void setCamelotDisplay(bool on) { analyser->setCamelotDisplay(on); }
    bool getCamelotDisplay() const  { return analyser->getCamelotDisplay(); }

private:
    std::unique_ptr<Analyser> analyser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyloAudioProcessor)
};
