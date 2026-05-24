#include "Analyser.h"
#include <BinaryData.h>
#include <cmath>
#include <algorithm>
#include <cstring>

static constexpr int kFFTSz = 4096;

// ── AnalysisResult ────────────────────────────────────────────────────────────
const char* AnalysisResult::noteName(int n)
{
    static const char* N[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return (n >= 0 && n < 12) ? N[n] : "–";
}

std::string AnalysisResult::keyString() const
{
    if (key < 0) return "–";
    return std::string(noteName(key));
}

std::string AnalysisResult::modeString() const
{
    if (key < 0) return "–";
    return modeName(mode);
}

std::string AnalysisResult::rootString() const
{
    return rootNote >= 0 ? noteName(rootNote) : "–";
}

std::string AnalysisResult::bpmString() const
{
    if (bpm <= 0.f) return "–";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", bpm);
    return buf;
}

// ── Worker ────────────────────────────────────────────────────────────────────
struct AnalyserWorker : public juce::Thread
{
    explicit AnalyserWorker(std::function<void()> fn)
        : juce::Thread("KeyloWorker"), work(std::move(fn)) {}
    void run() override { work(); }
    std::function<void()> work;
};

// ── Analyser ─────────────────────────────────────────────────────────────────
Analyser::Analyser()
{
    skey = std::make_unique<SkeyInference> (
        BinaryData::skey_onnx, (size_t) BinaryData::skey_onnxSize);
}
Analyser::~Analyser() { if (worker) worker->stopThread(1000); }

void Analyser::prepare(double sampleRate, int /*blockSize*/)
{
    sr = sampleRate;

    ringSize = (int)(sampleRate * kMaxSeconds);
    {
        juce::SpinLock::ScopedLockType lk(ringLock);
        ring.assign((size_t)ringSize, 0.f);
        writeHead    = 0;
        totalWritten = 0;
    }

    targetSamples = (int)(sampleRate * kSampleSecs);

    rtAccum.assign((size_t)kHop, 0.f);
    rtAccumPos = 0;
    rtBpmDet.prepare(sampleRate, kHop);

    // Ring azzerato = stato deve essere Idle.
    // Senza questo, prepareToPlay() lascia lo state machine inconsistente.
    state.store((int)AnalyserState::Idle);
    pendingAutoStart.store(false);
    readyPaused.store(false);
    readySilenceFrames.store(0);
    { juce::SpinLock::ScopedLockType lk(resultLock); result = {}; }
}

void Analyser::reset()
{
    if (worker) { worker->stopThread(500); worker.reset(); }

    {
        juce::SpinLock::ScopedLockType lk(ringLock);
        std::fill(ring.begin(), ring.end(), 0.f);
        writeHead    = 0;
        totalWritten = 0;
    }

    rtAccumPos = 0;
    rtBpmDet.reset();
    rtBpmDet.prepare(sr, kHop);
    levelDB.store(-96.f);
    readyPaused.store(false);
    readySilenceFrames.store(0);

    {
        juce::SpinLock::ScopedLockType lk(resultLock);
        result = {};
    }

    state.store((int)AnalyserState::Idle);
}

void Analyser::startCollecting()
{
    // Hard reset of buffer & RT state, then enter Collecting.
    {
        juce::SpinLock::ScopedLockType lk(ringLock);
        std::fill(ring.begin(), ring.end(), 0.f);
        writeHead    = 0;
        totalWritten = 0;
    }
    rtAccumPos = 0;
    rtBpmDet.reset();
    rtBpmDet.prepare(sr, kHop);
    readySilenceFrames.store(0);
    {
        juce::SpinLock::ScopedLockType lk(resultLock);
        result = {};
    }
    state.store((int)AnalyserState::Collecting);
}

void Analyser::resumeCollecting()
{
    // Preserva ring e writeHead — continua a scrivere da dove era.
    // Reset totalWritten: evita re-trigger immediato (totalWritten era già
    // >= targetSamples). Il ring si riempie di audio fresco prima della
    // prossima analisi.
    {
        juce::SpinLock::ScopedLockType lk(ringLock);
        totalWritten = 0;
    }
    rtAccumPos = 0;
    state.store((int)AnalyserState::Collecting);
}

void Analyser::pushBuffer(const juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();
    int numCh      = buffer.getNumChannels();

    // Block RMS → live dBFS
    float rmsSum = 0.f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* p = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i) rmsSum += p[i] * p[i];
    }
    float rms = std::sqrt(rmsSum / (float)(numSamples * numCh));
    levelDB.store(rms > 1e-9f ? 20.f * std::log10(rms) : -96.f);

    // Audio thread: only set a flag — startCollecting()/resumeCollecting() must
    // NOT run on the real-time thread (std::fill on ring is non-RT-safe).
    auto st  = (AnalyserState)state.load();
    float lvl = levelDB.load();
    if (st == AnalyserState::Idle && lvl > kAutoStartDB)
    {
        pendingAutoStart.store(true);
    }
    else if (st == AnalyserState::Ready)
    {
        if (lvl <= kAutoStartDB)
        {
            // Accumulate silence samples — only set readyPaused after ≥2 s sustained silence.
            // This prevents beat gaps / brief quiet moments from triggering a restart.
            int accumulated = readySilenceFrames.fetch_add(numSamples) + numSamples;
            if (accumulated >= (int)(sr * 2.0))
                readyPaused.store(true);
        }
        else
        {
            readySilenceFrames.store(0);       // reset on any audible buffer
            if (readyPaused.load())
            {
                pendingAutoStart.store(true);  // audio back after real silence → resume
                readyPaused.store(false);
            }
        }
    }

    if (state.load() != (int)AnalyserState::Collecting) return;

    // Write mono into ring + accumulate for RT BPM
    {
        juce::SpinLock::ScopedLockType lk(ringLock);
        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.f;
            for (int ch = 0; ch < numCh; ++ch)
                mono += buffer.getReadPointer(ch)[i];
            mono /= (float)numCh;

            ring[(size_t)writeHead] = mono;
            writeHead = (writeHead + 1) % ringSize;
            if (totalWritten < ringSize) ++totalWritten;
        }
    }

    // RT BPM accumulator (audio thread only, no lock)
    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.f;
        for (int ch = 0; ch < numCh; ++ch)
            mono += buffer.getReadPointer(ch)[i];
        mono /= (float)numCh;

        rtAccum[(size_t)rtAccumPos++] = mono;
        if (rtAccumPos >= kHop)
        {
            if (rtBpmDet.processSamples(rtAccum.data(), kHop))
            {
                juce::SpinLock::ScopedLockType lk(resultLock);
                result.bpm = rtBpmDet.getBPM();
            }
            rtAccumPos = 0;
        }
    }

    // Auto-trigger analysis when 12 s of audio have been collected
    if (totalWritten >= targetSamples)
        triggerAnalysis();
}

