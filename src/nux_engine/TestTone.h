/* NUX DAW - test tone generator.

   A 440 Hz sine used to prove that audio really reaches the selected output
   device. It lives on the audio thread: no allocation, no locking, no file I/O.

   The gain is ramped per sample instead of switched, because switching a sine on
   and off at block boundaries produces an audible click. */
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

namespace nux
{
class TestToneGenerator
{
public:
    TestToneGenerator() = default;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        phase = 0.0;
        currentGain = 0.0f;
        targetGain.store (0.0f, std::memory_order_relaxed);
    }

    void setFrequency (double newFrequency)
    {
        frequency.store (juce::jlimit (20.0, 20000.0, newFrequency), std::memory_order_relaxed);
    }

    double getFrequency() const noexcept { return frequency.load (std::memory_order_relaxed); }

    /** 0..1. The ramp makes the on/off transition click-free. */
    void setLevel (float newLevel)
    {
        targetGain.store (juce::jlimit (0.0f, 1.0f, newLevel), std::memory_order_relaxed);
    }

    float getLevel() const noexcept
    {
        return targetGain.load (std::memory_order_relaxed);
    }

    bool isActive() const noexcept { return getLevel() > 0.0f; }

    /** Audio thread: adds the tone into `buffer` (stereo, same signal both sides). */
    void render (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        if (numSamples <= 0 || buffer.getNumChannels() <= 0)
            return;

        const auto target = targetGain.load (std::memory_order_relaxed);

        // Nothing to do unless the tone is on or still ramping down.
        if (target <= 0.0f && currentGain <= 0.000001f)
        {
            currentGain = 0.0f;
            return;
        }

        const auto increment = juce::MathConstants<double>::twoPi * getFrequency() / sampleRate;

        // ~5 ms ramp: fast enough to feel instant, slow enough not to click.
        const auto rampStep = (float) (1.0 / (sampleRate * 0.005));

        for (int i = 0; i < numSamples; ++i)
        {
            if (currentGain < target)
                currentGain = juce::jmin (target, currentGain + rampStep);
            else if (currentGain > target)
                currentGain = juce::jmax (target, currentGain - rampStep);

            // 0.35 keeps a full-scale tone well below clipping with headroom to spare.
            const auto value = (float) (0.35 * std::sin (phase)) * currentGain;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addSample (ch, i, value);

            phase += increment;

            if (phase > juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
        }
    }

    /** Audio thread: silence without a ramp (panic / device change). */
    void reset() noexcept
    {
        phase = 0.0;
        currentGain = 0.0f;
        targetGain.store (0.0f, std::memory_order_relaxed);
    }

private:
    std::atomic<double> frequency { 440.0 };
    std::atomic<float> targetGain { 0.0f };
    double sampleRate = 44100.0;
    double phase = 0.0;
    float currentGain = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestToneGenerator)
};
}
