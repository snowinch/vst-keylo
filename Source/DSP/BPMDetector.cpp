#include "BPMDetector.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <numeric>

BPMDetector::~BPMDetector() { reset(); }

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
void BPMDetector::prepare(double sampleRate, int hopSize)
{
    reset();
    sr    = sampleRate;
    hop   = hopSize;
    fftSz = 1024;

    pvoc  = new_aubio_pvoc((uint_t)fftSz, (uint_t)hop);
    input = new_fvec((uint_t)hop);
    spec  = new_cvec((uint_t)fftSz);

    double binHz = sr / (double)fftSz;
    double melLo = 2595.0 * std::log10(1.0 + 40.0    / 700.0);
    double melHi = 2595.0 * std::log10(1.0 + 10000.0 / 700.0);
    int    nyq   = (int)spec->length - 1;
    for (int b = 0; b < kBands; ++b)
    {
        double m0 = melLo + (melHi - melLo) * (double)b       / (double)kBands;
        double m1 = melLo + (melHi - melLo) * (double)(b + 1) / (double)kBands;
        double f0 = 700.0 * (std::pow(10.0, m0 / 2595.0) - 1.0);
        double f1 = 700.0 * (std::pow(10.0, m1 / 2595.0) - 1.0);
        bandLo[b] = std::max(1,   (int)std::ceil (f0 / binHz));
        bandHi[b] = std::min(nyq, (int)std::floor(f1 / binHz));
        if (bandHi[b] < bandLo[b]) bandHi[b] = bandLo[b];
        prevBand[b] = 0.f;
    }
    bassLo = std::max(1,   (int)std::ceil (40.0  / binHz));
    bassHi = std::min(nyq, (int)std::floor(200.0 / binHz));
    prevBass = 0.f;

    // Chromatic-flux fold range: 150–4000 Hz (avoids LF noise & ringing)
    chromaBinLo = std::max(1,   (int)std::ceil (150.0  / binHz));
    chromaBinHi = std::min(nyq, (int)std::floor(4000.0 / binHz));
    prevChroma.fill(0.f);

    specFluxArr.reserve(2048);
    bassDiffArr.reserve(2048);
    chrFluxArr .reserve(2048);

    tempo   = new_aubio_tempo("specflux", 1024, (uint_t)hop, (uint_t)sr);
    tInput  = new_fvec((uint_t)hop);
    tOutput = new_fvec(1);
    aubio_tempo_set_silence(tempo, -40.f);
    aubio_tempo_set_threshold(tempo, 0.25f);
}

void BPMDetector::reset()
{
    if (pvoc)    { del_aubio_pvoc(pvoc);   pvoc    = nullptr; }
    if (input)   { del_fvec(input);        input   = nullptr; }
    if (spec)    { del_cvec(spec);         spec    = nullptr; }
    if (tempo)   { del_aubio_tempo(tempo); tempo   = nullptr; }
    if (tInput)  { del_fvec(tInput);       tInput  = nullptr; }
    if (tOutput) { del_fvec(tOutput);      tOutput = nullptr; }
    specFluxArr.clear();
    bassDiffArr.clear();
    chrFluxArr.clear();
    hopNum = 0;
    rtBPM = rtAccum = rtWeight = 0.f;
    for (int b = 0; b < kBands; ++b) prevBand[b] = 0.f;
    prevBass = 0.f;
    prevChroma.fill(0.f);
}

