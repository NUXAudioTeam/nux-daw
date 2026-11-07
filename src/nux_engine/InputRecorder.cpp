#include "InputRecorder.h"

#include <cstring>
#include <thread>

namespace nux
{
InputRecorder::~InputRecorder()
{
    stop();
}

void InputRecorder::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    const auto rate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    const auto channelCount = juce::jlimit (1, 2, numChannels);
    const auto blockSize = juce::jmax (32, maxBlockSize);

    // While a take is running the audio thread may be inside pushInput, writing into
    // `ring`. Resizing it here would be a data race and would lose whatever was buffered,
    // so the new device configuration is remembered and applied when the next take starts.
    if (recording.load (std::memory_order_acquire))
    {
        sampleRate = rate;
        channels = channelCount;
        preparedBlockSize = blockSize;
        return;
    }

    waitForAudioThreadToLeave();

    sampleRate = rate;
    channels = channelCount;
    preparedBlockSize = blockSize;

    ring.setSize (channels, ringSize, false, true, true);
    ring.clear();
    cacheRingPointers();
    drainBuffer.setSize (channels, preparedBlockSize, false, true, true);

    writeIndex.store (0, std::memory_order_relaxed);
    readIndex.store (0, std::memory_order_relaxed);
}

//==============================================================================
bool InputRecorder::start (const juce::File& file, double startPositionSteps, int trackIndex,
                           juce::String& error)
{
    error.clear();
    lastError.clear();

    if (recording.load (std::memory_order_acquire))
    {
        error = "Already recording.";
        return false;
    }

    // A previous take's audio thread may still be finishing a callback.
    waitForAudioThreadToLeave();

    // Apply any device configuration that arrived while the last take was running.
    if (ring.getNumChannels() != channels)
    {
        ring.setSize (channels, ringSize, false, true, true);
        cacheRingPointers();
    }

    if (drainBuffer.getNumSamples() < preparedBlockSize || drainBuffer.getNumChannels() != channels)
        drainBuffer.setSize (channels, preparedBlockSize, false, true, true);

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());

    if (stream == nullptr || ! stream->openedOk())
    {
        error = lastError = "Could not create the recording file: " + file.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wav;
    auto* raw = wav.createWriterFor (stream.get(), sampleRate, channels, 24, {}, 0);

    if (raw == nullptr)
    {
        error = lastError = "Could not create a WAV writer for the recording.";
        return false;   // `stream` still owns and closes the file
    }

    stream.release();   // the writer owns the stream from here on
    writer.reset (raw);

    currentFile = file;
    takeSampleRate.store (sampleRate, std::memory_order_relaxed);
    takeChannels = channels;
    startSteps.store (startPositionSteps, std::memory_order_relaxed);
    startTrackIndex.store (trackIndex, std::memory_order_relaxed);
    samplesWritten.store (0, std::memory_order_relaxed);
    droppedSamples.store (0, std::memory_order_relaxed);
    writeFailed = false;
    failWritesForTests = false;

    // Clear anything left from a previous take before the audio thread starts.
    writeIndex.store (0, std::memory_order_relaxed);
    readIndex.store (0, std::memory_order_relaxed);
    ring.clear();

    recording.store (true, std::memory_order_release);
    return true;
}

bool InputRecorder::stop()
{
    if (! recording.exchange (false, std::memory_order_acq_rel))
    {
        // Not recording. Still close a writer left open by a failed take.
        if (writer != nullptr)
            return closeFile (true);

        return true;
    }

    // The audio thread may be part-way through a callback that already saw
    // recording == true. Wait for it to leave before draining, or its last block would be
    // published after the drain and silently lost.
    waitForAudioThreadToLeave();

    drainToFile();
    return closeFile (true);
}

void InputRecorder::abort()
{
    recording.store (false, std::memory_order_release);
    waitForAudioThreadToLeave();

    // Do not keep a half-written file around: the caller asked for it to go away.
    if (writer != nullptr)
        closeFile (false);

    currentFile.deleteFile();
    samplesWritten.store (0, std::memory_order_relaxed);
    droppedSamples.store (0, std::memory_order_relaxed);
    writeIndex.store (0, std::memory_order_relaxed);
    readIndex.store (0, std::memory_order_relaxed);
}

void InputRecorder::cacheRingPointers()
{
    const auto numChannels = juce::jlimit (1, 2, ring.getNumChannels());

    for (int ch = 0; ch < numChannels; ++ch)
    {
        ringChannels[ch] = ring.getWritePointer (ch);
        ringReadChannels[ch] = ring.getReadPointer (ch);
    }
}

void InputRecorder::waitForAudioThreadToLeave()
{
    // Bounded: a hung callback must not hang the message thread forever. In practice this
    // exits on the first or second iteration.
    for (int spin = 0; spin < maxWaitSpins; ++spin)
    {
        if (callbacksInFlight.load (std::memory_order_acquire) == 0)
            return;

        std::this_thread::yield();
    }
}

