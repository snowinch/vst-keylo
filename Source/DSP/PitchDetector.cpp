#include "PitchDetector.h"
#include <cmath>
#include <cstring>
#include <algorithm>

PitchDetector::~PitchDetector() { reset(); }

void PitchDetector::prepare(double sampleRate, int fftSize, int hopSize)
{
    reset();
    sr    = sampleRate;
    fftSz = fftSize;
    hop   = hopSize;

    pitch  = new_aubio_pitch("yinfast", (uint_t)fftSize, (uint_t)hopSize, (uint_t)sampleRate);
    input  = new_fvec((uint_t)hopSize);
    output = new_fvec(1);

    aubio_pitch_set_unit(pitch, "Hz");
    aubio_pitch_set_silence(pitch, -60.f);
}

void PitchDetector::reset()
{
    if (pitch)  { del_aubio_pitch(pitch);   pitch  = nullptr; }
    if (input)  { del_fvec(input);          input  = nullptr; }
    if (output) { del_fvec(output);         output = nullptr; }
    freqAccum   = 0.f;
    weightAccum = 0.f;
    framesTotal = 0;
    rootNote    = -1;
    rootFreq    = 0.f;
}

static int freqToNote(float freq)
{
    if (freq < 16.35f || freq > 8372.f) return -1;
    double midi = 12.0 * std::log2((double)freq / 440.0) + 69.0;
    return (int)std::round(midi) % 12;
}

void PitchDetector::processSamples(const float* samples, int numSamples)
{
    if (!pitch || !input || !output) return;

    int written = 0;
    while (written + hop <= numSamples)
    {
        std::memcpy(input->data, samples + written, (size_t)hop * sizeof(float));
        aubio_pitch_do(pitch, input, output);

        float freq = output->data[0];
        float conf = aubio_pitch_get_confidence(pitch);

        ++framesTotal;
        if (freq > 16.35f && conf > 0.5f)
        {
            freqAccum   += freq * conf;
            weightAccum += conf;
        }
        written += hop;
    }

    if (weightAccum > 1.f)
    {
        rootFreq = freqAccum / weightAccum;
        rootNote = freqToNote(rootFreq);
    }
}

float PitchDetector::getPitchConfidence() const
{
    if (framesTotal == 0) return 0.f;
    // weightAccum is sum of per-frame confidences (max 1.0 each)
    return std::min(1.f, weightAccum / (float)framesTotal);
}
