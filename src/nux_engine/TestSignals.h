/* NUX DAW - reference test signals.

   Deterministic generators used by the Virtual Audio Lab. Every one of them is derived
   from a stated formula, so a measurement can be checked against a known answer rather
   than against whatever the engine happened to produce.

   No randomness is used except for the noise generators, which are seeded so the same
   seed always gives the same samples. */
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <random>

namespace nux::lab
{
enum class SignalType
{
    silence,
    sine,
    square,
    triangle,
    sawtooth,
    whiteNoise,
    pinkNoise,
    dcOffset,
    twoTone,        // for intermodulation distortion
    sineSweep,      // exponential, for frequency response
    impulse
};

struct SignalSpec
{
    SignalType type = SignalType::sine;
    double frequency = 1000.0;
    double frequency2 = 1100.0;      // second tone for twoTone
    double sweepEndHz = 20000.0;     // end frequency for sineSweep
    float amplitude = 0.5f;          // linear, relative to full scale
    float dcLevel = 0.25f;
    double durationSeconds = 1.0;
    double sampleRate = 44100.0;
    int numChannels = 2;
    unsigned int seed = 12345;
    bool invertRightChannel = false; // for stereo phase tests
};

/** Builds the described signal. Every generator is deterministic. */
juce::AudioBuffer<float> generateSignal (const SignalSpec& spec);

/** A named signal, for reports. */
juce::String getSignalName (SignalType type);

/** The DC offset the specification asks for, so a test can assert against it. */
float expectedDcOffset (const SignalSpec& spec);

/** The RMS a perfectly generated signal of this spec should have. */
float expectedRms (const SignalSpec& spec);
}
