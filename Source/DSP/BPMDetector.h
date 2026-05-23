#pragma once
#include <vector>
#include <array>

extern "C" {
#include "aubio.h"
}

// ─────────────────────────────────────────────────────────────────────────────
// BPMDetector — BeatGridMatch v5 (Multi-Stream + Multi-Scale)
//
// Three novelty streams, computed per hop and stored separately:
//   • Spectral flux (multi-band Mel HWR log-diff) — drums, percussive attacks
//   • Bass HWR log-diff (40–200 Hz only)         — kick / bass-note attacks
//   • Chromatic flux (Δ chroma vector, HWR sum)  — chord changes, harmonic
//                                                  onsets even on sustained
//                                                  pads/piano with no drums
//
// In getBPM() each stream is per-stream normalised by its own max (or zeroed
// if essentially silent), then summed.  This makes the detector robust across
// content types: drum-heavy → spectral & bass dominate; pure melodic → chr
// flux dominates.  No stream by itself can cause spurious detection when
// another stream is silent.
//
// Tempo selection: BeatGridMatch with MULTI-SCALE grid scoring.  For every
// (T, φ) we compute three averaged alignment scores:
//   • Score_quarter (4 positions / bar, weight ×3)
//   • Score_8th     (8 positions / bar, weight ×2)
//   • Score_16th    (16 positions / bar, weight ×1)
// Combined = (3·Q + 2·8 + 1·16) / 6.  Quarter evidence dominates → triplet
// patterns whose only alignment is at 16th sub-grid cannot win.
//
// Score-curve smoothing (5-tap mean along tempo axis), octave validation
// (T vs T/2 vs T·2), Gaussian popularity prior centred at 110 BPM (σ=40),
// parabolic refinement at the chosen tempo.  Result folded to [70, 180].
// ─────────────────────────────────────────────────────────────────────────────
class BPMDetector
{
public:
    BPMDetector() = default;
    ~BPMDetector();

    void prepare(double sampleRate, int hopSize);
    void reset();

    bool  processSamples(const float* samples, int numSamples);
    float getBPM() const;

private:
    static float normaliseBPM(float bpm);

    // STFT
    aubio_pvoc_t* pvoc  { nullptr };
    fvec_t*       input { nullptr };
    cvec_t*       spec  { nullptr };

    // Mel bands (6) for spectral flux
    static constexpr int kBands = 6;
    int   bandLo[kBands] {};
    int   bandHi[kBands] {};
    float prevBand[kBands] {};

    // Bass band (40–200 Hz) for bass-novelty stream
    int   bassLo { 0 }, bassHi { 0 };
    float prevBass { 0.f };

    // Chromatic-flux state
    int   chromaBinLo { 0 }, chromaBinHi { 0 };
    std::array<float, 12> prevChroma {};

    // Per-stream novelty arrays (one value per hop)
    std::vector<float> specFluxArr;
    std::vector<float> bassDiffArr;
    std::vector<float> chrFluxArr;

    int    fftSz  { 1024 };
    int    hop    { 512 };
    double sr     { 44100.0 };
    int    hopNum { 0 };

    // RT preview (aubio_tempo running average)
    aubio_tempo_t* tempo   { nullptr };
    fvec_t*        tInput  { nullptr };
    fvec_t*        tOutput { nullptr };
    float rtBPM { 0.f }, rtAccum { 0.f }, rtWeight { 0.f };
};
