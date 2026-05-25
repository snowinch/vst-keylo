#pragma once
#include <memory>
#include <string>

/** Result from S-KEY ONNX inference. */
struct SkeyResult
{
    int   root     { -1 };    // 0-11, C=0..B=11; -1 = failure
    bool  isMinor  { false };
    float confidence { 0.f }; // max softmax probability [0..1]
};

/**
 * Thin PIMPL wrapper around ONNX Runtime.
 * Holds the session alive; thread-safe only for single-threaded use
 * (the Analyser worker thread calls predict() serially — fine).
 *
 * ONNX model expects: audio float32 [1, T], mono, sr=22050, normalised -1..1.
 * Resampling from any DAW sr is done internally.
 */
class SkeyInference
{
public:
    /** Load model from embedded binary data (BinaryData::skey_onnx). */
    SkeyInference (const void* modelData, size_t modelSize);
    ~SkeyInference();

    bool isLoaded() const noexcept { return m_loaded; }

    /**
     * Run inference. Resamples from fromSr to 22050 internally.
     * audio  — mono float32 samples (any sr)
     * n      — number of samples
     * fromSr — DAW sample rate
     */
    SkeyResult predict (const float* audio, int n, double fromSr);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_loaded { false };
};
