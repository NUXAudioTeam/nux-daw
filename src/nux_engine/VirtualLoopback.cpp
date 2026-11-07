#include "VirtualLoopback.h"
#include "Measurements.h"
#include "TestSignals.h"

namespace nux::lab
{
namespace
{
/** Plays a pre-generated signal and copies whatever arrives back into a preallocated
    buffer. Nothing here allocates, locks, logs or reads a file: the capture buffer is
    sized when the callback is built, and a full buffer simply drops further input. */
class LoopbackCapture final : public juce::AudioIODeviceCallback
{
public:
    LoopbackCapture (const juce::AudioBuffer<float>& tone, int samplesToKeep)
        : playback (tone),
          capture (juce::jmax (2, tone.getNumChannels()), juce::jmax (1, samplesToKeep))
    {
        capture.clear();
    }

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        const juce::ScopedNoDenormals noDenormals;

        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            auto* out = outputChannelData[channel];

            if (out == nullptr)
                continue;

            const auto* source = playback.getReadPointer (juce::jmin (channel,
                                                                      playback.getNumChannels() - 1));
            const auto toneLength = playback.getNumSamples();

            for (int i = 0; i < numSamples; ++i)
                out[i] = source[(playhead + i) % toneLength];
        }

        const auto free = capture.getNumSamples() - writePosition;

        if (free > 0)
        {
            const auto toCopy = juce::jmin (free, numSamples);
            const auto channels = juce::jmin (numInputChannels, capture.getNumChannels());

            for (int channel = 0; channel < channels; ++channel)
                if (inputChannelData[channel] != nullptr)
                    juce::FloatVectorOperations::copy (capture.getWritePointer (channel, writePosition),
                                                       inputChannelData[channel], toCopy);

            writePosition += toCopy;
        }

        playhead = (playhead + numSamples) % juce::jmax (1, playback.getNumSamples());
        ++blocksRendered;
    }

    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}

    void audioDeviceStopped() override {}

    juce::AudioBuffer<float> capture;
    std::atomic<int> writePosition { 0 };
    std::atomic<int> blocksRendered { 0 };

private:
    juce::AudioBuffer<float> playback;
    juce::int64 playhead = 0;
};

/** Drops the leading silence so the measurement starts at the signal, not at the
    silence before the device started. Returns -1 when nothing was captured at all. */
int findFirstSignalSample (const juce::AudioBuffer<float>& buffer, float threshold)
{
    const auto numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            if (std::abs (buffer.getSample (channel, i)) > threshold)
                return i;

    return -1;
}
}

LoopbackOutcome runDeviceLoopback (const LoopbackRequest& request)
{
    juce::AudioDeviceManager manager;
    return runDeviceLoopback (request, listDeviceOptions (manager));
}

