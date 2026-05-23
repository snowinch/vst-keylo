// Standalone key detection test — no JUCE dependency
// Usage: KeyloTest <wav_file> [wav_file ...]
// Expected key and BPM are parsed from the filename:
//   "Name - KEY - BPMbpm - ..." (e.g. "Loop - C#m - 100bpm - artist.wav")

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cassert>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>

#include "DSP/KeyDetector.h"
#include "DSP/BPMDetector.h"
#include "DSP/PitchDetector.h"

// ── Minimal PCM WAV loader ────────────────────────────────────────────────────
struct WavData
{
    std::vector<float> samples;   // interleaved if stereo
    double sampleRate = 0.0;
    int    channels   = 0;
    int    numFrames  = 0;        // per-channel frames
};

static bool loadWav(const std::string& path, WavData& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    auto r4 = [&]() -> uint32_t { uint32_t v=0; f.read((char*)&v,4); return v; };
    auto r2 = [&]() -> uint16_t { uint16_t v=0; f.read((char*)&v,2); return v; };

    if (r4() != 0x46464952u) return false;  // RIFF
    r4();                                    // file size
    if (r4() != 0x45564157u) return false;  // WAVE

    uint16_t audioFmt=0, numCh=0, bps=0;
    uint32_t sr=0;
    bool gotFmt = false;

    while (f.good())
    {
        uint32_t chunkId   = r4();
        uint32_t chunkSize = r4();
        if (!f.good()) break;

        if (chunkId == 0x20746D66u)          // "fmt "
        {
            audioFmt = r2();
            numCh    = r2();
            sr       = r4();
            r4();                            // byteRate
            r2();                            // blockAlign
            bps      = r2();
            if (chunkSize > 16) f.seekg((long long)chunkSize - 16, std::ios::cur);
            gotFmt = true;
        }
        else if (chunkId == 0x61746164u)     // "data"
        {
            if (!gotFmt || sr == 0 || numCh == 0) return false;
            if (audioFmt != 1 && audioFmt != 3) return false;  // only PCM / float32

            int bytesPerSample = bps / 8;
            int totalSamples   = (int)(chunkSize / (uint32_t)bytesPerSample);

            out.sampleRate = (double)sr;
            out.channels   = (int)numCh;
            out.numFrames  = totalSamples / numCh;
            out.samples.resize((size_t)totalSamples);

            for (int i = 0; i < totalSamples; ++i)
            {
                float s = 0.f;
                if (audioFmt == 1)
                {
                    if (bps == 16)
                    {
                        int16_t v; f.read((char*)&v, 2); s = v / 32768.f;
                    }
                    else if (bps == 24)
                    {
                        uint8_t b[3]; f.read((char*)b, 3);
                        int32_t v = (b[2]<<16)|(b[1]<<8)|b[0];
                        if (v & 0x800000) v |= (int32_t)0xFF000000;
                        s = (float)v / 8388608.f;
                    }
                    else if (bps == 32)
                    {
                        int32_t v; f.read((char*)&v, 4);
                        s = (float)v / 2147483648.f;
                    }
                }
                else  // IEEE float 32
                {
                    f.read((char*)&s, 4);
                }
                out.samples[(size_t)i] = s;
            }
            return true;
        }
        else
        {
            f.seekg((long long)chunkSize, std::ios::cur);
        }
    }
    return false;
}

// ── Filename parser ───────────────────────────────────────────────────────────
// Format:  "Title - KEY - BPMbpm - @artist.wav"
struct ExpectedResult
{
    std::string key;   // e.g. "C#m", "Dm", "G#m"
    float       bpm = 0.f;
};

static std::vector<std::string> splitBy(const std::string& s, const std::string& sep)
{
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t found = s.find(sep, pos);
        if (found == std::string::npos) { parts.push_back(s.substr(pos)); break; }
        parts.push_back(s.substr(pos, found - pos));
        pos = found + sep.size();
    }
    return parts;
}

static ExpectedResult parseFilename(const std::string& path)
{
    // Extract basename without extension
    size_t slash = path.rfind('/');
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);

    auto parts = splitBy(base, " - ");
    ExpectedResult r;
    // Second token should be key (contains note name)
    for (size_t i = 1; i < parts.size(); ++i)
    {
        const std::string& p = parts[i];
        // Key: note optionally followed by # or b, then optionally m
        if (!p.empty() && p[0] >= 'A' && p[0] <= 'G')
        {
            r.key = p;
            continue;
        }
        // BPM: ends in "bpm"
        if (p.size() > 3 && p.substr(p.size()-3) == "bpm")
        {
            try { r.bpm = std::stof(p.substr(0, p.size()-3)); } catch (...) {}
        }
    }
    return r;
}

// ── Note name → index ─────────────────────────────────────────────────────────
static const char* kNoteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

