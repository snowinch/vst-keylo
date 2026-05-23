#pragma once

extern "C" {
#include "aubio.h"
}

class PitchDetector
{
public:
    PitchDetector() = default;
    ~PitchDetector();

    void prepare(double sampleRate, int fftSize, int hopSize);
    void reset();

    void processSamples(const float* samples, int numSamples);

    int   getRootNote()        const { return rootNote; }
    float getFrequency()       const { return rootFreq; }
    // 0..1 normalized confidence (fraction of frames with strong pitch detection)
    float getPitchConfidence() const;

private:
    aubio_pitch_t* pitch  { nullptr };
    fvec_t*        input  { nullptr };
    fvec_t*        output { nullptr };

    int    fftSz      { 4096 };
    int    hop        { 512 };
    double sr         { 44100.0 };

    float freqAccum   { 0.f };
    float weightAccum { 0.f };
    int   framesTotal { 0 };
    int   rootNote    { -1 };
    float rootFreq    { 0.f };
};
