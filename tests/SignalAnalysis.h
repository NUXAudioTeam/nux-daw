/* NUX DAW - signal measurement helpers for the offline audio tests.

   These are the maths the test suite uses to answer questions like "is this really
   440 Hz?", "does it clip?", "is the ramp smooth?". They live in the test target,
   not in the engine, because the engine has no business analysing its own output.

   Everything here works on a plain AudioBuffer, so it can be pointed at a rendered
   block, an exported WAV or a captured take with no changes. */
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace nux::tests
{
struct SignalAnalysis
{
    double frequency = 0.0;      // Hz, measured from zero crossings
    float peak = 0.0f;           // absolute peak amplitude (linear)
    float rms = 0.0f;            // root mean square (linear)
    float peakDb = -200.0f;      // peak in dBFS
    float rmsDb = -200.0f;
    bool clips = false;          // true when any sample reaches or exceeds full scale
    int numSamples = 0;
    int numChannels = 0;
};

/** Measures one channel of a buffer. */
inline SignalAnalysis analyse (const juce::AudioBuffer<float>& buffer, int channel = 0,
                               double sampleRate = 44100.0)
{
    SignalAnalysis result;
    result.numChannels = buffer.getNumChannels();

    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() == 0)
        return result;

    const auto* data = buffer.getReadPointer (channel);
    const auto n = buffer.getNumSamples();

    result.numSamples = n;
    result.peak = buffer.getMagnitude (channel, 0, n);
    result.rms = buffer.getRMSLevel (channel, 0, n);
    result.peakDb = juce::Decibels::gainToDecibels (result.peak, -200.0f);
    result.rmsDb = juce::Decibels::gainToDecibels (result.rms, -200.0f);
    result.clips = result.peak >= 0.999f;

    // Zero crossing frequency: count upward crossings and divide by the elapsed time.
    int crossings = 0;
    int first = -1;
    int last = -1;

    for (int i = 1; i < n; ++i)
    {
        if (data[i - 1] <= 0.0f && data[i] > 0.0f)
        {
            if (first < 0)
                first = i;

            last = i;
            ++crossings;
        }
    }

    if (crossings >= 2 && last > first && sampleRate > 0.0)
    {
        const auto seconds = (double) (last - first) / sampleRate;
        result.frequency = seconds > 0.0 ? (double) (crossings - 1) / seconds : 0.0;
    }

    return result;
}

/** True when every sample is finite (no NaN, no infinity). */
inline bool isFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (data[i]))
                return false;
    }

    return true;
}

/** The largest jump between neighbouring samples, as a proxy for clicks.

    A discontinuity shows up as a step that is far bigger than the signal's natural
    slope. For a sine of amplitude A at frequency f, the per-sample step is at most
    2*pi*f*A/sampleRate, so anything much larger than that is a click. */
inline float largestStep (const juce::AudioBuffer<float>& buffer, int channel = 0)
{
    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() < 2)
        return 0.0f;

    const auto* data = buffer.getReadPointer (channel);
    auto largest = 0.0f;

    for (int i = 1; i < buffer.getNumSamples(); ++i)
        largest = juce::jmax (largest, std::abs (data[i] - data[i - 1]));

    return largest;
}

/** The natural per-sample slope limit of a sine. Anything above this is a click. */
inline float sineStepLimit (float amplitude, double frequency, double sampleRate)
{
    return (float) (2.0 * juce::MathConstants<double>::pi * frequency * amplitude / sampleRate);
}

/** Compares two channels of the same buffer. Returns the largest difference. */
inline float channelDifference (const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() < 2)
        return 0.0f;

    auto largest = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        largest = juce::jmax (largest, std::abs (buffer.getSample (0, i) - buffer.getSample (1, i)));

    return largest;
}

/** Renders a block of a generator into a buffer at a fixed block size. */
template <typename Generator>
inline void renderInto (juce::AudioBuffer<float>& destination, Generator&& generator,
                        int blockSize, int numChannels = 2)
{
    juce::AudioBuffer<float> scratch (numChannels, blockSize);
    int written = 0;

    while (written < destination.getNumSamples())
    {
        const auto remaining = destination.getNumSamples() - written;
        const auto thisBlock = juce::jmin (blockSize, remaining);

        scratch.clear();
        generator (scratch, thisBlock);

        for (int ch = 0; ch < juce::jmin (numChannels, destination.getNumChannels()); ++ch)
            destination.copyFrom (ch, written, scratch, ch, 0, thisBlock);

        written += thisBlock;
    }
}
}
