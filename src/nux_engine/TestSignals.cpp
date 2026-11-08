#include "TestSignals.h"

namespace nux::lab
{
namespace
{
double phaseIncrement (double frequency, double sampleRate)
{
    return juce::MathConstants<double>::twoPi * frequency / juce::jmax (1.0, sampleRate);
}

void writeToChannels (juce::AudioBuffer<float>& buffer, int index, float left, float right)
{
    // The channel count comes from the buffer, never from the caller's request, so a mono
    // buffer is never written through channel 1. Writing past the last channel is silent
    // memory corruption in a Release build, which is exactly how this was found: a mono
    // loudness case corrupted the heap and took the whole test run down with a segfault.
    const auto channels = buffer.getNumChannels();

    buffer.setSample (0, index, left);

    if (channels > 1)
        buffer.setSample (1, index, right);

    for (int ch = 2; ch < channels; ++ch)
        buffer.setSample (ch, index, (left + right) * 0.5f);
}
}

juce::AudioBuffer<float> generateSignal (const SignalSpec& spec)
{
    const auto numChannels = juce::jlimit (1, 8, spec.numChannels);
    const auto sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    const auto numSamples = juce::jmax (1, (int) std::round (spec.durationSeconds * sampleRate));

    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    buffer.clear();

    std::mt19937 rng (spec.seed);
    std::uniform_real_distribution<float> uniform (-1.0f, 1.0f);

    const auto amplitude = juce::jlimit (0.0f, 1.0f, spec.amplitude);
    auto phase = 0.0;
    auto phase2 = 0.0;

    // Pink noise state (Voss-McCartney style running sum).
    double pinkB0 = 0.0, pinkB1 = 0.0, pinkB2 = 0.0, pinkB3 = 0.0, pinkB4 = 0.0, pinkB5 = 0.0;

    // For the sweep: instantaneous phase is integrated, not multiplied, so the
    // exponential sweep stays continuous.
    double sweepPhase = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto progress = (double) i / (double) numSamples;

        if (spec.type == SignalType::sineSweep)
        {
            // Exponential sweep from frequency to sweepEndHz.
            const auto current = spec.frequency * std::pow (spec.sweepEndHz / juce::jmax (1.0, spec.frequency), progress);
            sweepPhase += phaseIncrement (current, sampleRate);
        }

        float value = 0.0f;

        switch (spec.type)
        {
            case SignalType::silence:
                value = 0.0f;
                break;

            case SignalType::sine:
                value = amplitude * (float) std::sin (phase);
                break;

            case SignalType::square:
                value = amplitude * (std::sin (phase) >= 0.0 ? 1.0f : -1.0f);
                break;

            case SignalType::triangle:
            {
                // Triangle from the phase, so the result is band-limited only by sampling.
                const auto normalised = phase / juce::MathConstants<double>::twoPi;
                const auto fraction = normalised - std::floor (normalised);
                value = amplitude * (float) (4.0 * std::abs (fraction - 0.5) - 1.0);
                break;
            }

            case SignalType::sawtooth:
            {
                const auto normalised = phase / juce::MathConstants<double>::twoPi;
                const auto fraction = normalised - std::floor (normalised);
                value = amplitude * (float) (2.0 * fraction - 1.0);
                break;
            }

            case SignalType::whiteNoise:
                value = amplitude * uniform (rng);
                break;

            case SignalType::pinkNoise:
            {
                const auto white = uniform (rng);
                pinkB0 = 0.99886 * pinkB0 + white * 0.0555179;
                pinkB1 = 0.99332 * pinkB1 + white * 0.0750759;
                pinkB2 = 0.96900 * pinkB2 + white * 0.1538520;
                pinkB3 = 0.86650 * pinkB3 + white * 0.3104856;
                pinkB4 = 0.55000 * pinkB4 + white * 0.5329522;
                pinkB5 = -0.7616 * pinkB5 - white * 0.0168980;
                const auto pink = pinkB0 + pinkB1 + pinkB2 + pinkB3 + pinkB4 + pinkB5
                                  + white * 0.5362;
                value = amplitude * (float) (pink * 0.11);   // roughly unity scale
                break;
            }

            case SignalType::dcOffset:
                value = juce::jlimit (-1.0f, 1.0f, spec.dcLevel);
                break;

            case SignalType::twoTone:
                value = amplitude * 0.5f * (float) (std::sin (phase) + std::sin (phase2));
                break;

            case SignalType::sineSweep:
                value = amplitude * (float) std::sin (sweepPhase);
                break;

            case SignalType::impulse:
                value = i == 0 ? amplitude : 0.0f;
                break;
        }

        const auto right = spec.invertRightChannel ? -value : value;
        writeToChannels (buffer, i, value, right);

        phase += phaseIncrement (spec.frequency, sampleRate);
        phase2 += phaseIncrement (spec.frequency2, sampleRate);

        if (phase > juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        if (phase2 > juce::MathConstants<double>::twoPi)
            phase2 -= juce::MathConstants<double>::twoPi;
    }

    return buffer;
}

juce::String getSignalName (SignalType type)
{
    switch (type)
    {
        case SignalType::silence:     return "silence";
        case SignalType::sine:        return "sine";
        case SignalType::square:      return "square";
        case SignalType::triangle:    return "triangle";
        case SignalType::sawtooth:    return "sawtooth";
        case SignalType::whiteNoise:  return "white noise";
        case SignalType::pinkNoise:   return "pink noise";
        case SignalType::dcOffset:    return "DC offset";
        case SignalType::twoTone:     return "two tone";
        case SignalType::sineSweep:   return "sine sweep";
        case SignalType::impulse:     return "impulse";
    }

    return "unknown";
}

float expectedDcOffset (const SignalSpec& spec)
{
    return spec.type == SignalType::dcOffset ? juce::jlimit (-1.0f, 1.0f, spec.dcLevel) : 0.0f;
}

float expectedRms (const SignalSpec& spec)
{
    const auto a = juce::jlimit (0.0f, 1.0f, spec.amplitude);

    switch (spec.type)
    {
        case SignalType::silence:   return 0.0f;
        case SignalType::sine:      return a / juce::MathConstants<float>::sqrt2;
        case SignalType::square:    return a;
        case SignalType::triangle:  return a / std::sqrt (3.0f);
        case SignalType::sawtooth:  return a / std::sqrt (3.0f);
        case SignalType::dcOffset:  return std::abs (juce::jlimit (-1.0f, 1.0f, spec.dcLevel));
        case SignalType::twoTone:   return a * 0.5f / juce::MathConstants<float>::sqrt2;
        case SignalType::impulse:   return a / std::sqrt ((float) juce::jmax (1, (int) (spec.durationSeconds * spec.sampleRate)));
        case SignalType::whiteNoise:
        case SignalType::pinkNoise:
        case SignalType::sineSweep:
            return -1.0f;   // not analytically fixed; a test must not assert a value
    }

    return -1.0f;
}
}
