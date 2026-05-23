#pragma once
#include <array>
#include <vector>
#include <string>

extern "C" {
#include "aubio.h"
}

enum class Mode {
    Major = 0,
    NaturalMinor,
    HarmonicMinor,
    Dorian,
    Mixolydian,
    Phrygian,
    PhrygianDominant,
    Count
};

enum class GenreProfile {
    Modern = 0,   // kept for API compat — ignored in TDA v1
    Classic,
    Electronic,
    Count
};

enum class InputMode {
    Auto = 0,   // algorithm detects automatically
    Loop,       // force rhythmic-bass path (808/drum tracks)
    Melody      // force melodic path (sparse/solo content)
};

struct KeyCandidate
{
    int   key   { -1 };
    Mode  mode  { Mode::Major };
    float score { 0.f };
};

struct KeyDebugInfo
{
    // Existing fields — preserved for test_key.cpp diagnostics
    int   bassRoot    { -1 };
    float bassFocus   { 0.f };
    int   chordRoot   { -1 };
    float chordFocus  { 0.f };
    int   dtvMaxRoot  { -1 };
    float dtvMaxVote  { 0.f };
    int   ctgMaxRoot  { -1 };
    float ctgMaxVal   { 0.f };
    // Pearson-only top5 (computed for diagnostics when a file fails)
    float pearsonTop5Score[5] { 0,0,0,0,0 };
    int   pearsonTop5Root[5]  { -1,-1,-1,-1,-1 };
    Mode  pearsonTop5Mode[5]  { Mode::Major,Mode::Major,Mode::Major,Mode::Major,Mode::Major };

    // TDA v1 — Tonic Stability Accumulator signals
    float dsVector[12]     {};   // Decay Stability per pitch class
    float rtpVector[12]    {};   // Reverb Tail Persistence per pitch class
    float pciVector[12]    {};   // Phase Coherence Index per pitch class
    float rwmVector[12]    {};   // Rhythmic Weight Map per pitch class
    float tsaVector[12]    {};   // Fused TSA vector
    // Dynamic weights (which signals contributed)
    float cpkVector[12]    {};   // Chord Progression Key per pitch class
    float w_CPK            { 0.f };
    float htfVector[12]    {};   // Harmonic Template Fit per pitch class
    float w_HTF            { 0.f };
    float w_DS             { 0.f };
    float w_RTP            { 0.f };
    float w_PCI            { 0.f };
    float w_RWM            { 0.f };
    float w_MFF            { 0.f };
    float globalConfidence { 0.f };   // TSA[root] - TSA[second]
    // Actual computed TSA contribution weights (for diagnostics)
    float dbg_bassW        { 0.f };
    float dbg_chordW       { 0.f };
    float dbg_ctgW         { 0.f };
    float dbg_ctpW         { 0.f };
    float dbg_dsW          { 0.f };
    float dbg_pciW         { 0.f };
    float dbg_rwmW         { 0.f };
    float dbg_consensusW   { 0.f };
    float dbg_totalW       { 0.f };
    float dbg_dtvConc      { 0.f };   // DTV concentration (how focused is the downbeat vote)
    float mffDeviations[12]{};        // microtonal cent deviations (reserved)
    int   micMode          { -1 };    // mode from Modal Interval Classifier
    float micConfidence    { 0.f };   // Gini score of winning mode
};

std::string modeName(Mode m);
std::string modeNameShort(Mode m);
std::string camelotKey(int root, Mode mode);
std::string genreProfileName(GenreProfile g);

class KeyDetector
{
public:
    KeyDetector()  = default;
    ~KeyDetector();

    void prepare(double sampleRate, int fftSize, int hopSize);
    void reset();

    void processSamples(const float* samples, int numSamples);

    void getKey(int& key, Mode& mode, float& confidence,
                float bpmHint                        = 0.f,
                int   rootNoteHint                   = -1,
                float pitchConf                      = 0.f,
                GenreProfile genre                   = GenreProfile::Modern,
                KeyCandidate* top3                   = nullptr,
                std::vector<float>* stabilityTimeline = nullptr,
                KeyDebugInfo* dbg                    = nullptr,
                InputMode inputMode                  = InputMode::Auto) const;

private:
    aubio_pvoc_t* pvoc  { nullptr };
    fvec_t*       input { nullptr };
    cvec_t*       spec  { nullptr };

    int    fftSz { 4096 };
    int    hop   { 512 };
    double sr    { 44100.0 };
    int    storeBins { 0 };

    std::vector<std::vector<float>> mag;
    std::vector<std::vector<float>> phas;   // phase per frame per bin

    // Profiles kept for stabilityTimeline Pearson visualization only
    static const float kMajor[12];
    static const float kNaturalMinor[12];
    static const float kHarmonicMinor[12];
    static const float kDorian[12];
    static const float kMixolydian[12];
    static const float kPhrygian[12];
    static const float kPhrygianDominant[12];
    static const float kClassicMajor[12];
    static const float kClassicNatMin[12];
    static const float kElecMajor[12];
    static const float kElecNatMin[12];
    static const float kElecHarMin[12];
    static const float kElecDorian[12];
    static const float kElecMixo[12];
    static const float kElecPhrygian[12];
    static const float kElecPhrDom[12];

    static const float* profileFor(Mode m, GenreProfile genre = GenreProfile::Modern);
};