bool InputRecorder::closeFile (bool verifyContents)
{
    if (writer == nullptr)
        return ! writeFailed && lastError.isEmpty();

    const auto expectedSamples = samplesWritten.load (std::memory_order_relaxed);
    const auto file = currentFile;

    // flush() returns false when the format writer could not write. WavAudioFormatWriter
    // writes its header from its destructor, so the file is not valid until reset() has
    // run - and reset() cannot report failure, which is why the file is read back below.
    const auto flushed = writer->flush();
    writer.reset();

    if (! flushed || writeFailed)
    {
        lastError = "The recording could not be written to disk. The file is incomplete.";
        return false;
    }

    if (! verifyContents)
        return true;

    // Read the file back: a WAV whose header was never finalised (a crash, a full disk, a
    // writer that died) opens with zero length. Reporting that as a saved take is how a
    // recording disappears without anyone noticing.
    juce::WavAudioFormat wav;

    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(),
                                                                        true));

    if (reader == nullptr)
    {
        lastError = "The recording file could not be read back after finalising.";
        return false;
    }

    if (reader->lengthInSamples != expectedSamples)
    {
        lastError = "The recording file holds " + juce::String (reader->lengthInSamples)
                      + " samples instead of " + juce::String (expectedSamples)
                      + ". The take is incomplete.";
        return false;
    }

    if (reader->sampleRate <= 0.0 || reader->numChannels <= 0)
    {
        lastError = "The recording file header is invalid.";
        return false;
    }

    return true;
}

double InputRecorder::getRecordedSeconds() const noexcept
{
    const auto rate = takeSampleRate.load (std::memory_order_relaxed);
    return (double) samplesWritten.load (std::memory_order_relaxed) / juce::jmax (1.0, rate);
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

    // Announce that this callback is touching the buffer, then re-check: stop() may have
    // flipped the flag between the load above and this increment.
    callbacksInFlight.fetch_add (1, std::memory_order_acq_rel);

    if (! recording.load (std::memory_order_acquire))
    {
        callbacksInFlight.fetch_sub (1, std::memory_order_acq_rel);
        return;
    }

    const auto write = writeIndex.load (std::memory_order_relaxed);
    const auto read = readIndex.load (std::memory_order_acquire);

    // One slot is always left empty so "full" and "empty" cannot look the same.
    const auto used = (write - read + ringSize) % ringSize;
    const auto space = ringSize - 1 - used;
    const auto toWrite = juce::jmin (numSamples, juce::jmax (0, space));

    if (toWrite < numSamples)
    {
        // The file writer fell behind. Drop the newest samples (keeping what is already
        // buffered is more useful than keeping what arrived too late) and count them, so
        // the UI can say the take has gaps instead of pretending it does not.
        droppedSamples.fetch_add (numSamples - toWrite, std::memory_order_relaxed);
    }

    float peak = 0.0f;

    for (int ch = 0; ch < takeChannels; ++ch)
    {
        // Mono sources are copied to both ring channels so the take is always stereo.
        const auto sourceChannel = (numInputChannels > 0) ? juce::jmin (ch, numInputChannels - 1) : -1;
        const float* source = (sourceChannel >= 0 && inputChannels != nullptr)
                                 ? inputChannels[sourceChannel]
                                 : nullptr;

        auto* destination = ringChannels[ch];

        for (int i = 0; i < toWrite; ++i)
        {
            const auto value = source != nullptr ? source[i] : 0.0f;
            destination[(write + i) % ringSize] = value;
            peak = juce::jmax (peak, std::abs (value));
        }
    }

    writeIndex.store ((write + toWrite) % ringSize, std::memory_order_release);

    // Publish the peak so the UI can show whether the input is alive.
    inputLevel.store (juce::jmax (peak, inputLevel.load (std::memory_order_relaxed) * 0.9f),
                      std::memory_order_relaxed);

    callbacksInFlight.fetch_sub (1, std::memory_order_acq_rel);
}

void InputRecorder::drainToFile()
{
    if (writer == nullptr || writeFailed)
        return;

    auto read = readIndex.load (std::memory_order_relaxed);
    const auto write = writeIndex.load (std::memory_order_acquire);

    // Samples captured, wrapping the ring.
    auto available = (write - read + ringSize) % ringSize;

    if (available <= 0)
        return;

    const auto bufferCapacity = juce::jmax (1, drainBuffer.getNumSamples());

    while (available > 0)
    {
        auto contiguous = juce::jmin (available, bufferCapacity);
        contiguous = juce::jmin (contiguous, ringSize - read);

        // The buffers can only shrink while a take is not running, but a late drain after
        // a device change must not walk off the end of either buffer.
        const auto drainChannels = juce::jmin (takeChannels, ring.getNumChannels(),
                                              drainBuffer.getNumChannels());

        for (int ch = 0; ch < drainChannels; ++ch)
            std::memcpy (drainBuffer.getWritePointer (ch), ringReadChannels[ch] + read,
                         (size_t) contiguous * sizeof (float));

        if (failWritesForTests || ! writer->writeFromAudioSampleBuffer (drainBuffer, 0, contiguous))
        {
            // Keep the unread audio in the ring: if the caller retries the write it is still
            // there, and the take is marked as failed rather than quietly shortened.
            writeFailed = true;
            break;
        }

        read = (read + contiguous) % ringSize;
        available -= contiguous;
        samplesWritten.fetch_add (contiguous, std::memory_order_relaxed);
    }

    readIndex.store (read, std::memory_order_release);
}
}
