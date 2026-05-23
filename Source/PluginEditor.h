#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace K {
    static constexpr int W = 380;
    static constexpr int H = 322;

    static constexpr int kPad     = 16;
    static constexpr int kHeaderH = 34;
    static constexpr int kHeroH   = 170;
    static constexpr int kBpmH    = 40;
    static constexpr int kAltH    = 30;
    static constexpr int kAlgH    = 12;   // algorithm weight bar section
    static constexpr int kFootH   = 36;
    // derived y positions — must sum to H
    static constexpr int kHeroY   = kHeaderH;
    static constexpr int kBpmY    = kHeroY + kHeroH;
    static constexpr int kAltY    = kBpmY  + kBpmH;
    static constexpr int kAlgY    = kAltY  + kAltH;
    static constexpr int kFootY   = kAlgY  + kAlgH;

    // Palette
    static const juce::Colour Bg      (0xFF090909u);
    static const juce::Colour Surface (0xFF111115u);
    static const juce::Colour Border  (0xFF1E1E24u);
    static const juce::Colour Accent  (0xFF6B67F0u);   // violet — S-KEY
    static const juce::Colour AccentDim(0xFF3A3880u);
    static const juce::Colour Green   (0xFF3DD68Cu);
    static const juce::Colour Teal    (0xFF2DCFB3u);   // teal — TSA proprietary
    static const juce::Colour TextPri (0xFFF0F0F3u);
    static const juce::Colour TextSec (0xFF72727Cu);
    static const juce::Colour TextDim (0xFF3A3A42u);
}

class KeyloAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::Timer
{
public:
    explicit KeyloAudioProcessorEditor(KeyloAudioProcessor&);
    ~KeyloAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    struct NoteDisplay { juce::String letter; juce::String accidental; };

    juce::Rectangle<int> resetButtonBounds() const;
    bool isOverResetButton(juce::Point<float> p) const;

    void drawHeader(juce::Graphics&);
    void drawHero(juce::Graphics&);
    void drawBpmStrip(juce::Graphics&);
    void drawAltRow(juce::Graphics&);
    void drawAlgoBar(juce::Graphics&);
    void drawFooter(juce::Graphics&);
    void drawControlPills(juce::Graphics&);
    void drawStabilityTimeline(juce::Graphics&);

    void drawKeyHero(juce::Graphics&, juce::Rectangle<float> area);
    void drawListeningState(juce::Graphics&, juce::Rectangle<float> area);
    void drawAnalysingState(juce::Graphics&, juce::Rectangle<float> area);
    void drawProgressBar(juce::Graphics&, juce::Rectangle<float>, float progress);
    void drawNoteLetter(juce::Graphics&, juce::Rectangle<float>, const NoteDisplay&);

    static NoteDisplay noteDisplayForKey(int key);
    static juce::String modeLabel(Mode mode);
    static juce::String candidateLabel(const KeyCandidate& c);

    static juce::Typeface::Ptr panchangTypeface();
    static juce::Font panchang(float size);
    static juce::Font uiFont(float size, bool bold = false);

    // Input mode pills (LOOP=0, MELODY=1)
    juce::Rectangle<int> modePillBounds(int idx) const;
    // CAM pill (index 0)
    juce::Rectangle<int> camPillBounds() const;

    void drawPill(juce::Graphics&, juce::Rectangle<float> r,
                  const char* label, bool active, bool leftInGroup, bool rightInGroup);

    bool isSilentCollecting() const noexcept;

    KeyloAudioProcessor& processor;
    AnalysisResult cachedResult;
    AnalyserState  cachedState   { AnalyserState::Idle };
    float          cachedLevelDB { -96.f };
    float uiProgress { 0.f };
    int   animTick   { 0 };
    bool  resetHover { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyloAudioProcessorEditor)
};