float BPMDetector::normaliseBPM(float bpm)
{
    if (bpm <= 0.f) return 0.f;
    while (bpm > 180.f) bpm *= 0.5f;
    while (bpm < 70.f)  bpm *= 2.f;
    return bpm;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-hop: STFT → three independent novelty streams + RT preview
// ─────────────────────────────────────────────────────────────────────────────
bool BPMDetector::processSamples(const float* samples, int numSamples)
{
    if (!pvoc) return false;
    int  written = 0;
    bool updated = false;

    double binHz = sr / (double)fftSz;

    while (written + hop <= numSamples)
    {
        ++hopNum;
        const float* p = samples + written;

        std::memcpy(input->data, p, (size_t)hop * sizeof(float));
        aubio_pvoc_do(pvoc, input, spec);

        // ── Stream 1: Spectral flux (multi-band Mel log-mag HWR diff) ─────────
        float specFlux = 0.f;
        for (int b = 0; b < kBands; ++b)
        {
            float bandE = 0.f;
            for (int k = bandLo[b]; k <= bandHi[b]; ++k)
                bandE += spec->norm[k];
            float logE = std::log(1.f + bandE);
            float diff = logE - prevBand[b];
            if (diff > 0.f) specFlux += diff;
            prevBand[b] = logE;
        }

        // ── Stream 2: Bass HWR log-diff (40–200 Hz only) ──────────────────────
        float bassE = 0.f;
        for (int k = bassLo; k <= bassHi; ++k) bassE += spec->norm[k];
        float bassLog  = std::log(1.f + bassE);
        float bassDiff = bassLog - prevBass;
        if (bassDiff <= 0.f) bassDiff = 0.f;
        prevBass = bassLog;

        // ── Stream 3: Chromatic flux (Δ chroma vector, HWR sum) ───────────────
        // Rough chroma fold (no Gaussian, no harmonics) — sufficient for
        // detecting chord/note changes which is what we need for novelty.
        std::array<float, 12> chromaNow {};
        chromaNow.fill(0.f);
        for (int k = chromaBinLo; k <= chromaBinHi; ++k)
        {
            double freq = (double)k * binHz;
            double semi = 12.0 * std::log2(freq / 261.6255653);
            int    pc   = ((int)std::round(semi)) % 12;
            if (pc < 0) pc += 12;
            chromaNow[(size_t)pc] += spec->norm[k];
        }
        float chromaSum = 0.f;
        for (float v : chromaNow) chromaSum += v;
        if (chromaSum > 1e-9f)
            for (auto& v : chromaNow) v /= chromaSum;

        float chrFlux = 0.f;
        for (int c = 0; c < 12; ++c)
        {
            float d = chromaNow[(size_t)c] - prevChroma[(size_t)c];
            if (d > 0.f) chrFlux += d;
        }
        prevChroma = chromaNow;

        // Suppress first-hop artefacts (zero baseline)
        if (hopNum == 1) { specFlux = 0.f; bassDiff = 0.f; chrFlux = 0.f; }

        specFluxArr.push_back(specFlux);
        bassDiffArr.push_back(bassDiff);
        chrFluxArr .push_back(chrFlux);

        // ── RT preview via aubio_tempo (display only) ─────────────────────────
        std::memcpy(tInput->data, p, (size_t)hop * sizeof(float));
        aubio_tempo_do(tempo, tInput, tOutput);
        float rawBPM = aubio_tempo_get_bpm(tempo);
        if (rawBPM > 40.f && rawBPM < 300.f)
        {
            float n = normaliseBPM(rawBPM);
            rtAccum  = rtAccum * 0.85f + n * 0.15f;
            rtWeight += 1.f;
            if (rtWeight > 8.f) { rtBPM = rtAccum; updated = true; }
        }

        written += hop;
    }
    return updated;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeatGridMatch helpers
// ─────────────────────────────────────────────────────────────────────────────

static inline float maxRange(const std::vector<float>& v, int lo, int hi)
{
    int N = (int)v.size();
    if (lo < 0)   lo = 0;
    if (hi >= N)  hi = N - 1;
    if (lo > hi)  return 0.f;
    float m = v[(size_t)lo];
    for (int i = lo + 1; i <= hi; ++i) if (v[(size_t)i] > m) m = v[(size_t)i];
    return m;
}

// Multi-scale grid score: averaged alignment at quarter / 8th / 16th
// resolutions, combined 3:2:1.  Quarter evidence dominates the result.
static float gridScoreMultiScale(const std::vector<float>& nov,
                                 float period16, float phase, int N)
{
    // ±15% of a 16th — tight enough to reject 9:8 push patterns
    int tol = std::max(1, (int)std::round(period16 * 0.15f));

    float sQ = 0.f, s8 = 0.f, s16 = 0.f;
    int   nQ = 0,   n8 = 0,   n16 = 0;

    int p16 = 0;
    float pos = phase;
    while (pos < (float)N)
    {
        int   c  = (int)std::round(pos);
        float mx = maxRange(nov, c - tol, c + tol);
        int   m  = p16 % 16;

        s16 += mx; ++n16;
        if ((m & 1) == 0) { s8 += mx; ++n8; }
        if ((m & 3) == 0) { sQ += mx; ++nQ; }

        pos += period16;
        ++p16;
    }
    if (nQ < 2) return 0.f;   // not enough quarter clicks → unreliable

    float avgQ  = sQ  / (float)nQ;
    float avg8  = s8  / (float)n8;
    float avg16 = s16 / (float)n16;

    return (3.f * avgQ + 2.f * avg8 + 1.f * avg16) / 6.f;
}

static inline float tempoPrior(float T)
{
    float d = (T - 110.f) / 40.f;
    return std::exp(-0.5f * d * d);
}

// Filter + normalise a stream: require both above noise floor AND clear peaks
// (peak-to-mean ratio > 4 indicates real rhythmic content, not flat noise).
// Returns true if stream kept, false if killed.
static bool filterStream(std::vector<float>& s)
{
    if (s.empty()) return false;
    float mx = 0.f;
    double mn = 0.0;
    for (float v : s) { if (v > mx) mx = v; mn += (double)v; }
    mn /= (double)s.size();

    bool ok = (mx > 0.01f) && (mn < 1e-9 || mx / (float)mn > 4.0f);
    if (!ok)
    {
        std::fill(s.begin(), s.end(), 0.f);
        return false;
    }
    for (auto& v : s) v /= mx;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Final BPM — multi-stream fusion + multi-scale grid match
// ─────────────────────────────────────────────────────────────────────────────
float BPMDetector::getBPM() const
{
    int N = (int)specFluxArr.size();
    if (N < 64) return rtBPM;

    // ── Per-stream quality filter + normalise ────────────────────────────────
    // A stream is "useful" only when it has clear peaks (peak/mean > 4).
    // Flat streams (sustained pad with no transients) are zeroed out — they
    // contribute pure noise after normalisation and would cause spurious locks.
    std::vector<float> sFlux = specFluxArr;
    std::vector<float> sBass = bassDiffArr;
    std::vector<float> sChr  = chrFluxArr;
    int nValid = 0;
    if (filterStream(sFlux)) ++nValid;
    if (filterStream(sBass)) ++nValid;
    if (filterStream(sChr))  ++nValid;

    // No usable rhythmic information → fall back to RT estimate (aubio_tempo)
    if (nValid == 0)
        return rtBPM > 0.f ? rtBPM : 0.f;

    // ── Combined novelty: equal-weighted sum of normalised streams ───────────
    // Each contributes max 1.0 per hop; combined max ≤ 3.0.  Silent streams
    // contribute zero — no spurious additions from noise floor.
    std::vector<float> nov((size_t)N, 0.f);
    for (int i = 0; i < N; ++i)
        nov[(size_t)i] = sFlux[(size_t)i] + sBass[(size_t)i] + sChr[(size_t)i];

    // 3-tap mean smoothing (absorbs sample jitter, preserves peak structure)
    {
        std::vector<float> tmp((size_t)N, 0.f);
        for (int i = 0; i < N; ++i)
        {
            float s = nov[(size_t)i];
            int   c = 1;
            if (i > 0)     { s += nov[(size_t)(i - 1)]; ++c; }
            if (i + 1 < N) { s += nov[(size_t)(i + 1)]; ++c; }
            tmp[(size_t)i] = s / (float)c;
        }
        nov = std::move(tmp);
    }

    double frameRate = sr / (double)hop;

    // ── Tempo + phase sweep with multi-scale grid scoring ─────────────────────
    constexpr float kBpmLo = 60.f, kBpmHi = 200.f, kBpmStep = 0.5f;
    constexpr int   kPhases = 16;

    int   nGrid = (int)((kBpmHi - kBpmLo) / kBpmStep) + 1;
    std::vector<float> scoreCurve((size_t)nGrid, 0.f);

    for (int i = 0; i < nGrid; ++i)
    {
        float T = kBpmLo + (float)i * kBpmStep;
        float periodQ  = (float)(frameRate * 60.0 / (double)T);
        float period16 = periodQ * 0.25f;
        if (period16 < 1.f || period16 * 4.f >= (float)N) continue;

        float bestPhaseScore = 0.f;
        for (int ps = 0; ps < kPhases; ++ps)
        {
            float phase = (float)ps * periodQ / (float)kPhases;
            float s = gridScoreMultiScale(nov, period16, phase, N);
            if (s > bestPhaseScore) bestPhaseScore = s;
        }
        scoreCurve[(size_t)i] = bestPhaseScore * tempoPrior(T);
    }

    // 5-tap mean smoothing on score curve — punishes isolated single-bin peaks
    std::vector<float> smooth((size_t)nGrid, 0.f);
    for (int i = 0; i < nGrid; ++i)
    {
        int lo = std::max(0,         i - 2);
        int hi = std::min(nGrid - 1, i + 2);
        float s = 0.f;
        for (int j = lo; j <= hi; ++j) s += scoreCurve[(size_t)j];
        smooth[(size_t)i] = s / (float)(hi - lo + 1);
    }

    int   bestI = 0;
    float best  = smooth[0];
    for (int i = 1; i < nGrid; ++i)
        if (smooth[(size_t)i] > best) { best = smooth[(size_t)i]; bestI = i; }
    float bestBPM = kBpmLo + (float)bestI * kBpmStep;

    // ── Octave validation (T vs T/2 vs T*2) ───────────────────────────────────
    auto evalAt = [&](float T) -> float {
        if (T < 40.f || T > 250.f) return -1e30f;
        float pQ  = (float)(frameRate * 60.0 / (double)T);
        float p16 = pQ * 0.25f;
        if (p16 < 1.f || p16 * 4.f >= (float)N) return -1e30f;
        float bestP = 0.f;
        for (int ps = 0; ps < kPhases; ++ps)
        {
            float phase = (float)ps * pQ / (float)kPhases;
            float s = gridScoreMultiScale(nov, p16, phase, N);
            if (s > bestP) bestP = s;
        }
        return bestP * tempoPrior(T);
    };

    float candidates[3] = { bestBPM, bestBPM * 0.5f, bestBPM * 2.f };
    float candScores[3] = { evalAt(candidates[0]), evalAt(candidates[1]), evalAt(candidates[2]) };
    int   pick = 0;
    for (int i = 1; i < 3; ++i) if (candScores[i] > candScores[pick]) pick = i;
    float finalBPM = candidates[pick];

    // ── Parabolic refinement around chosen tempo ─────────────────────────────
    {
        float steps[5]  = { -1.0f, -0.5f, 0.f, 0.5f, 1.0f };
        float scores[5] = { 0.f };
        for (int i = 0; i < 5; ++i) scores[i] = evalAt(finalBPM + steps[i]);
        int best5 = 2;
        for (int i = 0; i < 5; ++i) if (scores[i] > scores[best5]) best5 = i;
        if (best5 > 0 && best5 < 4)
        {
            float yL = scores[best5 - 1], y0 = scores[best5], yR = scores[best5 + 1];
            float denom = yL - 2.f * y0 + yR;
            if (std::abs(denom) > 1e-12f)
            {
                float delta = 0.5f * (yL - yR) / denom;
                if (delta > -1.f && delta < 1.f)
                    finalBPM += (steps[best5] + delta * 0.5f);
                else
                    finalBPM += steps[best5];
            }
            else
                finalBPM += steps[best5];
        }
        else
            finalBPM += steps[best5];
    }

    return normaliseBPM(finalBPM);
}