bool Analyser::pollAutoStart()
{
    if (!pendingAutoStart.exchange(false)) return false;
    auto st = (AnalyserState)state.load();
    if (st == AnalyserState::Idle)
    {
        startCollecting();   // fresh start: ring azzerato
        return true;
    }
    if (st == AnalyserState::Ready)
    {
        resumeCollecting();  // resume: ring preservato, risultato visibile
        return true;
    }
    // Collecting o Analysing: già attivo, non fare nulla.
    return false;
}

void Analyser::triggerAnalysis()
{
    if ((AnalyserState)state.load() == AnalyserState::Analysing) return;

    int n;
    { juce::SpinLock::ScopedLockType lk(ringLock); n = totalWritten; }
    if (n < (int)(sr * kMinSeconds))
    {
        state.store((int)AnalyserState::Idle);
        return;
    }

    state.store((int)AnalyserState::Analysing);
    if (worker) { worker->stopThread(300); worker.reset(); }
    worker = std::make_unique<AnalyserWorker>([this]{ runAnalysis(); });
    worker->startThread(juce::Thread::Priority::low);
}

AnalyserState Analyser::getState() const noexcept
{
    return (AnalyserState)state.load();
}

AnalysisResult Analyser::getResult() const
{
    juce::SpinLock::ScopedLockType lk(resultLock);
    return result;
}

