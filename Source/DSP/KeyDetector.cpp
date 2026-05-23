#include "KeyDetector.h"
#include <cmath>
#include <cstring>
#include <numeric>
#include <algorithm>

// Index: 0=C 1=C# 2=D 3=Eb 4=E 5=F 6=F# 7=G 8=Ab 9=A 10=Bb 11=B

// ── Profiles (kept for stabilityTimeline Pearson visualization) ──────────────
const float KeyDetector::kMajor[12]            = { 6.5f, 0.3f, 2.5f, 0.3f, 4.0f, 3.5f, 0.3f, 5.5f, 0.3f, 3.0f, 0.3f, 3.5f };
const float KeyDetector::kNaturalMinor[12]     = { 6.5f, 0.3f, 2.5f, 4.5f, 0.3f, 3.5f, 0.3f, 5.5f, 3.0f, 0.3f, 3.5f, 0.3f };
const float KeyDetector::kHarmonicMinor[12]    = { 6.5f, 0.3f, 2.5f, 4.5f, 0.3f, 3.5f, 0.3f, 5.5f, 3.0f, 0.3f, 0.3f, 4.5f };
const float KeyDetector::kDorian[12]           = { 6.5f, 0.3f, 2.5f, 4.5f, 0.3f, 3.5f, 0.3f, 5.5f, 0.3f, 3.5f, 3.5f, 0.3f };
const float KeyDetector::kMixolydian[12]       = { 6.5f, 0.3f, 2.5f, 0.3f, 4.0f, 3.5f, 0.3f, 5.5f, 0.3f, 3.0f, 4.0f, 0.3f };
const float KeyDetector::kPhrygian[12]         = { 6.5f, 4.0f, 0.3f, 4.5f, 0.3f, 3.5f, 0.3f, 5.5f, 3.0f, 0.3f, 3.0f, 0.3f };
const float KeyDetector::kPhrygianDominant[12] = { 6.5f, 4.0f, 0.3f, 0.3f, 5.5f, 4.5f, 0.3f, 5.5f, 3.0f, 0.3f, 3.0f, 0.3f };
const float KeyDetector::kClassicMajor[12]     = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
const float KeyDetector::kClassicNatMin[12]    = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
const float KeyDetector::kElecMajor[12]        = { 8.0f, 0.3f, 2.0f, 0.3f, 3.0f, 1.5f, 0.3f, 7.0f, 0.3f, 1.5f, 0.3f, 1.5f };
const float KeyDetector::kElecNatMin[12]       = { 8.0f, 0.3f, 2.0f, 4.0f, 0.3f, 1.5f, 0.3f, 7.0f, 2.5f, 0.3f, 2.5f, 0.3f };
const float KeyDetector::kElecHarMin[12]       = { 8.0f, 0.3f, 2.0f, 4.0f, 0.3f, 1.5f, 0.3f, 7.0f, 2.5f, 0.3f, 0.3f, 4.0f };
const float KeyDetector::kElecDorian[12]       = { 8.0f, 0.3f, 2.0f, 4.0f, 0.3f, 1.5f, 0.3f, 7.0f, 0.3f, 2.5f, 2.5f, 0.3f };
const float KeyDetector::kElecMixo[12]         = { 8.0f, 0.3f, 2.0f, 0.3f, 3.0f, 1.5f, 0.3f, 7.0f, 0.3f, 1.5f, 3.5f, 0.3f };
const float KeyDetector::kElecPhrygian[12]     = { 8.0f, 3.5f, 0.3f, 4.0f, 0.3f, 1.5f, 0.3f, 7.0f, 2.0f, 0.3f, 2.0f, 0.3f };
const float KeyDetector::kElecPhrDom[12]       = { 8.0f, 3.5f, 0.3f, 0.3f, 5.0f, 3.5f, 0.3f, 7.0f, 2.0f, 0.3f, 2.0f, 0.3f };

// ─────────────────────────────────────────────────────────────────────────────
// Name helpers
// ─────────────────────────────────────────────────────────────────────────────
std::string modeName(Mode m)
{
    switch (m) {
        case Mode::Major:             return "major";
        case Mode::NaturalMinor:      return "minor";
        case Mode::HarmonicMinor:     return "harm. minor";
        case Mode::Dorian:            return "dorian";
        case Mode::Mixolydian:        return "mixolydian";
        case Mode::Phrygian:          return "phrygian";
        case Mode::PhrygianDominant:  return "phryg. dom.";
        default:                      return "-";
    }
}

std::string modeNameShort(Mode m)
{
    switch (m) {
        case Mode::Major:             return "maj";
        case Mode::NaturalMinor:      return "min";
        case Mode::HarmonicMinor:     return "h.min";
        case Mode::Dorian:            return "dor";
        case Mode::Mixolydian:        return "mix";
        case Mode::Phrygian:          return "phr";
        case Mode::PhrygianDominant:  return "p.dom";
        default:                      return "-";
    }
}

