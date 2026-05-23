#include "Analyser.h"
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
Analyser::Analyser()  = default;
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
            readyPaused.store(true);           // silence gap observed
        else if (readyPaused.load())
        {
            pendingAutoStart.store(true);      // audio back after silence → resume
            readyPaused.store(false);
        }
        // else: audio still playing after analysis → keep showing result, do nothing
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
    r.alt1    = top3[1];
    r.alt2    = top3[2];
    r.camelot = camelotKey(r.key, r.mode);

    { juce::SpinLock::ScopedLockType lk(resultLock); result = r; }

    state.store((int)AnalyserState::Ready);
}
