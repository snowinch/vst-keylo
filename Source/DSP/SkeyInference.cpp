#include "SkeyInference.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

// ── Key mapping ───────────────────────────────────────────────────────────────
// S-KEY: major A=0..G#=11,  minor B=12..Bb=23
// Ours:  major C=0..B=11,   minor C=12..B=23  (root 0-11, C=0)

static SkeyResult decodeSkeyIndex (int idx, const float* probs)
{
    SkeyResult r;
    r.confidence = probs[idx];

    if (idx < 12)                          // major
    {
        r.isMinor = false;
        r.root    = (idx + 9) % 12;        // A(0)->9, C(3)->0, G#(11)->8
    }
    else                                   // minor
    {
        r.isMinor = true;
        int mr = idx - 12;                 // B=0,C=1,C#=2,...Bb=11
        r.root = (mr + 11) % 12;           // B(0)->11, C(1)->0, C#(2)->1
    }

    return r;
}

// ── Linear resampler ──────────────────────────────────────────────────────────
static std::vector<float> resampleTo22050 (const float* in, int n, double fromSr)
{
    constexpr double kTargetSr = 22050.0;
    if (std::abs (fromSr - kTargetSr) < 1.0)
        return std::vector<float> (in, in + n);

    double ratio = kTargetSr / fromSr;
    int outLen   = static_cast<int> (n * ratio);
    std::vector<float> out (static_cast<size_t> (outLen));

    for (int i = 0; i < outLen; ++i)
    {
        double srcPos = i / ratio;
        int    s0     = static_cast<int> (srcPos);
        int    s1     = std::min (s0 + 1, n - 1);
        float  frac   = static_cast<float> (srcPos - s0);
        out[static_cast<size_t>(i)] = in[s0] * (1.f - frac) + in[s1] * frac;
    }
    return out;
}

// ── PIMPL ─────────────────────────────────────────────────────────────────────
struct SkeyInference::Impl
{
    Ort::Env             env  { ORT_LOGGING_LEVEL_WARNING, "skey" };
    Ort::SessionOptions  opts;
    Ort::Session         session;
    Ort::AllocatorWithDefaultOptions allocator;

    std::string inputName;
    std::string outputName;

    Impl (const void* data, size_t size)
        : session (env, data, size, opts)
    {
        opts.SetIntraOpNumThreads (1);
        opts.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        inputName  = session.GetInputNameAllocated  (0, allocator).get();
        outputName = session.GetOutputNameAllocated (0, allocator).get();
    }
};

// ── SkeyInference ─────────────────────────────────────────────────────────────
SkeyInference::SkeyInference (const void* modelData, size_t modelSize)
{
    try
    {
        m_impl  = std::make_unique<Impl> (modelData, modelSize);
        m_loaded = true;
    }
    catch (const std::exception& e)
    {
        FILE* f = fopen ("/tmp/keylo_ort_error.txt", "w");
        if (f) { fprintf (f, "ORT init error: %s\n", e.what()); fclose (f); }
        m_loaded = false;
    }
    catch (...)
    {
        FILE* f = fopen ("/tmp/keylo_ort_error.txt", "w");
        if (f) { fprintf (f, "ORT init error: unknown exception\n"); fclose (f); }
        m_loaded = false;
    }
}

SkeyInference::~SkeyInference() = default;

SkeyResult SkeyInference::predict (const float* audio, int n, double fromSr)
{
    if (! m_loaded) return {};

    try
    {
        // 1. Resample to 22050 Hz
        auto buf = resampleTo22050 (audio, n, fromSr);

        // 2. Normalize to -1..1
        float mx = 0.f;
        for (float s : buf) mx = std::max (mx, std::abs (s));
        if (mx > 1e-9f)
            for (float& s : buf) s /= mx;

        // 3. Build input tensor [1, T]
        std::vector<int64_t> shape { 1, static_cast<int64_t> (buf.size()) };
        auto memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value tensor = Ort::Value::CreateTensor<float> (
            memInfo, buf.data(), buf.size(), shape.data(), 2);

        // 4. Run
        const char* inNames[]  = { m_impl->inputName.c_str()  };
        const char* outNames[] = { m_impl->outputName.c_str() };
        auto outputs = m_impl->session.Run (
            Ort::RunOptions { nullptr }, inNames, &tensor, 1, outNames, 1);

        // 5. Decode — [24] softmax probabilities
        const float* probs = outputs[0].GetTensorData<float>();
        int best = 0;
        for (int i = 1; i < 24; ++i)
            if (probs[i] > probs[best]) best = i;

        return decodeSkeyIndex (best, probs);
    }
    catch (...) { return {}; }
}