float Analyser::getLevelDB() const noexcept { return levelDB.load(); }

float Analyser::getSecondsAccum() const
{
    juce::SpinLock::ScopedLockType lk(ringLock);
    return (float)totalWritten / (float)sr;
}

void Analyser::runAnalysis()
{
    int    snapN;
    int    snapWriteHead;
    int    snapRingSize;
    double snapSr;
    {
        juce::SpinLock::ScopedLockType lk(ringLock);
        snapN         = totalWritten;
        snapWriteHead = writeHead;
        snapRingSize  = ringSize;
        snapSr        = sr;
    }

    std::vector<float> linear((size_t)snapN);

    // Copy ring buffer WITHOUT holding the spinlock: data we need is historical
    // (before snapWriteHead), so the race on the few samples near writeHead is
    // negligible for analysis. Holding the spinlock across a 2 MB memcpy would
    // busy-spin the audio thread for tens of ms → DAW system overload.
    if (snapN < snapRingSize)
        std::memcpy(linear.data(), ring.data(), (size_t)snapN * sizeof(float));
    else
    {
        int tail = snapRingSize - snapWriteHead;
        std::memcpy(linear.data(),        ring.data() + snapWriteHead, (size_t)tail * sizeof(float));
        std::memcpy(linear.data() + tail, ring.data(),                 (size_t)snapWriteHead * sizeof(float));
    }

    int n = (int)linear.size();
    BPMDetector bpm; bpm.prepare(snapSr, kHop);
    KeyDetector key; key.prepare(snapSr, kFFTSz, kHop);
    PitchDetector pit; pit.prepare(snapSr, kFFTSz, kHop);

    for (int off = 0; off + kHop <= n; off += kHop)
    {
        bpm.processSamples(linear.data() + off, kHop);
        key.processSamples(linear.data() + off, kHop);
        pit.processSamples(linear.data() + off, kHop);
    }

    float rmsSum = 0.f;
    for (int i = 0; i < n; ++i) rmsSum += linear[(size_t)i] * linear[(size_t)i];
    float rms = std::sqrt(rmsSum / (float)n);

    AnalysisResult r;
    r.bpm          = bpm.getBPM();
    r.energy       = rms > 1e-9f ? 20.f * std::log10(rms) : -96.f;
    r.secondsAccum = (float)snapN / (float)snapSr;
    r.rootNote     = pit.getRootNote();

    GenreProfile g = (GenreProfile)currentGenre.load();
    InputMode    im = (InputMode)currentInputMode.load();
    KeyCandidate top3[3];
    key.getKey(r.key, r.mode, r.confidence,
               r.bpm, pit.getRootNote(), pit.getPitchConfidence(),
               g, top3, &r.stabilityTimeline, nullptr, im);

    // Save TSA result before S-KEY may override
    int  tsaKey  = r.key;
    Mode tsaMode = r.mode;

    r.alt1    = top3[1];
    r.alt2    = top3[2];

    // ── S-KEY ONNX fusion ─────────────────────────────────────────────────
    // TSA gives root 0-11 + detailed mode; S-KEY gives root 0-11 + major/minor.
    // If they agree on root  → keep TSA (more mode detail), raise confidence.
    // If they disagree       → trust S-KEY (better on test set), use its root
    //                          with NaturalMinor / Major, lower confidence.
    if (skey && skey->isLoaded())
    {
        // Split 12s buffer into two 6s windows, run S-KEY on each.
        // If roots agree  → high confidence (averaged).
        // If roots differ → use the higher-confidence window, penalised.
        constexpr double kWinSecs = 6.0;
        int winSamples = (int)(kWinSecs * snapSr);

        int n1 = std::min (n, winSamples);
        SkeyResult res1 = skey->predict (linear.data(), n1, snapSr);

        SkeyResult res2 {};
        if (n > winSamples)
            res2 = skey->predict (linear.data() + winSamples, n - winSamples, snapSr);

        // Pick final result
        SkeyResult skeyFinal {};
        if (res1.root >= 0 && res2.root >= 0)
        {
            if (res1.root == res2.root && res1.isMinor == res2.isMinor)
            {
                // Both windows agree — strong signal
                skeyFinal.root       = res1.root;
                skeyFinal.isMinor    = res1.isMinor;
                skeyFinal.confidence = (res1.confidence + res2.confidence) * 0.5f * 1.2f; // boost
                skeyFinal.confidence = std::min (skeyFinal.confidence, 1.0f);
            }
            else
            {
                // Windows disagree — take higher confidence, penalise
                const SkeyResult& best = (res1.confidence >= res2.confidence) ? res1 : res2;
                skeyFinal.root       = best.root;
                skeyFinal.isMinor    = best.isMinor;
                skeyFinal.confidence = best.confidence * 0.65f;
            }
        }
        else if (res1.root >= 0)
        {
            skeyFinal = res1;
        }

        if (skeyFinal.root >= 0)
        {
            bool tsaValid   = (r.key >= 0);
            bool rootsAgree = tsaValid && (r.key == skeyFinal.root);

            if (rootsAgree)
            {
                // TSA + S-KEY agree on root → keep TSA mode (richer), boost confidence
                // r.key and r.mode already set by TSA — preserve them
                r.confidence = std::min (1.f,
                    std::max (r.confidence, skeyFinal.confidence) * 1.15f);
                r.skeyWeight = 0.35f;   // TSA majority, S-KEY confirmed
            }
            else
            {
                // Disagree → S-KEY wins (better empirically), penalise
                r.key        = skeyFinal.root;
                r.mode       = skeyFinal.isMinor ? Mode::NaturalMinor : Mode::Major;
                r.confidence = skeyFinal.confidence * 0.75f;
                r.skeyWeight = 0.85f;   // S-KEY dominant
            }
        }

    }

    // ── Alternative keys ──────────────────────────────────────────────────────
    // Replaces TSA top-3 with musically meaningful alternatives.
    auto relativeKey = [](int root, Mode mode) -> KeyCandidate {
        bool isMaj = (mode == Mode::Major || mode == Mode::Mixolydian);
        return { isMaj ? (root + 9) % 12 : (root + 3) % 12,
                 isMaj ? Mode::NaturalMinor : Mode::Major, 0.f };
    };
    auto subdominantKey = [](int root, Mode mode) -> KeyCandidate {
        // Perfect 4th up — captures real tonic when detector landed on the 5th
        // e.g. detected G#m → subdominant = C#m (correct tonic in many trap loops)
        return { (root + 5) % 12, mode, 0.f };
    };

    if (r.key >= 0)
    {
        if (r.skeyWeight >= 0.8f)
        {
            // S-KEY won over TSA disagreement → TSA as second opinion, subdominant as third
            r.alt1 = { tsaKey, tsaMode, top3[0].score };
            r.alt2 = subdominantKey (r.key, r.mode);
        }
        else
        {
            // Both agreed → relative + subdominant (4th above)
            r.alt1 = relativeKey    (r.key, r.mode);
            r.alt2 = subdominantKey (r.key, r.mode);
        }
    }

    r.camelot = camelotKey(r.key, r.mode);

    { juce::SpinLock::ScopedLockType lk(resultLock); result = r; }

    state.store((int)AnalyserState::Ready);
}
