#include "InputRecorder.h"

namespace nux
{
InputRecorder::~InputRecorder()
{
    stop();
}

void InputRecorder::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    channels = juce::jlimit (1, 2, numChannels);
    preparedBlockSize = juce::jmax (32, maxBlockSize);

    ring.setSize (channels, ringSize, false, true, true);
    ring.clear();
    drainBuffer.setSize (channels, preparedBlockSize, false, true, true);

    writeIndex.store (0, std::memory_order_relaxed);
    readIndex.store (0, std::memory_order_relaxed);
}

//==============================================================================
bool InputRecorder::start (const juce::File& file, double startPositionSteps, juce::String& error)
{
    error.clear();

    if (recording.load())
    {
        error = "Already recording.";
        return false;
    }

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());

    if (stream == nullptr || ! stream->openedOk())
    {
        error = "Could not create the recording file: " + file.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wav;
    auto* raw = wav.createWriterFor (stream.get(), sampleRate, channels, 24, {}, 0);

    if (raw == nullptr)
    {
        error = "Could not create a WAV writer for the recording.";
        return false;
    }

    stream.release();   // the writer owns the stream from here on
    writer.reset (raw);

    currentFile = file;
    startSteps.store (startPositionSteps, std::memory_order_relaxed);
    samplesWritten.store (0, std::memory_order_relaxed);

    // Clear anything left from a previous take before the audio thread starts.
    writeIndex.store (0, std::memory_order_relaxed);
    readIndex.store (0, std::memory_order_relaxed);
    ring.clear();

    recording.store (true, std::memory_order_release);
    return true;
}

void InputRecorder::stop()
{
    if (! recording.load())
    {
        closeFile();
        return;
    }

    recording.store (false, std::memory_order_release);

    // Flush whatever the audio thread already captured.
    drainToFile();
    closeFile();
}

void InputRecorder::abort()
{
    recording.store (false, std::memory_order_release);
    closeFile();
    currentFile.deleteFile();
    samplesWritten.store (0, std::memory_order_relaxed);
    writeIndex.store (0, std::memory_order_relaxed);
    readIndex.store (0, std::memory_order_relaxed);
}

void InputRecorder::closeFile()
{
    if (writer != nullptr)
    {
        writer->flush();
        writer.reset();
    }
}

double InputRecorder::getRecordedSeconds() const noexcept
{
    return (double) samplesWritten.load (std::memory_order_relaxed) / juce::jmax (1.0, sampleRate);
}

juce::File InputRecorder::getRecordingFile() const
{
    return currentFile;
}

//==============================================================================
void InputRecorder::pushInput (const float* const* inputChannels, int numInputChannels, int numSamples)
{
    if (! recording.load (std::memory_order_acquire) || numSamples <= 0)
        return;

    // Mono sources are copied to both ring channels so the take is always stereo.
    float peak = 0.0f;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto sourceChannel = (numInputChannels > 0) ? juce::jmin (ch, numInputChannels - 1) : -1;
        const float* source = (sourceChannel >= 0 && inputChannels != nullptr)
                                 ? inputChannels[sourceChannel]
                                 : nullptr;

        auto* destination = ring.getWritePointer (ch);
        auto write = writeIndex.load (std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto value = source != nullptr ? source[i] : 0.0f;
            destination[(write + i) % ringSize] = value;
            peak = juce::jmax (peak, std::abs (value));
        }
    }

    writeIndex.store ((writeIndex.load (std::memory_order_relaxed) + numSamples) % ringSize,
                      std::memory_order_release);

    // Publish the peak so the UI can show whether the input is alive.
    inputLevel.store (juce::jmax (peak, inputLevel.load (std::memory_order_relaxed) * 0.9f),
                      std::memory_order_relaxed);
}

void InputRecorder::drainToFile()
{
    if (writer == nullptr)
        return;

    auto read = readIndex.load (std::memory_order_relaxed);
    const auto write = writeIndex.load (std::memory_order_acquire);

    // Samples captured, wrapping the ring.
    auto available = (write - read + ringSize) % ringSize;

    if (available <= 0)
        return;

    while (available > 0)
    {
        const auto chunk = juce::jmin (available, preparedBlockSize);
        const auto contiguous = juce::jmin (chunk, ringSize - read);

        for (int ch = 0; ch < channels; ++ch)
            drainBuffer.copyFrom (ch, 0, ring, ch, read, contiguous);

        writer->writeFromAudioSampleBuffer (drainBuffer, 0, contiguous);

        read = (read + contiguous) % ringSize;
        available -= contiguous;
        samplesWritten.fetch_add (contiguous, std::memory_order_relaxed);
    }

    readIndex.store (read, std::memory_order_release);
}
}