static int noteNameToIndex(const std::string& n)
{
    // Strip trailing 'm' or 'maj' etc. to get note portion
    std::string note = n;
    // Remove trailing mode suffix
    while (!note.empty() && (note.back() == 'm' || note.back() == 'j' ||
                              note.back() == 'a'))
        note.pop_back();
    // Handle flats as equivalent sharps
    static const struct { const char* flat; const char* sharp; } kEnharm[] = {
        {"Bb","A#"}, {"Eb","D#"}, {"Ab","G#"}, {"Db","C#"}, {"Gb","F#"}
    };
    for (auto& e : kEnharm)
        if (note == e.flat) { note = e.sharp; break; }
    for (int i = 0; i < 12; ++i)
        if (note == kNoteNames[i]) return i;
    return -1;
}

static bool isMinorKey(const std::string& key)
{
    return !key.empty() && key.back() == 'm';
}

static bool isMinorMode(Mode m)
{
    return m == Mode::NaturalMinor || m == Mode::HarmonicMinor
        || m == Mode::Dorian       || m == Mode::Phrygian;
}

// ── ANSI colours ─────────────────────────────────────────────────────────────
#define GRN "\033[32m"
#define YLW "\033[33m"
#define RED "\033[31m"
#define RST "\033[0m"

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a circular mono window of exactly `targetFrames` starting at `startFrame`.
static std::vector<float> circularWindow(const WavData& wav, int startFrame, int targetFrames)
{
    std::vector<float> out((size_t)targetFrames);
    int total = wav.numFrames;
    for (int i = 0; i < targetFrames; ++i)
    {
        int src = (startFrame + i) % total;
        float s = 0.f;
        for (int ch = 0; ch < wav.channels; ++ch)
            s += wav.samples[(size_t)(src * wav.channels + ch)];
        out[(size_t)i] = s / (float)wav.channels;
    }
    return out;
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf(stderr, "Usage: KeyloTest <wav_file> [wav_file ...]\n");
        return 1;
    }

    constexpr int    kFFTSz    = 4096;
    constexpr int    kHop      = 512;
    constexpr double kTestSec  = 12.0;
    constexpr int    kRuns     = 5;     // offsets per file: 0%, 20%, 40%, 60%, 80%

    // Per-file counters (file correct = majority of runs correct)
    int filesTotal = 0, filesExact = 0, filesRoot = 0;
    // Per-run counters (all runs across all files)
    int runsTotal = 0, runsExact = 0, runsRoot = 0;

    std::printf("\n%-36s  %-6s", "FILE", "EXPECT");
    for (int r = 0; r < kRuns; ++r) std::printf("  %-9s", (std::string("#") + std::to_string(r+1)).c_str());
    std::printf("  CONS   BPM\n");
    std::printf("%s\n", std::string(36+8+kRuns*11+12, '-').c_str());

    for (int a = 1; a < argc; ++a)
    {
        std::string path = argv[a];
        auto expected    = parseFilename(path);

        WavData wav;
        if (!loadWav(path, wav))
        {
            std::fprintf(stderr, "  [!] Cannot load: %s\n", path.c_str());
            continue;
        }

        int targetFrames = (int)(wav.sampleRate * kTestSec);

        // Short filename
        size_t sl = path.rfind('/');
        std::string name = (sl == std::string::npos) ? path : path.substr(sl + 1);
        if (name.size() > 35) name = name.substr(0, 34) + "~";

        int expRoot   = noteNameToIndex(expected.key);
        bool expMinor = isMinorKey(expected.key);

        // BPM from offset-0 window (shared hint across all runs)
        float detBpm = 0.f;
        {
            auto mono = circularWindow(wav, 0, targetFrames);
            BPMDetector bd; bd.prepare(wav.sampleRate, kHop);
            for (int off = 0; off + kHop <= targetFrames; off += kHop)
                bd.processSamples(mono.data() + off, kHop);
            detBpm = bd.getBPM();
        }

        std::string runResults[kRuns];
        int hitExact = 0, hitRoot = 0;

        for (int r = 0; r < kRuns; ++r)
        {
            int startFrame = (int)((float)r / (float)kRuns * (float)wav.numFrames);
            auto mono = circularWindow(wav, startFrame, targetFrames);

            PitchDetector pd; pd.prepare(wav.sampleRate, kFFTSz, kHop);
            KeyDetector   kd; kd.prepare(wav.sampleRate, kFFTSz, kHop);
            for (int off = 0; off + kHop <= targetFrames; off += kHop)
            {
                pd.processSamples(mono.data() + off, kHop);
                kd.processSamples(mono.data() + off, kHop);
            }
            int pitchHint   = pd.getRootNote();
            float pitchConf = pd.getPitchConfidence();

            int   detKey  = -1;
            Mode  detMode = Mode::Major;
            float conf    = 0.f;
            kd.getKey(detKey, detMode, conf, detBpm, pitchHint, pitchConf,
                      GenreProfile::Electronic, nullptr, nullptr, nullptr,
                      InputMode::Loop);

            bool rootOk = (detKey == expRoot);
            bool exact  = rootOk && (expMinor == isMinorMode(detMode));
            if (exact)  ++hitExact;
            if (rootOk) ++hitRoot;

            std::string detNote = (detKey >= 0 && detKey < 12) ? kNoteNames[detKey] : "?";
            std::string detStr  = detNote + " " + modeNameShort(detMode);
            const char* col     = exact ? GRN : (rootOk ? YLW : RED);
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%s%-9s" RST, col, detStr.c_str());
            runResults[r] = buf;

            ++runsTotal;
            if (exact)  ++runsExact;
            if (rootOk) ++runsRoot;
        }

        // File-level score: majority of runs
        bool fileExact = (hitExact >= (kRuns / 2 + 1));
        bool fileRoot  = (hitRoot  >= (kRuns / 2 + 1));
        ++filesTotal;
        if (fileExact) ++filesExact;
        if (fileRoot)  ++filesRoot;

        // Consistency indicator: all same result?
        char consStr[16];
        std::snprintf(consStr, sizeof(consStr), "%d/%d %d/%d",
                      hitExact, kRuns, hitRoot, kRuns);

        std::printf("%-36s  %-6s", name.c_str(), expected.key.c_str());
        for (int r = 0; r < kRuns; ++r)
            std::printf("  %s", runResults[r].c_str());
        std::printf("  %-7s  %.1f\n", consStr, detBpm);

        // Diagnostics for any run that got the root wrong (first failure only)
        for (int r = 0; r < kRuns; ++r)
        {
            int startFrame = (int)((float)r / (float)kRuns * (float)wav.numFrames);
            auto mono = circularWindow(wav, startFrame, targetFrames);

            PitchDetector pd; pd.prepare(wav.sampleRate, kFFTSz, kHop);
            KeyDetector   kd; kd.prepare(wav.sampleRate, kFFTSz, kHop);
            for (int off = 0; off + kHop <= targetFrames; off += kHop)
            {
                pd.processSamples(mono.data() + off, kHop);
                kd.processSamples(mono.data() + off, kHop);
            }
            int pitchHint   = pd.getRootNote();
            float pitchConf = pd.getPitchConfidence();

            int dk=-1; Mode dm=Mode::Major; float dc=0.f;
            KeyDebugInfo info;
            kd.getKey(dk, dm, dc, detBpm, pitchHint, pitchConf,
                      GenreProfile::Electronic, nullptr, nullptr, &info,
                      InputMode::Loop);

            if (dk != expRoot)
            {
                static const char* N[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                int tsaOrder[12]; for(int i=0;i<12;i++) tsaOrder[i]=i;
                std::sort(tsaOrder, tsaOrder+12, [&](int a, int b){ return info.tsaVector[a]>info.tsaVector[b]; });
                std::printf("  [#%d off=%d%%] bass=%s(%.2f) chord=%s(%.2f) dtv=%s(%.2f) ctg=%s\n",
                    r+1, (int)((float)r/kRuns*100),
                    info.bassRoot>=0?N[info.bassRoot]:"?", info.bassFocus,
                    info.chordRoot>=0?N[info.chordRoot]:"?", info.chordFocus,
                    info.dtvMaxRoot>=0?N[info.dtvMaxRoot]:"?", info.dbg_dtvConc,
                    info.ctgMaxRoot>=0?N[info.ctgMaxRoot]:"?");
                std::printf("  TSA: %s=%.3f %s=%.3f %s=%.3f | conf=%.3f\n",
                    N[tsaOrder[0]], info.tsaVector[tsaOrder[0]],
                    N[tsaOrder[1]], info.tsaVector[tsaOrder[1]],
                    N[tsaOrder[2]], info.tsaVector[tsaOrder[2]],
                    info.globalConfidence);
                std::printf("  W: bass=%.3f chord=%.3f ctg=%.3f ctp=%.3f ds=%.3f pci=%.3f rwm=%.3f | total=%.3f\n",
                    info.dbg_bassW, info.dbg_chordW, info.dbg_ctgW, info.dbg_ctpW,
                    info.dbg_dsW, info.dbg_pciW, info.dbg_rwmW,
                    info.dbg_totalW);
                break; // first failure only
            }
        }
    }

    std::printf("\n%s\n", std::string(36+8+kRuns*11+12, '-').c_str());
    std::printf("Files  exact  (majority ≥%d/%d): %d/%d  (%.0f%%)\n",
                kRuns/2+1, kRuns, filesExact, filesTotal,
                filesTotal>0 ? 100.f*filesExact/filesTotal : 0.f);
    std::printf("Files  root   (majority ≥%d/%d): %d/%d  (%.0f%%)\n",
                kRuns/2+1, kRuns, filesRoot,  filesTotal,
                filesTotal>0 ? 100.f*filesRoot/filesTotal  : 0.f);
    std::printf("Runs   exact  (all offsets):     %d/%d  (%.0f%%)\n",
                runsExact, runsTotal,
                runsTotal>0 ? 100.f*runsExact/runsTotal : 0.f);
    std::printf("Runs   root   (all offsets):     %d/%d  (%.0f%%)\n",
                runsRoot,  runsTotal,
                runsTotal>0 ? 100.f*runsRoot/runsTotal  : 0.f);
    std::printf("\n" GRN "green" RST "=exact  " YLW "yellow" RST "=root ok/wrong mode  " RED "red" RST "=wrong root\n\n");

    return 0;
}
