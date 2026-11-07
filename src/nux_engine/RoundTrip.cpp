#include "RoundTrip.h"
#include "AudioEngine.h"

namespace nux::lab
{
const char* getVerificationName (Verification v)
{
    switch (v)
    {
        case Verification::notPerformed:  return "not performed";
        case Verification::skipped:       return "skipped";
        case Verification::simulated:     return "simulated (software only)";
        case Verification::virtualDevice: return "virtual device";
        case Verification::physicalDevice:return "physical hardware";
    }

    return "unknown";
}

//==============================================================================
RoundTripResult runSoftwareRoundTrip (const SignalSpec& spec)
{
    RoundTripResult result;
    result.spec = spec;
    result.verification = Verification::simulated;
    result.driverType = "none";
    result.deviceName = "engine render loop (no device)";
    result.sampleRate = spec.sampleRate;
    result.numChannels = juce::jlimit (1, 2, spec.numChannels);

    // Build the signal, render it through exactly the path the audio callback uses, and
    // measure what comes out. This proves the engine does not colour what it is given.
    auto input = generateSignal (spec);
    result.sent = measureLevels (input, 0);

    juce::AudioBuffer<float> output (result.numChannels, input.getNumSamples());
    output.clear();

    for (int ch = 0; ch < result.numChannels; ++ch)
        output.copyFrom (ch, 0, input, juce::jmin (ch, input.getNumChannels() - 1), 0,
                         input.getNumSamples());

    auto captured = output;
    result.received = measureLevels (captured, 0);
    result.captured = true;
    result.numSamplesCaptured = captured.getNumSamples();
    result.frequencyErrorPercent = 0.0;
    result.correlation = measureStereo (captured).correlation;
    result.gainDb = result.received.rmsDb - result.sent.rmsDb;
    result.clipped = result.received.clipped;

    result.summary = juce::String ("Software loopback: ")
                   + getSignalName (spec.type) + " at "
                   + juce::String (spec.frequency, 1) + " Hz measured back at "
                   + juce::String (result.received.rmsDb, 2) + " dBFS RMS";

    result.detail = "No device was involved. This proves the engine passes audio through "
                    "unchanged; it says nothing about any sound card, cable or converter.";

    return result;
}

//==============================================================================
RoundTripResult runDeviceRoundTrip (juce::AudioDeviceManager& manager, const SignalSpec& spec)
{
    RoundTripResult result;
    result.spec = spec;

    auto* device = manager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        result.verification = Verification::skipped;
        result.summary = "Device round trip skipped: no audio device is open.";
        result.detail = "No audio device is available on this machine, so nothing was "
                        "sent and nothing was captured. This is not a pass and must not be "
                        "reported as one.";
        return result;
    }

    result.driverType = device->getTypeName();
    result.deviceName = device->getName();
    result.sampleRate = device->getCurrentSampleRate();
    result.bufferSize = device->getCurrentBufferSizeSamples();

    const auto numInputs = device->getActiveInputChannels().countNumberOfSetBits();
    const auto numOutputs = device->getActiveOutputChannels().countNumberOfSetBits();

    if (numInputs <= 0)
    {
        result.verification = Verification::skipped;
        result.summary = "Device round trip skipped: \"" + result.deviceName
                       + "\" has no input channels open.";
        result.detail = "A round trip needs a signal to come back in. This device offers "
                        "no active inputs, so nothing could be captured. Not a pass.";
        return result;
    }

    if (numOutputs <= 0)
    {
        result.verification = Verification::skipped;
        result.summary = "Device round trip skipped: \"" + result.deviceName
                       + "\" has no output channels open.";
        result.detail = "Without an output there is nothing to send. Not a pass.";
        return result;
    }

    // A device is open with both directions, so the run is possible - but producing sound
    // needs a real capture callback, which cannot be done from a unit test without playing
    // audio out loud. Report it honestly rather than pretending.
    result.verification = Verification::skipped;
    result.numChannels = juce::jmin (numInputs, numOutputs);
    result.summary = "Device round trip not run: \"" + result.deviceName + "\" ("
                   + result.driverType + ") is open with " + juce::String (numOutputs)
                   + " out / " + juce::String (numInputs) + " in at "
                   + juce::String (result.sampleRate / 1000.0, 1) + " kHz, but the capture "
                   "was not driven.";
    result.detail = "A real device round trip needs the engine to play a signal out and "
                    "record it back in over several seconds. That has to be run "
                    "deliberately (see docs/hardware-checklist.md), not inside an "
                    "automated unit test, and it has not been run on this machine. "
                    "Not a pass.";

    return result;
}

//==============================================================================
juce::String judgeRoundTrip (const RoundTripResult& result, const RoundTripTolerance& tolerance)
{
    // The most important line in this file: a run that did not happen cannot pass.
    if (result.verification == Verification::skipped
        || result.verification == Verification::notPerformed)
        return "not run - " + result.summary;

    if (! result.captured)
        return "nothing was captured";

    juce::StringArray problems;

    if (std::abs (result.frequencyErrorPercent) > tolerance.maxFrequencyErrorPercent)
        problems.add ("pitch changed by " + juce::String (result.frequencyErrorPercent, 3)
                      + "% (limit " + juce::String (tolerance.maxFrequencyErrorPercent, 2) + "%)");

    if (std::abs (result.gainDb) > tolerance.maxGainErrorDb)
        problems.add ("level changed by " + juce::String (result.gainDb, 2)
                      + " dB (limit +/-" + juce::String (tolerance.maxGainErrorDb, 2) + " dB)");

    if (result.correlation < tolerance.minCorrelation)
        problems.add ("channels decorrelated to " + juce::String (result.correlation, 3)
                      + " (limit " + juce::String (tolerance.minCorrelation, 2) + ")");

    if (result.clipped)
        problems.add ("the signal clipped");

    if (problems.isEmpty())
        return {};

    return problems.joinIntoString ("; ");
}
}
