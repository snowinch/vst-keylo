#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <string>
#include <vector>
#include "DSP/BPMDetector.h"
#include "DSP/KeyDetector.h"
#include "DSP/PitchDetector.h"
#include "DSP/SkeyInference.h"

// ── Result ────────────────────────────────────────────────────────────────────
struct AnalysisResult
{
    float  bpm          { 0.f };
    int    key          { -1 };
    Mode   mode         { Mode::Major };
    int    rootNote     { -1 };
    float  energy       { -96.f };
    float  confidence   { 0.f };
    float  secondsAccum { 0.f };
    std::string camelot {};        // e.g. "8A", "4B"

    // Top 2 alternative candidates
    KeyCandidate alt1 {};
    KeyCandidate alt2 {};

    // Per-segment Pearson scores [0..1] — 16 elements when available
    std::vector<float> stabilityTimeline {};

    // Algorithm weight: 0 = pure TSA, 1 = pure S-KEY (for UI bar)
    float skeyWeight { 0.f };

    static const char* noteName(int n);
    std::string keyString()  const;
    std::string modeString() const;
    std::string rootString() const;
    std::string bpmString()  const;
};

// ── State ─────────────────────────────────────────────────────────────────────
enum class AnalyserState
{
    Idle,
    Collecting,
    Analysing,
    Ready
};

// ── Analyser ──────────────────────────────────────────────────────────────────
class Analyser
{
public:
    static constexpr float kSampleSecs  = 12.f;
    static constexpr float kMinSeconds  = 4.f;

    // Signal level above this dBFS triggers auto-start in Idle state
    static constexpr float kAutoStartDB = -50.f;

    Analyser();
    ~Analyser();

    void prepare(double sampleRate, int blockSize);
    void pushBuffer(const juce::AudioBuffer<float>& buffer);

    void startCollecting();
    void resumeCollecting();
    void reset();

    void setGenre(GenreProfile g) noexcept { currentGenre.store((int)g); }
    GenreProfile getGenre() const noexcept { return (GenreProfile)currentGenre.load(); }

    void      setInputMode(InputMode m) noexcept { currentInputMode.store((int)m); }
    InputMode getInputMode() const noexcept      { return (InputMode)currentInputMode.load(); }

    void setCamelotDisplay(bool on) noexcept { camelotDisplay.store(on); }
    bool getCamelotDisplay() const noexcept  { return camelotDisplay.load(); }

    // Call from UI timer — processes pending auto-start off the audio thread.
    bool pollAutoStart();

    AnalyserState  getState()        const noexcept;
    AnalysisResult getResult()       const;
    float          getLevelDB()      const noexcept;
    float          getSecondsAccum() const;

private:
    void triggerAnalysis();
    void runAnalysis();

    double sr { 44100.0 };

    std::atomic<int>   state           { (int)AnalyserState::Idle };
    std::atomic<float> levelDB         { -96.f };
    std::atomic<bool>  pendingAutoStart { false };
    std::atomic<int>   currentGenre     { (int)GenreProfile::Modern };
    std::atomic<int>   currentInputMode { (int)InputMode::Loop };
    std::atomic<bool>  camelotDisplay   { false };
    std::atomic<bool>  readyPaused      { false }; // true after sustained silence in Ready state
    std::atomic<int>   readySilenceFrames { 0 };   // consecutive below-threshold buffers in Ready

    static constexpr int kMaxSeconds = 14;
    static constexpr int kHop = 512;
    mutable juce::SpinLock ringLock;
    std::vector<float> ring;
    int ringSize     { 0 };
    int writeHead    { 0 };
    int totalWritten { 0 };

    int targetSamples { 0 };

    std::vector<float> rtAccum;
    int rtAccumPos { 0 };
    BPMDetector rtBpmDet;

    mutable juce::SpinLock resultLock;
    AnalysisResult result;

    std::unique_ptr<juce::Thread>    worker;
    std::unique_ptr<SkeyInference>   skey;
};