LoopbackOutcome runDeviceLoopback (const LoopbackRequest& request,
                                   const std::vector<DeviceOption>& options)
{
    LoopbackOutcome outcome;
    auto& result = outcome.result;
    result.spec = request.spec;

    const auto selection = makeSelection (request.choice, options, true);

    if (! selection.valid)
    {
        result.verification = Verification::skipped;
        result.summary = "Device round trip skipped: " + selection.error;
        result.detail = selection.explanation;
        outcome.error = selection.error;
        return outcome;
    }

    result.driverType = request.choice.typeName;
    result.deviceName = request.choice.outputName + " -> " + request.choice.inputName;
    result.sampleRate = request.sampleRate;
    result.bufferSize = request.bufferSize;

    // The tone is built before the device opens, so the callback never allocates.
    auto spec = request.spec;
    spec.sampleRate = request.sampleRate;
    spec.numChannels = 2;
    spec.durationSeconds = juce::jmax (0.25, request.seconds);

    const auto tone = generateSignal (spec);
    const auto samplesToKeep = (int) (request.sampleRate * (request.seconds + 1.0));

    juce::AudioDeviceManager manager;
    juce::AudioDeviceManager::AudioDeviceSetup setup = selection.setup;
    setup.sampleRate = request.sampleRate;
    setup.bufferSize = request.bufferSize;

    const auto deviceError = manager.initialise (2, 2, nullptr, false,
                                                 request.choice.typeName, &setup);

    if (deviceError.isNotEmpty() || manager.getCurrentAudioDevice() == nullptr)
    {
        outcome.deviceError = deviceError;

        result.verification = Verification::skipped;
        result.summary = "Device round trip skipped: the driver refused to open \""
                       + request.choice.outputName + "\".";
        result.detail = (deviceError.isNotEmpty() ? "The driver said: \"" + deviceError + "\". "
                                                  : juce::String())
                      + "Nothing was played and nothing was captured, so this is not a pass. "
                        "If the device exists but is busy, close whatever else is using it.";
        outcome.error = deviceError.isNotEmpty() ? deviceError
                                                 : "the driver opened no device at all";
        return outcome;
    }

    auto* device = manager.getCurrentAudioDevice();

    if (device->getActiveInputChannels().countNumberOfSetBits() <= 0)
    {
        manager.closeAudioDevice();

        result.verification = Verification::skipped;
        result.summary = "Device round trip skipped: \"" + device->getName()
                       + "\" opened with no input channels.";
        result.detail = "A round trip needs a return path. This device accepted playback but "
                        "gave no capture channels, so nothing could be measured. Not a pass.";
        outcome.error = "the device opened with no input channels";
        return outcome;
    }

    result.sampleRate = device->getCurrentSampleRate();
    result.bufferSize = device->getCurrentBufferSizeSamples();

    LoopbackCapture capture (tone, samplesToKeep);
    manager.addAudioCallback (&capture);

    // Let the device run long enough to play the tone and capture it. Sleeping in small
    // steps keeps the caller responsive enough to cancel by closing the app.
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) ((request.seconds + 1.0) * 1000.0);

    while (juce::Time::getMillisecondCounter() < deadline)
    {
        juce::Thread::sleep (50);

        if (capture.writePosition.load() >= samplesToKeep)
            break;
    }

    manager.removeAudioCallback (&capture);
    manager.closeAudioDevice();

    outcome.ran = true;
    result.numSamplesCaptured = capture.writePosition.load();

    if (result.numSamplesCaptured <= 0)
    {
        result.verification = Verification::skipped;
        result.summary = "Device round trip ran but captured nothing at all.";
        result.detail = "The device stayed open for the full run without delivering a single "
                        "capture buffer. That is a failed run, not a skip, and certainly not "
                        "a pass.";
        outcome.error = "no capture buffers arrived";
        return outcome;
    }

    const auto firstSignal = findFirstSignalSample (capture.capture, 1.0e-4f);

    if (firstSignal < 0)
    {
        result.verification = selection.usesVirtualCable ? Verification::virtualDevice
                                                         : Verification::physicalDevice;
        result.captured = true;
        result.received = measureLevels (capture.capture, 0);
        result.summary = "Round trip FAILED: audio was played and captured, but what came back "
                         "was silence.";
        result.detail = "Every captured sample was below -80 dBFS. If a virtual cable is "
                        "selected, check that the cable's playback side is the device NUX "
                        "wrote to and its recording side is the device NUX listened on.";
        outcome.verdict = judgeRoundTrip (result, RoundTripTolerance {});
        return outcome;
    }

    const auto available = result.numSamplesCaptured - firstSignal;
    const auto measureLength = juce::jmin (available, (int) (result.sampleRate * 0.5));

    juce::AudioBuffer<float> received (capture.capture.getNumChannels(), measureLength);

    for (int channel = 0; channel < received.getNumChannels(); ++channel)
        received.copyFrom (channel, 0, capture.capture, channel, firstSignal, measureLength);

    result.captured = true;
    result.verification = selection.usesVirtualCable ? Verification::virtualDevice
                                                     : Verification::physicalDevice;
    result.numChannels = received.getNumChannels();
    result.sent = measureLevels (tone, 0);
    result.received = measureLevels (received, 0);
    result.clipped = result.received.clipped;
    result.gainDb = result.received.peakDb - result.sent.peakDb;

    // The capture is trimmed to the signal, so a spectral peak here is the tone itself.
    const auto measuredFrequency = measureFrequencyAccurate (received, 0, result.sampleRate);

    if (measuredFrequency > 0.0 && request.spec.frequency > 0.0)
        result.frequencyErrorPercent = std::abs (measuredFrequency - request.spec.frequency)
                                     / request.spec.frequency * 100.0;

    const auto carrier = selection.usesVirtualCable
                           ? juce::String ("a virtual cable")
                           : juce::String ("real hardware");

    result.summary = "Round trip completed through " + carrier + ": "
                   + juce::String (result.received.peakDb, 1) + " dBFS peak came back, "
                   + juce::String (result.frequencyErrorPercent, 3) + "% frequency error.";
    result.detail = selection.explanation;

    outcome.verdict = judgeRoundTrip (result, RoundTripTolerance {});
    return outcome;
}
}