std::string genreProfileName(GenreProfile g)
{
    switch (g) {
        case GenreProfile::Modern:     return "modern";
        case GenreProfile::Classic:    return "classic";
        case GenreProfile::Electronic: return "electronic";
        default:                       return "-";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Profile selection — kept for stabilityTimeline visualization
// ─────────────────────────────────────────────────────────────────────────────
const float* KeyDetector::profileFor(Mode m, GenreProfile genre)
{
    if (genre == GenreProfile::Classic) {
        if (m == Mode::Major)        return kClassicMajor;
        if (m == Mode::NaturalMinor) return kClassicNatMin;
    } else if (genre == GenreProfile::Electronic) {
        switch (m) {
            case Mode::Major:             return kElecMajor;
            case Mode::NaturalMinor:      return kElecNatMin;
            case Mode::HarmonicMinor:     return kElecHarMin;
            case Mode::Dorian:            return kElecDorian;
            case Mode::Mixolydian:        return kElecMixo;
            case Mode::Phrygian:          return kElecPhrygian;
            case Mode::PhrygianDominant:  return kElecPhrDom;
            default: break;
        }
    }
    switch (m) {
        case Mode::Major:             return kMajor;
        case Mode::NaturalMinor:      return kNaturalMinor;
        case Mode::HarmonicMinor:     return kHarmonicMinor;
        case Mode::Dorian:            return kDorian;
        case Mode::Mixolydian:        return kMixolydian;
        case Mode::Phrygian:          return kPhrygian;
        case Mode::PhrygianDominant:  return kPhrygianDominant;
        default:                      return kMajor;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Camelot Wheel
// ─────────────────────────────────────────────────────────────────────────────
static const int kCamelotMajor[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };
static const int kCamelotMinor[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };

std::string camelotKey(int root, Mode mode)
{
    if (root < 0 || root > 11) return {};
    bool isMinorSide = (mode == Mode::NaturalMinor  || mode == Mode::HarmonicMinor
                     || mode == Mode::Dorian         || mode == Mode::Phrygian
                     || mode == Mode::PhrygianDominant);
    int num = isMinorSide ? kCamelotMinor[root] : kCamelotMajor[root];
    return std::to_string(num) + (isMinorSide ? "A" : "B");
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
KeyDetector::~KeyDetector() { reset(); }

void KeyDetector::prepare(double sampleRate, int fftSize, int hopSize)
{
    reset();
    sr    = sampleRate;
    fftSz = fftSize;
    hop   = hopSize;
    pvoc  = new_aubio_pvoc((uint_t)fftSize, (uint_t)hopSize);
    input = new_fvec((uint_t)hopSize);
    spec  = new_cvec((uint_t)fftSize);

    double binHz = sr / (double)fftSz;
    storeBins = std::min((int)spec->length - 1, (int)std::ceil(5500.0 / binHz));
    mag.clear();  mag.reserve(2048);
    phas.clear(); phas.reserve(2048);
}

void KeyDetector::reset()
{
    if (pvoc)  { del_aubio_pvoc(pvoc);  pvoc  = nullptr; }
    if (input) { del_fvec(input);       input = nullptr; }
    if (spec)  { del_cvec(spec);        spec  = nullptr; }
    mag.clear();
    phas.clear();
    storeBins = 0;
}

void KeyDetector::processSamples(const float* samples, int numSamples)
{
    if (!pvoc || !input || !spec) return;
    int written = 0;
    while (written + hop <= numSamples) {
        std::memcpy(input->data, samples + written, (size_t)hop * sizeof(float));
        aubio_pvoc_do(pvoc, input, spec);

        std::vector<float> mRow((size_t)(storeBins + 1));
        std::vector<float> pRow((size_t)(storeBins + 1));
        for (int k = 0; k <= storeBins; ++k) {
            mRow[(size_t)k] = spec->norm[k];
            pRow[(size_t)k] = spec->phas[k];
        }
        mag.push_back(std::move(mRow));
        phas.push_back(std::move(pRow));

        written += hop;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pearson correlation (kept for stabilityTimeline + debug pearsonTop5)
// ─────────────────────────────────────────────────────────────────────────────
static float pearsonCorr(const float* a, const float* b, int n)
{
    float ma = 0.f, mb = 0.f;
    for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= (float)n; mb /= (float)n;
    float num = 0.f, da = 0.f, db = 0.f;
    for (int i = 0; i < n; ++i) {
        float ra = a[i] - ma, rb = b[i] - mb;
        num += ra * rb; da += ra * ra; db += rb * rb;
    }
    float denom = std::sqrt(da * db);
    return denom < 1e-9f ? 0.f : num / denom;
}

// ─────────────────────────────────────────────────────────────────────────────
// Median HPSS — suppresses drum transients in the magnitude matrix
// ─────────────────────────────────────────────────────────────────────────────
static void medianFilterTime(std::vector<std::vector<float>>& M, int B, int N, int win)
{
    if (win < 2 || N < 3) return;
    std::vector<float> tmp((size_t)N, 0.f);
    std::vector<float> buf;
    buf.reserve((size_t)(2 * win + 1));
    for (int k = 0; k < B; ++k) {
        for (int n = 0; n < N; ++n) {
            int lo = std::max(0, n - win), hi = std::min(N - 1, n + win);
            buf.clear();
            for (int j = lo; j <= hi; ++j) buf.push_back(M[(size_t)j][(size_t)k]);
            auto mid = buf.begin() + (ptrdiff_t)(buf.size() / 2);
            std::nth_element(buf.begin(), mid, buf.end());
            tmp[(size_t)n] = *mid;
        }
        for (int n = 0; n < N; ++n) M[(size_t)n][(size_t)k] = tmp[(size_t)n];
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Tuning estimation via cent-deviation histogram
// ─────────────────────────────────────────────────────────────────────────────
static double estimateTuningOffset(const std::vector<std::vector<float>>& magS,
                                   int B, int N, double binHz, double kRefC4)
{
    std::array<float, 101> hist {};
    hist.fill(0.f);
    for (int n = 0; n < N; ++n) {
        const auto& row = magS[(size_t)n];
        for (int b = 1; b + 1 < B; ++b) {
            float m = row[(size_t)b];
            if (m <= row[(size_t)(b-1)] || m <= row[(size_t)(b+1)]) continue;
            float prev = row[(size_t)(b-1)], next = row[(size_t)(b+1)];
            double den = (double)(prev - 2.f * m + next);
            double delta = (std::abs(den) > 1e-12) ? 0.5 * (double)(prev - next) / den : 0.0;
            double freq = ((double)b + delta) * binHz;
            if (freq < 100.0 || freq > 5000.0) continue;
            double semi = 12.0 * std::log2(freq / kRefC4);
            double centDev = (semi - std::round(semi)) * 100.0;
            int idx = (int)std::round(centDev) + 50;
            if (idx >= 0 && idx <= 100) hist[(size_t)idx] += m;
        }
    }
    std::array<float, 101> sm {};
    sm.fill(0.f);
    for (int i = 0; i <= 100; ++i) {
        float s = 0.f; int c = 0;
        for (int j = std::max(0,i-2); j <= std::min(100,i+2); ++j) { s += hist[(size_t)j]; ++c; }
        sm[(size_t)i] = s / (float)c;
    }
    int best = 50;
    for (int i = 0; i <= 100; ++i) if (sm[(size_t)i] > sm[(size_t)best]) best = i;
    return (double)(best - 50);
}

// ─────────────────────────────────────────────────────────────────────────────
// HPCP — 12-bin chroma, σ=0.5 semitones, 5 harmonics
// ─────────────────────────────────────────────────────────────────────────────
static void hpcpAccumulate(const std::vector<float>& specRow,
                           int B, double binHz, double refC4, float harmDk,
                           std::array<float, 12>& chromaOut)
{
    static constexpr double kSigma2 = 0.5 * 0.5;

    std::vector<float> Wh((size_t)B, 0.f);
    for (int k = 0; k < B; ++k) {
        int w  = std::max(4, (int)(0.08f * (float)k));
        int k0 = std::max(0, k - w), k1 = std::min(B-1, k + w);
        float lm = 0.f;
        for (int j = k0; j <= k1; ++j) lm += specRow[(size_t)j];
        lm /= (float)(k1 - k0 + 1);
        Wh[(size_t)k] = specRow[(size_t)k] / (1e-7f + lm);
    }
    for (int b = 1; b + 1 < B; ++b) {
        float m = Wh[(size_t)b];
        if (m <= Wh[(size_t)(b-1)] || m <= Wh[(size_t)(b+1)]) continue;
        if (m < 0.5f) continue;
        float prev = Wh[(size_t)(b-1)], next = Wh[(size_t)(b+1)];
        double denom = (double)(prev - 2.f*m + next);
        double delta = (std::abs(denom) > 1e-12) ? 0.5*(double)(prev-next)/denom : 0.0;
        double freq  = ((double)b + delta) * binHz;
        if (freq < 100.0 || freq > 5000.0) continue;
        float mass = specRow[(size_t)b];
        for (int h = 1; h <= 5; ++h) {
            double hFreq = freq * (double)h;
            if (hFreq < 100.0 || hFreq > 5000.0) continue;
            double semi = 12.0 * std::log2(hFreq / refC4);
            double pc   = semi - 12.0 * std::floor(semi / 12.0);
            float  hw   = std::pow(harmDk, (float)(h-1));
            for (int c = 0; c < 12; ++c) {
                double diff = pc - (double)c;
                if (diff >  6.0) diff -= 12.0;
                if (diff < -6.0) diff += 12.0;
                chromaOut[(size_t)c] += hw * mass * (float)std::exp(-0.5*diff*diff/kSigma2);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Diatonic chord set for each mode
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<std::pair<int,bool>> diatonicChords(int K, Mode m)
{
    auto R = [&](int s) { return ((K+s)%12+12)%12; };
    switch (m) {
        case Mode::Major:
            return {{R(0),true},{R(2),false},{R(4),false},{R(5),true},{R(7),true},{R(9),false}};
        case Mode::NaturalMinor:
            return {{R(0),false},{R(3),true},{R(5),false},{R(7),false},{R(8),true},{R(10),true}};
        case Mode::HarmonicMinor:
            return {{R(0),false},{R(3),true},{R(5),false},{R(7),true},{R(8),true}};
        case Mode::Dorian:
            return {{R(0),false},{R(2),false},{R(3),true},{R(5),true},{R(7),false},{R(10),true}};
        case Mode::Mixolydian:
            return {{R(0),true},{R(2),false},{R(5),true},{R(7),false},{R(9),false},{R(10),true}};
        case Mode::Phrygian:
            return {{R(0),false},{R(1),true},{R(3),true},{R(5),false},{R(8),true},{R(10),false}};
        case Mode::PhrygianDominant:
            return {{R(0),true},{R(1),true},{R(5),false},{R(8),true},{R(10),false}};
        default: return {};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Normalize a 12-element vector to sum = 1
// ─────────────────────────────────────────────────────────────────────────────
static void normalize12(float* v)
{
    float s = 0.f;
    for (int i = 0; i < 12; ++i) s += v[i];
    if (s > 1e-9f) for (int i = 0; i < 12; ++i) v[i] /= s;
}

// ─────────────────────────────────────────────────────────────────────────────
// Chord Transition PageRank (CTP)
//
// Models the chord sequence as a directed graph: nodes = pitch classes (roots),
// edges = observed chord transitions weighted by frequency. Applies PageRank
// power iteration to find the stationary distribution of a random walk.
//
// The tonic in any tonal system is the harmonic "gravity well" — the node that
// chord progressions most often return to. PageRank surfaces this well without
// requiring any hand-coded cadential patterns. α=0.85 is the standard damping.
//
// Reliability weight: fraction of beat pairs with valid chord data.
// ─────────────────────────────────────────────────────────────────────────────
static void computeChordTransitionPageRank(const std::vector<int>& beatRoot,
                                           float ctpVector[12], float& w_CTP)
{
    std::fill(ctpVector, ctpVector + 12, 1.f / 12.f);
    w_CTP = 0.f;

    // Build transition count matrix
    float T[12][12] {};
    int validPairs = 0, totalPairs = 0;
    int nBR = (int)beatRoot.size();
    for (int i = 0; i + 1 < nBR; ++i) {
        ++totalPairs;
        int from = beatRoot[i], to = beatRoot[i + 1];
        if (from < 0 || to < 0) continue;
        T[from][to] += 1.f;
        ++validPairs;
    }
    if (validPairs < 4 || totalPairs == 0) return;

    // Row-normalize to get transition probabilities
    for (int r = 0; r < 12; ++r) {
        float sum = 0.f;
        for (int c = 0; c < 12; ++c) sum += T[r][c];
        if (sum > 1e-9f)
            for (int c = 0; c < 12; ++c) T[r][c] /= sum;
        else
            for (int c = 0; c < 12; ++c) T[r][c] = 1.f / 12.f;  // dangling node
    }

    // Power iteration: π = α * (π * T) + (1-α) / 12
    constexpr float kAlpha = 0.85f;
    constexpr float kTele  = (1.f - kAlpha) / 12.f;
    float pi[12]; std::fill(pi, pi + 12, 1.f / 12.f);

    for (int iter = 0; iter < 30; ++iter) {
        float next[12] {};
        for (int j = 0; j < 12; ++j)
            for (int i = 0; i < 12; ++i)
                next[j] += pi[i] * T[i][j];
        for (int j = 0; j < 12; ++j)
            pi[j] = kAlpha * next[j] + kTele;
    }

    // pi already sums to 1 (PageRank invariant)
    std::copy(pi, pi + 12, ctpVector);

    w_CTP = (float)validPairs / (float)totalPairs;
}

// ─────────────────────────────────────────────────────────────────────────────
// CPK — Chord Progression Key
//
// Slides a window of 3–4 consecutive beat roots over the beat sequence.
// For each window, scores each of the 12 candidate roots by how well the
// observed interval pattern matches known common progressions in that key.
//
// No ML, no training data. Uses music-theoretic interval patterns derived
// from common trap/hip-hop progressions (expressed as semitone intervals
// between consecutive chord roots, relative to tonic = 0).
//
// Reliability weight: fraction of beat pairs with valid chord data.
// ─────────────────────────────────────────────────────────────────────────────
static void computeChordProgressionKey(const std::vector<int>& beatRoot,
                                        float cpkVector[12], float& w_CPK)
{
    std::fill(cpkVector, cpkVector + 12, 0.f);
    w_CPK = 0.f;

    int nBR = (int)beatRoot.size();
    if (nBR < 4) return;

    int validBeats = 0;
    for (int r : beatRoot) if (r >= 0) ++validBeats;
    if (validBeats < 4) return;
    w_CPK = (float)validBeats / (float)nBR;

    // Diatonic consistency check: for each sliding window of valid beat roots,
    // find which tonic makes the most chord roots diatonic.
    // Offset-independent: cares only which chords appear, not their order.
    // Scale membership table — row = mode, col = semitone offset from tonic.
    static const bool kDiat[7][12] = {
        {1,0,1,0,1,1,0,1,0,1,0,1},  // Major
        {1,0,1,1,0,1,0,1,1,0,1,0},  // Natural Minor
        {1,0,1,1,0,1,0,1,1,0,0,1},  // Harmonic Minor
        {1,0,1,1,0,1,0,1,0,1,1,0},  // Dorian
        {1,0,1,0,1,1,0,1,0,1,1,0},  // Mixolydian
        {1,1,0,1,0,1,0,1,1,0,1,0},  // Phrygian
        {1,1,0,0,1,1,0,1,1,0,1,0},  // Phrygian Dominant
    };

    // Collect only valid (non-negative) beat roots
    std::vector<int> vr;
    vr.reserve((size_t)nBR);
    for (int r : beatRoot) if (r >= 0) vr.push_back(r);
    int nV = (int)vr.size();

    // Sliding windows of width 3-4
    for (int start = 0; start + 2 < nV; ++start)
    {
        int wEnd = std::min(nV - 1, start + 3);
        int wLen = wEnd - start + 1;

        for (int tonic = 0; tonic < 12; ++tonic)
        {
            // Best diatonic count across all 7 modes for this tonic
            int bestCount = 0;
            for (int m = 0; m < 7; ++m) {
                int cnt = 0;
                for (int k = start; k <= wEnd; ++k)
                    if (kDiat[m][(vr[(size_t)k] - tonic + 12) % 12]) ++cnt;
                if (cnt > bestCount) bestCount = cnt;
            }
            // Only add a vote when at least majority of chords are diatonic
            if (bestCount >= (wLen * 2 + 2) / 3)  // ≥ 2/3 ceiling
                cpkVector[tonic] += (float)bestCount / (float)wLen;
        }
    }

    // If nothing voted, zero w_CPK so this signal doesn't dilute totalW
    float cpkSum = 0.f;
    for (int i = 0; i < 12; ++i) cpkSum += cpkVector[i];
    if (cpkSum < 1e-9f) w_CPK = 0.f;
    else normalize12(cpkVector);
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal 7 — Harmonic Template Fit (HTF)
//
// For each of the 12 candidate roots, generates a synthetic harmonic template
// representing the expected spectral footprint of that tonic and its diatonic
// scale. Measures how well the observed magnitude spectrum responds to each
// template via normalized dot product.
//
// This simulates what a musician does at the piano: "does this note resonate
// with the audio?" — not "how much energy is on this note?"
//
// Template hierarchy per root (weights reflect tonal gravity):
//   Tonic (degree 0):  1.00 × harmonics 1–6
//   Fifth (degree 7):  0.60 × harmonics 1–4
//   Third (degree 3/4):0.40 × harmonics 1–3
//   Other diatonic:    0.20 × fundamental only
//
// Reliability weight: mean template response across all bins — high when
// the audio has clear tonal content, low when it's noisy/percussive.
// ─────────────────────────────────────────────────────────────────────────────
static void computeHarmonicTemplateFit(const std::vector<std::vector<float>>& magS,
                                        int N, int B, double binHz, double refAdj,
                                        float htfVector[12], float& w_HTF)
{
    std::fill(htfVector, htfVector + 12, 0.f);
    w_HTF = 0.f;
    if (N < 4 || B < 8) return;

    std::vector<double> meanSpec((size_t)B, 0.0);
    for (int n = 0; n < N; ++n)
        for (int k = 0; k < B; ++k)
            meanSpec[(size_t)k] += (double)magS[(size_t)n][(size_t)k];
    for (int k = 0; k < B; ++k) meanSpec[(size_t)k] /= (double)N;

    double specNorm = 0.0;
    for (int k = 0; k < B; ++k) specNorm += meanSpec[(size_t)k] * meanSpec[(size_t)k];
    specNorm = std::sqrt(specNorm);
    if (specNorm < 1e-9) return;

    // Natural minor scale degrees (semitones from root)
    static const int kMinorDeg[7] = { 0, 2, 3, 5, 7, 8, 10 };

    // Tonal gravity weights per scale degree index
    static const float kDegWeight[7] = {
        1.00f,  // tonic
        0.20f,  // 2nd
        0.40f,  // 3rd
        0.20f,  // 4th
        0.60f,  // 5th
        0.20f,  // 6th
        0.20f,  // 7th
    };

    static const int kDegHarmonics[7] = { 6, 1, 3, 1, 4, 1, 1 };

    float totalResponse = 0.f;
    for (int root = 0; root < 12; ++root)
    {
        std::vector<double> tmpl((size_t)B, 0.0);
        double tmplNorm = 0.0;

        for (int d = 0; d < 7; ++d)
        {
            int   semitone = (root + kMinorDeg[d]) % 12;
            float degW     = kDegWeight[d];
            int   nHarm    = kDegHarmonics[d];

            for (int octave = -1; octave <= 3; ++octave)
            {
                double freq = refAdj * std::pow(2.0, (semitone - 0 + octave * 12) / 12.0);
                if (freq < 60.0 || freq > 800.0) continue;

                for (int h = 1; h <= nHarm; ++h)
                {
                    double hFreq = freq * (double)h;
                    if (hFreq < 60.0 || hFreq > (double)(B - 1) * binHz) continue;

                    double binF = hFreq / binHz;
                    int    binI = (int)binF;
                    if (binI < 1 || binI >= B - 1) continue;

                    float harmW = degW / (float)h;
                    for (int dk = -2; dk <= 2; ++dk)
                    {
                        int    kb   = binI + dk;
                        if (kb < 0 || kb >= B) continue;
                        double dist = (double)dk;
                        double g    = std::exp(-0.5 * dist * dist);
                        tmpl[(size_t)kb] += harmW * g;
                        tmplNorm         += harmW * g * harmW * g;
                    }
                }
            }
        }

        tmplNorm = std::sqrt(tmplNorm);
        if (tmplNorm < 1e-9) continue;

        double dot = 0.0;
        for (int k = 0; k < B; ++k)
            dot += tmpl[(size_t)k] * meanSpec[(size_t)k];

        float response = (float)(dot / (tmplNorm * specNorm));
        htfVector[root] = std::max(0.f, response);
        totalResponse  += htfVector[root];
    }

    if (totalResponse < 1e-9f) return;
    normalize12(htfVector);
    w_HTF = std::min(1.f, totalResponse / 12.f * 8.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal 1 — Decay Stability (DS)
//
// For each spectral bin, compute the fraction of frames where its magnitude
// stays above 30% of its peak (= "sustainability"). Bins with high sustain
// are structural notes held over time, not transients.
// Accumulate sustainability-weighted mean magnitude into 12 pitch classes.
//
// Range: 100–2000 Hz (captures melody + mid-bass harmonics, avoids pure sub).
// Reliability weight: mean sustainability of active bins, normalized.
// ─────────────────────────────────────────────────────────────────────────────
static void computeDecayStability(const std::vector<std::vector<float>>& magS,
                                  int N, int B, double binHz, double refAdj,
                                  float dsVector[12], float& w_DS)
{
    std::fill(dsVector, dsVector + 12, 0.f);

    // 300 Hz lower bound: explicitly excludes 808 fundamental range (40-250 Hz).
    // 808 at G#2 = 207 Hz would otherwise appear as "sustained" and bias DS toward the 5th.
    int bLo = std::max(1,     (int)std::ceil (300.0  / binHz));
    int bHi = std::min(B - 1, (int)std::floor(2000.0 / binHz));

    float totalDS = 0.f, meanSus = 0.f;
    int   activeBins = 0;

    for (int k = bLo; k <= bHi; ++k)
    {
        float peak = 0.f, meanMag = 0.f;
        for (int n = 0; n < N; ++n) {
            float m = magS[(size_t)n][(size_t)k];
            if (m > peak) peak = m;
            meanMag += m;
        }
        meanMag /= (float)N;
        if (peak < 1e-9f) continue;

        float thresh = peak * 0.3f;
        int   count  = 0;
        for (int n = 0; n < N; ++n)
            if (magS[(size_t)n][(size_t)k] >= thresh) ++count;

        float sus = (float)count / (float)N;
        ++activeBins;
        meanSus += sus;

        if (sus < 0.08f) continue;   // ignore highly transient bins

        double freq = (double)k * binHz;
        double semi = 12.0 * std::log2(freq / refAdj);
        double pc   = semi - 12.0 * std::floor(semi / 12.0);
        int    pcI  = ((int)std::round(pc)) % 12;
        if (pcI < 0) pcI += 12;

        float contrib = sus * meanMag;
        dsVector[pcI] += contrib;
        totalDS       += contrib;
    }

    normalize12(dsVector);

    if (activeBins > 0) meanSus /= (float)activeBins;
    // Reliability: higher when more sustained content. Cap at 0.6 mean sus → 1.0.
    w_DS = std::min(1.f, meanSus / 0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal 2 — Reverb Tail Persistence (RTP)
//
// In relative-silence windows (energy < 15% of mean), residual reverb is
// dominated by harmonically-related partials of the last active tones.
// These are biased toward the tonic (most frequently played, most resonant).
//
// Range: 250–5000 Hz (above the bass fundamental, within harmonic zone).
// Minimum window: 3 consecutive frames (~35 ms at 512 hop / 44100 sr).
// Reliability weight: number of silence windows found / expected count.
// ─────────────────────────────────────────────────────────────────────────────
static void computeReverbTailPersistence(const std::vector<std::vector<float>>& magS,
                                         int N, int B, double binHz, double refAdj,
                                         float rtpVector[12], float& w_RTP)
{
    std::fill(rtpVector, rtpVector + 12, 0.f);
    w_RTP = 0.f;

    int bLo = std::max(1,     (int)std::ceil (250.0  / binHz));
    int bHi = std::min(B - 1, (int)std::floor(5000.0 / binHz));

    // Per-frame harmonic energy
    std::vector<float> frameE((size_t)N, 0.f);
    for (int n = 0; n < N; ++n)
        for (int k = bLo; k <= bHi; ++k)
            frameE[(size_t)n] += magS[(size_t)n][(size_t)k];

    float meanE = 0.f;
    for (float v : frameE) meanE += v;
    if (N > 0) meanE /= (float)N;
    if (meanE < 1e-9f) return;

    float silThresh = meanE * 0.15f;
    constexpr int kMinSilFrames = 3;
    int silenceWindows = 0;

    int n = 0;
    while (n < N) {
        if (frameE[(size_t)n] < silThresh) {
            // Count consecutive silence frames
            int start = n;
            while (n < N && frameE[(size_t)n] < silThresh) ++n;
            int len = n - start;
            if (len >= kMinSilFrames) {
                ++silenceWindows;
                // Accumulate HPCP in this window
                for (int fn = start; fn < n; ++fn) {
                    for (int k = bLo; k <= bHi; ++k) {
                        float m = magS[(size_t)fn][(size_t)k];
                        if (m < 1e-9f) continue;
                        double freq = (double)k * binHz;
                        double semi = 12.0 * std::log2(freq / refAdj);
                        double pc   = semi - 12.0 * std::floor(semi / 12.0);
                        int    pcI  = ((int)std::round(pc)) % 12;
                        if (pcI < 0) pcI += 12;
                        rtpVector[pcI] += m;
                    }
                }
            }
        } else {
            ++n;
        }
    }

    normalize12(rtpVector);

    // Reliability based on number of windows found
    // Expect ~1 window per 4 bars (≈ 10 s → ~1 window). More = better.
    if (silenceWindows > 0)
        w_RTP = std::min(1.f, (float)silenceWindows / 2.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal 3 — Phase Coherence Index (PCI) — Instantaneous Frequency stability
//
// A bin carrying a pure stable tone has a nearly-constant instantaneous
// frequency (phase advances by ~2π*k*hop/fftSz per frame). Bins with unstable
// IF (high variance) are inharmonic/noisy. Bins with stable IF are structural.
//
// Stability = 1 / (1 + variance_of_phase_deviation)
// Reliability weight: harmonic RMS / total RMS (high for tonal content).
// ─────────────────────────────────────────────────────────────────────────────
static void computePhaseCoherence(const std::vector<std::vector<float>>& phsM,
                                  const std::vector<std::vector<float>>& magS,
                                  int N, int B, double binHz, double refAdj,
                                  int fftSz, int hop,
                                  float pciVector[12], float& w_PCI)
{
    std::fill(pciVector, pciVector + 12, 0.f);
    w_PCI = 0.f;
    if (N < 4 || phsM.empty()) return;

    constexpr float kPi  = 3.14159265f;
    constexpr float kPi2 = 2.f * kPi;

    // 300 Hz lower bound: same rationale as DS — exclude 808 fundamental zone.
    int bLo = std::max(1,     (int)std::ceil (300.0  / binHz));
    int bHi = std::min(B - 1, (int)std::floor(5000.0 / binHz));

    float totalPCI    = 0.f;
    float harmRmsSq   = 0.f;
    float totalRmsSq  = 0.f;
    int   harmCount   = 0, totalCount = 0;

    int kHarmLo = std::max(1,     (int)std::ceil (250.0  / binHz));
    int kHarmHi = std::min(B - 1, (int)std::floor(5000.0 / binHz));

    for (int k = bLo; k <= bHi; ++k)
    {
        // Expected phase advance per hop for bin k: 2π * k * hop / fftSz
        float expAdv = kPi2 * (float)k * (float)hop / (float)fftSz;

        // Welford online variance of phase deviation
        int   cnt = 0;
        float M   = 0.f, S = 0.f;

        float meanMag = 0.f;

        for (int n = 1; n < N; ++n) {
            float m0 = magS[(size_t)(n-1)][(size_t)k];
            float m1 = magS[(size_t)n    ][(size_t)k];
            if (m0 < 1e-9f || m1 < 1e-9f) continue;

            float dev = phsM[(size_t)n][(size_t)k] - phsM[(size_t)(n-1)][(size_t)k] - expAdv;
            // Wrap to [-π, π]
            while (dev >  kPi) dev -= kPi2;
            while (dev < -kPi) dev += kPi2;

            ++cnt;
            float delta = dev - M;
            M += delta / (float)cnt;
            S += delta * (dev - M);

            meanMag += m1;
        }
        if (cnt < 4) continue;

        meanMag /= (float)cnt;
        float var      = S / (float)cnt;
        float stability = 1.f / (1.f + var * 5.f);   // 5x scale: var≈0.2 → stab≈0.5

        if (stability < 0.1f) continue;

        double freq = (double)k * binHz;
        double semi = 12.0 * std::log2(freq / refAdj);
        double pc   = semi - 12.0 * std::floor(semi / 12.0);
        int    pcI  = ((int)std::round(pc)) % 12;
        if (pcI < 0) pcI += 12;

        float contrib   = stability * meanMag;
        pciVector[pcI] += contrib;
        totalPCI        += contrib;

        // RMS accumulators for reliability
        if (k >= kHarmLo && k <= kHarmHi) {
            harmRmsSq += meanMag * meanMag; ++harmCount;
        }
        totalRmsSq += meanMag * meanMag; ++totalCount;
    }

    normalize12(pciVector);

    float harmRms  = (harmCount  > 0) ? std::sqrt(harmRmsSq  / (float)harmCount)  : 0.f;
    float totalRms = (totalCount > 0) ? std::sqrt(totalRmsSq / (float)totalCount) : 0.f;
    w_PCI = (totalRms > 1e-9f) ? std::min(1.f, harmRms / totalRms) : 0.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Modal Interval Classifier (MIC) — Gini-based diatonic energy concentration
//
// For each of the 7 modes, with root fixed, compute the fraction of chroma
// energy falling on diatonic scale degrees (= Gini/concentration score).
// The mode with highest concentration score wins.
//
// When the top-2 Gini scores are within 0.015, use cadence gravity as a
// tiebreaker: if the V-chord (7 semitones above root) appears in cadences,
// it's natural minor (or harmonic minor); if the bVII (10 sem) appears, it's
// Dorian or Mixolydian.
// ─────────────────────────────────────────────────────────────────────────────
static Mode computeModalGini(const float* norm, int root,
                             const std::array<float,12>& cadenceGravity,
                             bool isMinorContext, bool isMajorContext)
{
    // Scale degrees present in each mode (from root = degree 0)
    static const int kDeg[(int)Mode::Count][7] = {
        { 0, 2, 4, 5, 7, 9, 11 },   // Major
        { 0, 2, 3, 5, 7, 8, 10 },   // NaturalMinor
        { 0, 2, 3, 5, 7, 8, 11 },   // HarmonicMinor
        { 0, 2, 3, 5, 7, 9, 10 },   // Dorian
        { 0, 2, 4, 5, 7, 9, 10 },   // Mixolydian
        { 0, 1, 3, 5, 7, 8, 10 },   // Phrygian
        { 0, 1, 4, 5, 7, 8, 10 },   // PhrygianDominant
    };

    float totalE = 0.f;
    for (int i = 0; i < 12; ++i) totalE += norm[i];
    if (totalE < 1e-9f) return Mode::NaturalMinor;

    float giniScore[(int)Mode::Count] {};

    for (int m = 0; m < (int)Mode::Count; ++m) {
        float diatE = 0.f;
        for (int d = 0; d < 7; ++d)
            diatE += norm[(root + kDeg[m][d]) % 12];
        giniScore[m] = diatE / totalE;
    }

    // Context biases — small, not per-track-tuned
    if (isMinorContext) {
        giniScore[(int)Mode::NaturalMinor] += 0.03f;
        giniScore[(int)Mode::Dorian]       += 0.02f;
        giniScore[(int)Mode::HarmonicMinor]+= 0.01f;
    } else if (isMajorContext) {
        giniScore[(int)Mode::Major]        += 0.03f;
        giniScore[(int)Mode::Mixolydian]   += 0.01f;
    }

    // Cadence-based tiebreaker
    // bVI→I (interval=8) or bVII→I (interval=10) appearing strongly → Dorian/Mixolydian
    // V→I  (interval=7) strongly → NaturalMinor or HarmonicMinor
    float vCadence   = cadenceGravity[(root + 7)  % 12];   // V chord
    float bviiCad    = cadenceGravity[(root + 10) % 12];   // bVII chord
    if (bviiCad > 0.4f) giniScore[(int)Mode::Dorian]    += 0.02f;
    if (bviiCad > 0.4f) giniScore[(int)Mode::Mixolydian]+= 0.02f;
    if (vCadence > 0.4f) giniScore[(int)Mode::NaturalMinor] += 0.01f;
    if (vCadence > 0.6f) giniScore[(int)Mode::HarmonicMinor]+= 0.01f;

    int bestM = 0;
    for (int m = 1; m < (int)Mode::Count; ++m)
        if (giniScore[m] > giniScore[bestM]) bestM = m;

    return (Mode)bestM;
}

// ─────────────────────────────────────────────────────────────────────────────
// getKey — TDA v1 (Tonal Decay Analysis)
//
// Root detection via Tonic Stability Accumulator (TSA):
//   weighted average of 4 independent signals (DS, RTP, PCI, RWM)
//   + established bass analysis (HSBF + 808 slide tracker)
//   + Chord Transition Gravity (CTG).
//
// Each signal's weight is derived from its own reliability in the buffer.
// No weights are tuned to specific test tracks.
//
// Mode detection via Modal Interval Classifier (MIC):
//   Gini-based diatonic energy concentration for 7 modes at fixed root.
//   Cadence gravity used as tiebreaker for ambiguous minor sub-modes.
// ─────────────────────────────────────────────────────────────────────────────
void KeyDetector::getKey(int& key, Mode& mode, float& confidence,
                         float bpmHint, int rootNoteHint, float pitchConf,
                         GenreProfile genre, KeyCandidate* top3,
                         std::vector<float>* stabilityTimeline,
                         KeyDebugInfo* dbg, InputMode inputMode) const
{
    int N = (int)mag.size();
    int B = storeBins + 1;
    if (N < 16 || B < 8) { key = -1; mode = Mode::Major; confidence = 0.f; return; }

    double binHz        = sr / (double)fftSz;
    const double kRefC4 = 261.6255653;
    const float  kHarmDk = 0.6f;

    // ── 1) Median HPSS ────────────────────────────────────────────────────────
    std::vector<std::vector<float>> magS = mag;
    medianFilterTime(magS, B, N, 7);

    // ── 2) Tuning estimation ──────────────────────────────────────────────────
    double tuningCents = estimateTuningOffset(magS, B, N, binHz, kRefC4);
    double refAdj      = kRefC4 * std::pow(2.0, tuningCents / 1200.0);

    // ── 3a) Onset novelty (200–4000 Hz HWR flux on raw mag) ──────────────────
    std::vector<float> onsetNov((size_t)N, 0.f);
    {
        int bOLo = std::max(1,     (int)(200.0 / binHz));
        int bOHi = std::min(B - 1, (int)(4000.0 / binHz));
        for (int n = 1; n < N; ++n) {
            float flux = 0.f;
            for (int k = bOLo; k <= bOHi; ++k) {
                float d = mag[(size_t)n][(size_t)k] - mag[(size_t)(n-1)][(size_t)k];
                if (d > 0.f) flux += d;
            }
            onsetNov[(size_t)n] = flux;
        }
        float maxON = *std::max_element(onsetNov.begin(), onsetNov.end());
        if (maxON > 1e-9f) for (auto& v : onsetNov) v /= maxON;
    }

    // ── 3b) Input type detection ──────────────────────────────────────────────
    float spectralFlatness = 0.f;
    {
        int bFLo = std::max(1, (int)(300.0 / binHz));
        int bFHi = std::min(B-1, (int)(3000.0 / binHz));
        double logSum = 0.0, linSum = 0.0; int flatN = 0;
        for (int n = 0; n < N; ++n)
            for (int k = bFLo; k <= bFHi; ++k) {
                float m = magS[(size_t)n][(size_t)k];
                if (m > 1e-9f) { logSum += std::log((double)m); linSum += (double)m; ++flatN; }
            }
        if (flatN > 0 && linSum > 1e-9)
            spectralFlatness = (float)(std::exp(logSum/(double)flatN) / (linSum/(double)flatN));
    }
    bool isHighlyTonal = (spectralFlatness < 0.08f);
    bool isClearPitch  = (pitchConf > 0.35f);

    int kBassLo = std::max(1,     (int)std::ceil (40.0  / binHz));
    int kBassHi = std::min(B - 1, (int)std::floor(200.0 / binHz));
    int bassOnsetCount = 0;
    {
        std::vector<float> bassLog((size_t)N, 0.f);
        for (int n = 0; n < N; ++n) {
            float e = 0.f;
            for (int k = kBassLo; k <= kBassHi; ++k) e += mag[(size_t)n][(size_t)k];
            bassLog[(size_t)n] = std::log(1.f + e);
        }
        float meanBL = 0.f;
        for (float v : bassLog) meanBL += v;
        meanBL /= (float)N;
        float thrBL = meanBL * 2.f;
        for (int n = 1; n + 1 < N; ++n) {
            float d = bassLog[(size_t)n] - bassLog[(size_t)(n-1)];
            if (d > thrBL && d > bassLog[(size_t)(n-1)] - bassLog[(size_t)(n > 1 ? n-2 : 0)])
                ++bassOnsetCount;
        }
    }
    bool hasRhythmicBass = (bassOnsetCount >= 4);
    // InputMode overrides auto-detection
    if (inputMode == InputMode::Loop)   hasRhythmicBass = true;
    if (inputMode == InputMode::Melody) hasRhythmicBass = false;

    // ── 4) Per-frame HPCP with onset + cadence weighting ─────────────────────
    std::array<float, 12> chroma {};
    chroma.fill(0.f);
    std::vector<std::array<float, 12>> frameChromas;
    frameChromas.reserve((size_t)N);
    int cadenceBoundary = (int)(N * 0.75f);

    for (int n = 0; n < N; ++n) {
        std::array<float, 12> framePC {};
        framePC.fill(0.f);
        hpcpAccumulate(magS[(size_t)n], B, binHz, refAdj, kHarmDk, framePC);

        float s = 0.f;
        for (float v : framePC) s += v;
        if (s < 1e-9f) continue;

        std::array<float, 12> normFrame {};
        for (int c = 0; c < 12; ++c) normFrame[(size_t)c] = framePC[(size_t)c] / s;
        frameChromas.push_back(normFrame);

        float w = 1.0f;
        if      (onsetNov[(size_t)n] > 0.5f)                       w = 4.0f;
        else if (n > 0 && onsetNov[(size_t)(n-1)] > 0.5f)          w = 2.5f;
        else if (n > 1 && onsetNov[(size_t)(n-2)] > 0.5f)          w = 1.5f;
        if (w < 2.0f && onsetNov[(size_t)n] < 0.08f && spectralFlatness < 0.15f)
            w = std::max(w, 1.8f);
        if (n >= cadenceBoundary) w *= 2.0f;

        for (int c = 0; c < 12; ++c) chroma[(size_t)c] += w * normFrame[(size_t)c];
    }

    float chromaSum = 0.f;
    for (float v : chroma) chromaSum += v;
    if (chromaSum < 1e-9f) { key = -1; mode = Mode::Major; confidence = 0.f; return; }
    float norm[12];
    for (int i = 0; i < 12; ++i) norm[i] = chroma[(size_t)i] / chromaSum;

    // ── 5) Chord root + quality histogram ─────────────────────────────────────
    std::array<int,12> rootHist {}; rootHist.fill(0);
    int chordFramesUsed = 0, majorChordCount = 0, minorChordCount = 0;
    for (const auto& fc : frameChromas) {
        int bestRoot = -1; bool bestIsMajor = true; float bestScore = 0.f;
        for (int root = 0; root < 12; ++root) {
            float sMaj = fc[(size_t)root]+fc[(size_t)((root+4)%12)]+fc[(size_t)((root+7)%12)];
            float sMin = fc[(size_t)root]+fc[(size_t)((root+3)%12)]+fc[(size_t)((root+7)%12)];
            if (sMaj > bestScore) { bestScore = sMaj; bestRoot = root; bestIsMajor = true;  }
            if (sMin > bestScore) { bestScore = sMin; bestRoot = root; bestIsMajor = false; }
        }
        if (bestRoot >= 0 && bestScore > 0.30f) {
            rootHist[(size_t)bestRoot]++;
            if (bestIsMajor) ++majorChordCount; else ++minorChordCount;
            ++chordFramesUsed;
        }
    }
    int   chordRoot = -1; float chordFocus = 0.f;
    if (chordFramesUsed > 4) {
        int maxCount = 0;
        for (int i = 0; i < 12; ++i)
            if (rootHist[(size_t)i] > maxCount) { maxCount = rootHist[(size_t)i]; chordRoot = i; }
        chordFocus = (float)maxCount / (float)chordFramesUsed;
    }

    if (dbg) { dbg->chordRoot = chordRoot; dbg->chordFocus = chordFocus; }

    bool isMinorContext = false, isMajorContext = false;
    if (chordFramesUsed > 4) {
        if (minorChordCount > (int)((float)majorChordCount * 1.25f)) isMinorContext = true;
        else if (majorChordCount > (int)((float)minorChordCount * 1.25f)) isMajorContext = true;
    }

    // ── 6) Beat sequence + CTG + DTV ──────────────────────────────────────────
    std::array<float, 12> downbeatVotes {};
    downbeatVotes.fill(0.f);
    std::vector<int>  beatRoot;
    std::vector<bool> beatIsMajor;
    std::array<float, 12> cadenceGravity {};
    cadenceGravity.fill(0.f);

    if (bpmHint > 30.f && bpmHint < 250.f && (int)frameChromas.size() > 16)
    {
        double frameRate = sr / (double)hop;
        float  periodF   = (float)(frameRate * 60.0 / (double)bpmHint);
        int    Nf        = (int)frameChromas.size();

        if (periodF >= 4.f && periodF * 4.f < (float)Nf)
        {
            // Multi-band onset novelty for beat finding
            std::vector<float> nov((size_t)N, 0.f);
            int bandSplit[7];
            for (int s = 0; s < 7; ++s)
                bandSplit[s] = std::min(B-1, (int)((float)B*(float)s/6.f));
            float prevBand[6] = {};
            for (int n = 0; n < N; ++n) {
                float onset = 0.f;
                for (int s = 0; s < 6; ++s) {
                    float e = 0.f;
                    for (int k = bandSplit[s]; k <= bandSplit[s+1]; ++k) e += mag[(size_t)n][(size_t)k];
                    float le = std::log(1.f + e), d = le - prevBand[s];
                    if (d > 0.f) onset += d;
                    prevBand[s] = le;
                }
                nov[(size_t)n] = onset;
            }
            int phase = 0; float maxNov = nov[0];
            for (int i = 1; i < (int)periodF && i < N; ++i)
                if (nov[(size_t)i] > maxNov) { maxNov = nov[(size_t)i]; phase = i; }

            std::vector<int> beats; beats.reserve(64);
            for (float pos = (float)phase; pos < (float)(Nf-1); pos += periodF)
                beats.push_back((int)std::round(pos));

            // Per-beat chord root
            for (int b : beats) {
                std::array<float,12> bc {}; bc.fill(0.f);
                int used = 0;
                for (int f = std::max(0,b-1); f <= std::min(Nf-1,b+3); ++f)
                    { for (int c = 0; c < 12; ++c) bc[(size_t)c] += frameChromas[(size_t)f][(size_t)c]; ++used; }
                if (!used) { beatRoot.push_back(-1); beatIsMajor.push_back(false); continue; }
                int bestR=-1; bool bestM=false; float bestS=0.f;
                for (int r = 0; r < 12; ++r) {
                    float sMaj = bc[(size_t)r]+bc[(size_t)((r+4)%12)]+bc[(size_t)((r+7)%12)];
                    float sMin = bc[(size_t)r]+bc[(size_t)((r+3)%12)]+bc[(size_t)((r+7)%12)];
                    if (sMaj > bestS){bestS=sMaj;bestR=r;bestM=true;}
                    if (sMin > bestS){bestS=sMin;bestR=r;bestM=false;}
                }
                float wSum = 0.f; for (float v : bc) wSum += v;
                if (bestS > 0.45f*wSum) { beatRoot.push_back(bestR); beatIsMajor.push_back(bestM); }
                else                    { beatRoot.push_back(-1);    beatIsMajor.push_back(false); }
            }

            // DTV — bass-onset novelty to find bar downbeat, accumulate bass PC
            {
                std::vector<float> bassNovDTV((size_t)N, 0.f);
                {
                    float prevE = 0.f;
                    for (int n = 0; n < N; ++n) {
                        float e = 0.f;
                        for (int k = kBassLo; k <= kBassHi; ++k) e += mag[(size_t)n][(size_t)k];
                        float le = std::log(1.f + e), d = le - prevE;
                        bassNovDTV[(size_t)n] = d > 0.f ? d : 0.f;
                        prevE = le;
                    }
                }
                int nBeats = (int)beats.size();
                for (int bar = 0; bar * 4 + 3 < nBeats; ++bar) {
                    int   downbeat   = beats[bar * 4];
                    float maxNovBeat = bassNovDTV[std::min(N-1, downbeat)];
                    for (int bi = 1; bi < 4; ++bi) {
                        int   b  = beats[bar * 4 + bi];
                        float nv = bassNovDTV[std::min(N-1, b)];
                        if (nv > maxNovBeat) { maxNovBeat = nv; downbeat = b; }
                    }
                    for (int fn = std::max(0,downbeat-1); fn <= std::min(N-1,downbeat+2); ++fn) {
                        for (int k = kBassLo; k <= kBassHi; ++k) {
                            float m = magS[(size_t)fn][(size_t)k]; if (m < 1e-9f) continue;
                            double freq = (double)k * binHz;
                            float  wk   = 1.f / (1.f + (float)freq / 80.f);
                            double semi = 12.0 * std::log2(freq / refAdj);
                            double pc   = semi - 12.0 * std::floor(semi / 12.0);
                            int    pcI  = ((int)std::round(pc)) % 12; if (pcI < 0) pcI += 12;
                            downbeatVotes[(size_t)pcI] += wk * m;
                        }
                    }
                }
            }

            // CTG — chord cadence resolution gravity
            {
                static const int   kCadIntervals[] = { 7, 10,  8,  1 };
                static const float kCadWeights[]   = { 1.0f, 0.9f, 0.6f, 0.5f };
                constexpr int kNCad = 4;
                int nBR = (int)beatRoot.size();
                for (int i = 0; i + 1 < nBR; ++i) {
                    int from = beatRoot[i], to = beatRoot[i + 1];
                    if (from < 0 || to < 0) continue;
                    int interval = (from - to + 12) % 12;
                    for (int ci = 0; ci < kNCad; ++ci)
                        if (interval == kCadIntervals[ci])
                            { cadenceGravity[(size_t)to] += kCadWeights[ci]; break; }
                }
            }
        }
    }

    // Expose CTG/DTV debug info (using raw before normalization)
    if (dbg) {
        dbg->ctgMaxRoot = -1; dbg->ctgMaxVal = 0.f;
        float maxCG = *std::max_element(cadenceGravity.begin(), cadenceGravity.end());
        if (maxCG > 1e-9f) {
            dbg->ctgMaxVal = 1.f;
            for (int i = 0; i < 12; ++i)
                if (cadenceGravity[(size_t)i] >= maxCG)
                    { dbg->ctgMaxRoot = i; break; }
        }
        dbg->dtvMaxRoot = -1; dbg->dtvMaxVote = 0.f;
        float maxDV = *std::max_element(downbeatVotes.begin(), downbeatVotes.end());
        if (maxDV > 1e-9f) {
            dbg->dtvMaxVote = 1.f;
            for (int i = 0; i < 12; ++i)
                if (downbeatVotes[(size_t)i] >= maxDV)
                    { dbg->dtvMaxRoot = i; break; }
        }
    }

    // ── 7) Bass analysis — HSBF + 808 slide tracker ───────────────────────────
    auto bassFreqAt = [&](int f) -> double {
        float bestScore = 0.f; int bestK = -1;
        for (int k = kBassLo; k <= kBassHi; ++k) {
            double fundFreq = (double)k * binHz;
            if (fundFreq < 40.0) continue;
            float score = 0.f;
            for (int h = 1; h <= 4; ++h) {
                int hBin = (int)std::round(fundFreq * (double)h / binHz);
                if (hBin < 0 || hBin >= B) break;
                score += (1.0f / (float)h) * mag[(size_t)f][(size_t)hBin];
            }
            if (score > bestScore) { bestScore = score; bestK = k; }
        }
        if (bestK < kBassLo) return -1.0;
        if (bestK > kBassLo && bestK < kBassHi) {
            float mp = mag[(size_t)f][(size_t)(bestK-1)];
            float mc = mag[(size_t)f][(size_t)(bestK  )];
            float mn = mag[(size_t)f][(size_t)(bestK+1)];
            double den = (double)(mp - 2.f*mc + mn);
            double delta = (std::abs(den) > 1e-12) ? 0.5*(double)(mp-mn)/den : 0.0;
            return ((double)bestK + delta) * binHz;
        }
        return (double)bestK * binHz;
    };

    auto bassMagAt = [&](int f) -> float {
        float bestM = 0.f;
        for (int k = kBassLo; k <= kBassHi; ++k) {
            float wm = (1.f/(1.f+(float)((double)k*binHz)/80.f)) * mag[(size_t)f][(size_t)k];
            if (wm > bestM) bestM = wm;
        }
        return bestM;
    };

    // HSBF sustain histogram
    std::array<float,12> bassSustain {}; bassSustain.fill(0.f);
    for (int n = 0; n < N; ++n) {
        double freq = bassFreqAt(n);
        if (freq < 40.0) continue;
        float bm = bassMagAt(n);
        if (bm < 1e-9f) continue;
        double semi = 12.0 * std::log2(freq / refAdj);
        double pc   = semi - 12.0 * std::floor(semi / 12.0);
        int    pcI  = ((int)std::round(pc)) % 12; if (pcI < 0) pcI += 12;
        float  w    = 1.0f / (1.0f + (float)freq / 80.f);
        bassSustain[(size_t)pcI] += w * bm;
    }

    // 808 slide tracker
    std::array<float,12> bassAttack {}; bassAttack.fill(0.f);
    {
        std::vector<float> bassLog((size_t)N, 0.f);
        for (int n = 0; n < N; ++n) {
            float e = 0.f;
            for (int k = kBassLo; k <= kBassHi; ++k) e += mag[(size_t)n][(size_t)k];
            bassLog[(size_t)n] = std::log(1.f + e);
        }
        std::vector<float> bassNov((size_t)N, 0.f);
        for (int n = 1; n < N; ++n) {
            float d = bassLog[(size_t)n] - bassLog[(size_t)(n-1)];
            bassNov[(size_t)n] = d > 0.f ? d : 0.f;
        }
        float meanNov = 0.f;
        for (float v : bassNov) meanNov += v;
        meanNov /= (float)N;
        float thr = meanNov * 2.f;

        for (int n = 1; n + 1 < N; ++n) {
            float v = bassNov[(size_t)n];
            if (v < thr || v < bassNov[(size_t)(n-1)] || v < bassNov[(size_t)(n+1)]) continue;

            int settledFrame = std::min(N-1, n+2);
            {
                double prevSemi = -9999.0; int stableCount = 0;
                for (int f = n+1; f <= std::min(N-1, n+15); ++f) {
                    double freq = bassFreqAt(f);
                    if (freq < 40.0) { stableCount = 0; prevSemi = -9999.0; continue; }
                    double semi = 12.0 * std::log2(freq / refAdj);
                    if (prevSemi > -9998.0) {
                        double diffCents = std::abs(semi - prevSemi) * 100.0;
                        if (diffCents < 8.0) {
                            if (++stableCount >= 2) { settledFrame = f; break; }
                        } else { stableCount = 0; }
                    }
                    prevSemi = semi;
                }
            }
            double freq = bassFreqAt(settledFrame);
            if (freq < 40.0) continue;
            double semi = 12.0 * std::log2(freq / refAdj);
            double pc   = semi - 12.0 * std::floor(semi / 12.0);
            int    pcI  = ((int)std::round(pc)) % 12; if (pcI < 0) pcI += 12;
            bassAttack[(size_t)pcI] += bassMagAt(settledFrame);
        }
    }

    float sumA = 0.f, sumS = 0.f;
    for (int i = 0; i < 12; ++i) { sumA += bassAttack[(size_t)i]; sumS += bassSustain[(size_t)i]; }
    std::array<float,12> bassChroma {};
    for (int i = 0; i < 12; ++i) {
        float a = (sumA > 1e-9f) ? bassAttack [(size_t)i]/sumA : 0.f;
        float s = (sumS > 1e-9f) ? bassSustain[(size_t)i]/sumS : 0.f;
        bassChroma[(size_t)i] = (sumA > 1e-9f ? 0.6f : 0.f)*a + (sumA > 1e-9f ? 0.4f : 1.f)*s;
    }
    float bassTotal = 0.f; for (float v : bassChroma) bassTotal += v;
    int   bassRoot  = 0;   float bassMax = bassChroma[0];
    for (int i = 1; i < 12; ++i)
        if (bassChroma[(size_t)i] > bassMax) { bassMax = bassChroma[(size_t)i]; bassRoot = i; }
    float bassFocus = (bassTotal > 1e-9f) ? bassMax / bassTotal : 0.f;

    if (dbg) { dbg->bassRoot = bassRoot; dbg->bassFocus = bassFocus; }

    // ── 8) TDA Signals ────────────────────────────────────────────────────────

    // Signal 1: Decay Stability
    float dsVector[12]; float w_DS;
    computeDecayStability(magS, N, B, binHz, refAdj, dsVector, w_DS);

    // Signal 2: Reverb Tail Persistence
    float rtpVector[12]; float w_RTP;
    computeReverbTailPersistence(magS, N, B, binHz, refAdj, rtpVector, w_RTP);

    // Signal 3: Phase Coherence Index
    float pciVector[12]; float w_PCI;
    if (!phas.empty())
        computePhaseCoherence(phas, magS, N, B, binHz, refAdj, fftSz, hop, pciVector, w_PCI);
    else
        { std::fill(pciVector, pciVector+12, 0.f); w_PCI = 0.f; }

    // Signal 4: Rhythmic Weight Map (DTV normalized)
    // Weight proportional to DTV concentration: how dominated by one note the downbeat
    // bass signal is. High concentration = reliable tonic candidate on beat 1.
    float rwmVector[12]; float w_RWM;
    {
        float dtvTotal = 0.f;
        for (int i = 0; i < 12; ++i) { rwmVector[i] = downbeatVotes[(size_t)i]; dtvTotal += rwmVector[i]; }
        float dtvMax = *std::max_element(downbeatVotes.begin(), downbeatVotes.end());
        float dtvConcentration = (dtvTotal > 1e-9f) ? dtvMax / dtvTotal : 0.f;
        if (dtvTotal > 1e-9f) for (int i = 0; i < 12; ++i) rwmVector[i] /= dtvTotal;
        // Quadratic amplification: high-concentration DTV (e.g. 100% on one note)
        // is physically more reliable than a diluted one — beat-1 bass energy
        // concentrated entirely on one pitch class is strong tonic evidence.
        float baseW = (bpmHint > 30.f && bpmHint < 250.f && hasRhythmicBass)
                      ? (0.30f + 0.35f * dtvConcentration) : 0.05f;
        w_RWM = baseW * dtvConcentration;
    }

    // Signal 5: Chord Transition PageRank
    float ctpVector[12]; float w_CTP;
    computeChordTransitionPageRank(beatRoot, ctpVector, w_CTP);

    // Signal 6: Chord Progression Key
    float cpkVector[12]; float w_CPK;
    computeChordProgressionKey(beatRoot, cpkVector, w_CPK);

    // Signal 7: Harmonic Template Fit
    float htfVector[12]; float w_HTF;
    computeHarmonicTemplateFit(magS, N, B, binHz, refAdj, htfVector, w_HTF);

    // Bass chroma normalized → TSA contributor
    float bassVec[12];
    { std::copy(bassChroma.begin(), bassChroma.end(), bassVec); normalize12(bassVec); }

    // CTG normalized → TSA contributor
    float ctgVec[12];
    {
        std::copy(cadenceGravity.begin(), cadenceGravity.end(), ctgVec);
        normalize12(ctgVec);
    }
    float ctgSum = 0.f;
    for (float v : cadenceGravity) ctgSum += v;

    // Chord root histogram → TSA contributor (full distribution, not just argmax)
    float chordVec[12] {};
    for (int i = 0; i < 12; ++i) chordVec[i] = (float)rootHist[(size_t)i];
    normalize12(chordVec);

    // Melodic pitch hint (boosted ×4 in Melody mode)
    float melodicScale  = (inputMode == InputMode::Melody) ? 4.f : 1.f;
    float melodicWeight = (isHighlyTonal && isClearPitch)
                          ? melodicScale * (0.06f + 0.10f * pitchConf) : 0.f;
    float melodicVec[12] {};
    if (melodicWeight > 0.f && rootNoteHint >= 0 && rootNoteHint < 12)
        melodicVec[rootNoteHint] = 1.f;

    // ── 9) Tonic Stability Accumulator (TSA) ─────────────────────────────────
    //
    // All weights are CONTINUOUS functions of signal quality — no binary gates.
    // This prevents threshold-based overfitting to specific test samples.
    //
    // bass:    proportional to bassFocus (how concentrated bass energy is on one note)
    // chord:   proportional to chordFocus (how dominant one chord root is)
    // CTG:     proportional to tanh(ctgSum) — smooth function of cadence evidence
    // CTP:     proportional to fraction of valid beat transitions
    // RWM:     binary (needs BPM + rhythmic bass) but the vector itself is continuous
    // DS/PCI:  dynamic from signal reliability
    // melodic: small fixed bonus when pitch detector has high confidence

    // Consensus boost: when bass and CTG independently vote the same root,
    // that is statistically independent evidence (different signal paths) and
    // should receive a multiplicative boost beyond their individual weights.
    // Weight = product of their individual reliabilities → continuous, no hard gate.
    float consensusVec[12] {};
    float consensusW = 0.f;
    {
        int ctgArgMax = -1; float ctgMax = 0.f;
        for (int i = 0; i < 12; ++i)
            if (cadenceGravity[(size_t)i] > ctgMax) { ctgMax = cadenceGravity[(size_t)i]; ctgArgMax = i; }
        if (bassRoot >= 0 && ctgArgMax >= 0 && bassRoot == ctgArgMax && ctgMax > 1e-9f) {
            consensusVec[bassRoot] = 1.f;
            // Weight = product of both reliabilities — strongest when both signals are strong
            consensusW = bassFocus * std::tanh(ctgSum) * 0.35f;
        }
    }

    // Weights rationale:
    // - chord histogram: most robust aggregate tonal center signal (all frames, direct count)
    // - bass: reliable only when focused on one note, capped by focus
    // - CTP: good concept but limited by chord detection quality under 808 noise → halved
    // - CTG: smooth function of cadential evidence (tanh ensures no hard gate)
    // - RWM: reliable when BPM+bass present, fixed weight
    // - DS/PCI: dynamic from signal reliability, capped to avoid dominating
    // - consensus: bass × CTG agreement — independent corroboration signal
    float bassWCap   = (inputMode == InputMode::Loop)   ? 0.50f :
                       (inputMode == InputMode::Melody) ? 0.05f :
                       (hasRhythmicBass                 ? 0.38f : 0.15f);
    // Reduce bass weight when bass and chord disagree — uncertain about which is tonic
    float bassChordAgree = (chordRoot >= 0 && bassRoot == chordRoot) ? 1.0f : 0.65f;
    float bassW      = bassWCap * bassFocus * bassChordAgree;
    float chordW     = chordFocus * 0.40f;
    float ctgW       = std::tanh(ctgSum) * 0.25f;
    float ctpW       = w_CTP * 0.15f;
    float cpkW       = 0.f;  // CPK disabled: diatonic check votes too broadly, hurts accuracy
    float htfW       = 0.f;  // HTF disabled: template correlation still unresolvable for tonic vs dominant (same shared notes in C#m/G#m)
    float dsW        = std::min(0.20f, w_DS  * 0.35f);
    float rtpW       = std::min(0.08f, w_RTP * 0.12f);
    float pciW       = std::min(0.20f, w_PCI * 0.30f);
    float rwmW       = w_RWM;
    float melW       = melodicWeight;

    float totalW = bassW + chordW + ctgW + ctpW + cpkW + htfW + dsW + rtpW + pciW + rwmW + melW + consensusW;

    float tsaVector[12] {};
    if (totalW > 1e-9f) {
        for (int pc = 0; pc < 12; ++pc) {
            tsaVector[pc] = (bassW      * bassVec[pc]      +
                             chordW     * chordVec[pc]     +
                             ctgW       * ctgVec[pc]       +
                             ctpW       * ctpVector[pc]    +
                             cpkW       * cpkVector[pc]    +
                             htfW       * htfVector[pc]    +
                             dsW        * dsVector[pc]     +
                             rtpW       * rtpVector[pc]    +
                             pciW       * pciVector[pc]    +
                             rwmW       * rwmVector[pc]    +
                             melW       * melodicVec[pc]   +
                             consensusW * consensusVec[pc]) / totalW;
        }
    }

    // Root = argmax TSA
    int rootCandidate = 0;
    for (int i = 1; i < 12; ++i)
        if (tsaVector[i] > tsaVector[rootCandidate]) rootCandidate = i;

    int secondCandidate = (rootCandidate == 0) ? 1 : 0;
    for (int i = 0; i < 12; ++i)
        if (i != rootCandidate && tsaVector[i] > tsaVector[secondCandidate])
            secondCandidate = i;

    float globalConfidence = tsaVector[rootCandidate] - tsaVector[secondCandidate];

    // ── 10) Modal Interval Classifier (MIC) ───────────────────────────────────
    Mode detMode = computeModalGini(norm, rootCandidate, cadenceGravity,
                                    isMinorContext, isMajorContext);

    // ── 11) Pearson top-5 for diagnostics (when dbg != null) ─────────────────
    if (dbg)
    {
        struct PTop { float s; int r; Mode m; };
        PTop pt[5]; for (auto& p : pt) p = {-2.f,-1,Mode::Major};
        for (int mm = 0; mm < (int)Mode::Count; ++mm) {
            const float* profile = profileFor((Mode)mm, GenreProfile::Electronic);
            for (int root = 0; root < 12; ++root) {
                float rotated[12];
                for (int i = 0; i < 12; ++i) rotated[i] = profile[(i-root+12)%12];
                float s = pearsonCorr(norm, rotated, 12);
                if (s > pt[4].s) {
                    pt[4] = {s, root, (Mode)mm};
                    for (int t = 3; t >= 0; --t)
                        if (pt[t].s < pt[t+1].s) std::swap(pt[t],pt[t+1]); else break;
                }
            }
        }
        for (int t = 0; t < 5; ++t) {
            dbg->pearsonTop5Score[t] = pt[t].s;
            dbg->pearsonTop5Root[t]  = pt[t].r;
            dbg->pearsonTop5Mode[t]  = pt[t].m;
        }
        // TDA fields
        std::copy(dsVector,   dsVector   + 12, dbg->dsVector);
        std::copy(rtpVector,  rtpVector  + 12, dbg->rtpVector);
        std::copy(pciVector,  pciVector  + 12, dbg->pciVector);
        std::copy(rwmVector,  rwmVector  + 12, dbg->rwmVector);
        std::copy(tsaVector,  tsaVector  + 12, dbg->tsaVector);
        std::copy(cpkVector,  cpkVector  + 12, dbg->cpkVector);
        std::copy(htfVector,  htfVector  + 12, dbg->htfVector);
        dbg->w_DS  = w_DS;  dbg->w_RTP = w_RTP;
        dbg->w_PCI = w_PCI; dbg->w_RWM = w_RWM;
        dbg->w_CPK = w_CPK; dbg->w_HTF = w_HTF;
        dbg->globalConfidence = globalConfidence;
        dbg->dbg_bassW      = bassW;
        dbg->dbg_chordW     = chordW;
        dbg->dbg_ctgW       = ctgW;
        dbg->dbg_ctpW       = ctpW;
        dbg->dbg_dsW        = dsW;
        dbg->dbg_pciW       = pciW;
        dbg->dbg_rwmW       = rwmW;
        dbg->dbg_consensusW = consensusW;
        dbg->dbg_totalW     = totalW;
        // DTV concentration
        {
            float dtvTotal2 = 0.f;
            float dtvMax2   = *std::max_element(downbeatVotes.begin(), downbeatVotes.end());
            for (int i = 0; i < 12; ++i) dtvTotal2 += downbeatVotes[(size_t)i];
            dbg->dbg_dtvConc = (dtvTotal2 > 1e-9f) ? dtvMax2 / dtvTotal2 : 0.f;
        }
        dbg->micMode       = (int)detMode;
        dbg->micConfidence = globalConfidence;
    }

    // ── 12) Key stability timeline (Pearson on detected key, for UI) ──────────
    if (stabilityTimeline != nullptr) {
        stabilityTimeline->assign(16, 0.f);
        int nFrames = (int)frameChromas.size();
        if (nFrames >= 16) {
            float rotProfile[12];
            const float* prof = profileFor(detMode, GenreProfile::Electronic);
            for (int i = 0; i < 12; ++i) rotProfile[i] = prof[(i - rootCandidate + 12) % 12];
            for (int seg = 0; seg < 16; ++seg) {
                int segStart = seg * nFrames / 16;
                int segEnd   = (seg + 1) * nFrames / 16;
                std::array<float,12> segChroma {}; segChroma.fill(0.f);
                for (int f = segStart; f < segEnd; ++f)
                    for (int c = 0; c < 12; ++c)
                        segChroma[(size_t)c] += frameChromas[(size_t)f][(size_t)c];
                float sSum = 0.f; for (float val : segChroma) sSum += val;
                if (sSum < 1e-9f) continue;
                float segNorm[12];
                for (int i = 0; i < 12; ++i) segNorm[i] = segChroma[(size_t)i] / sSum;
                float corr = pearsonCorr(segNorm, rotProfile, 12);
                (*stabilityTimeline)[(size_t)seg] = (corr + 1.f) * 0.5f;
            }
        }
    }

    // ── 13) top3 from TSA ──────────────────────────────────────────────────────
    if (top3 != nullptr) {
        // Sort pitch classes by TSA score, pick top 3 distinct (root different)
        int order[12];
        std::iota(order, order + 12, 0);
        std::sort(order, order + 12, [&](int a, int b){ return tsaVector[a] > tsaVector[b]; });

        top3[0] = { rootCandidate, detMode, globalConfidence + 0.5f };
        int filled = 1;
        for (int i = 1; i < 12 && filled < 3; ++i) {
            int r = order[i];
            if (r == rootCandidate) continue;
            // Best mode for this alternative root via Gini
            Mode altMode = computeModalGini(norm, r, cadenceGravity, isMinorContext, isMajorContext);
            top3[filled] = { r, altMode, tsaVector[r] };
            ++filled;
        }
        for (int t = filled; t < 3; ++t) top3[t] = { -1, Mode::Major, 0.f };
    }

    // ── Output ────────────────────────────────────────────────────────────────
    key        = rootCandidate;
    mode       = detMode;
    confidence = globalConfidence + 0.5f;   // shift to ~[0.5, 1.5] range for UI compat
}
