/* NUX DAW - recording the audio input into a clip.

   The recorder is deliberately split in two halves so the audio thread never
   allocates, never locks and never touches the disk:

   * pushInput() runs on the audio thread. It copies the incoming samples into a
     pre-allocated ring buffer and returns immediately.
   * drainToFile() runs on the message thread (or a timer). It moves whatever has
     been captured into a WAV file and tracks the take's length.

   Recording starts at the transport position, so the finished clip can be placed
   on the timeline where it was actually played.

   Reliability rules this class keeps, each of which was a bug at some point:

   * A full ring buffer drops samples and *counts* them. It never overwrites
     unread audio silently.
   * stop() waits for the audio thread to leave pushInput() before draining, so
     the last block of a take is not lost - and start()/prepare() wait for the
     same thing before they touch the buffer, so a device change cannot resize it
     under a live callback.
   * The sample rate and channel count belong to the take, captured when it
     starts. A device or buffer-size change mid-take cannot retroactively change
     how long the finished take claims to be.
   * The track a take belongs to is captured at start, not read at stop: arming
     another track while recording must not move the take somewhere else.
   * A take is only reported as saved when the file was actually finalised and
     reads back the number of samples that were written. */
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>

namespace nux
{
class InputRecorder
{
public:
    InputRecorder() = default;
    ~InputRecorder();

    /** Prepares the ring buffer. Safe to call whenever the device changes: while a take is
        running the new configuration is deferred until the next take, because resizing the
        buffer the audio thread is writing into would lose audio. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** Starts a take. Creates the file; returns false and fills `error` on failure.
        Message thread only. `trackIndex` is the track the take will be placed on. */
    bool start (const juce::File& file, double startPositionSteps, int trackIndex,
                juce::String& error);

    /** Stops the take and finishes the file. Returns false when the file could not be
        finalised or did not read back correctly; `getLastError()` says why. Message thread
        only. */
    bool stop();

    bool isRecording() const noexcept { return recording.load (std::memory_order_acquire); }

    /** Number of samples captured so far. */
    juce::int64 getRecordedSamples() const noexcept { return samplesWritten.load (std::memory_order_relaxed); }

    /** Length of the current (or last) take in seconds.

        Uses the rate the take was started at. A device change mid-take changes the rate of
        future takes, not the length of this one. */
    double getRecordedSeconds() const noexcept;

    /** The transport position the take started at, in steps. */
    double getStartPositionSteps() const noexcept { return startSteps.load (std::memory_order_relaxed); }

    /** The track this take was started for (-1 when none). */
    int getStartTrackIndex() const noexcept { return startTrackIndex.load (std::memory_order_relaxed); }

    /** The sample rate the current (or last) take is being written at. */
    double getTakeSampleRate() const noexcept { return takeSampleRate.load (std::memory_order_relaxed); }

    /** The file the take is being written to (empty when never recorded). */
    juce::File getRecordingFile() const;

    /** True when the input is silent, so the UI can warn that nothing will
        be captured. Updated on every pushInput call. */
    float getInputLevel() const noexcept { return inputLevel.load (std::memory_order_relaxed); }

    /** Samples the audio thread had to throw away because the file writer fell behind.
        Non-zero means the take has gaps and the UI must say so rather than pretend the
        recording is complete. */
    juce::int64 getDroppedSamples() const noexcept { return droppedSamples.load (std::memory_order_relaxed); }

    /** Why the last start/stop failed. Empty after a successful one. */
    const juce::String& getLastError() const noexcept { return lastError; }

    /** Audio thread: appends the input to the ring buffer. Never allocates. */
    void pushInput (const float* const* inputChannels, int numInputChannels, int numSamples);

    /** Message thread: writes buffered samples to the file. Call regularly. */
    void drainToFile();

    /** Drops everything without writing (failed take). */
    void abort();

    /** Test hook: makes the next write attempt fail, so the "disk full" path can be
        exercised. Never set outside tests. */
    void setWriteFailureForTests (bool shouldFail) noexcept { failWritesForTests = shouldFail; }

private:
    bool closeFile (bool verifyContents);
    void waitForAudioThreadToLeave();
    void cacheRingPointers();

    static constexpr int ringSize = 1 << 18;   // ~6 s at 44.1 kHz, plenty of slack
    static constexpr int maxWaitSpins = 4000;  // ~a few ms of yielding, then give up

    juce::AudioBuffer<float> ring { 2, ringSize };

    /** Raw channel pointers into `ring`, cached when it is resized.

        The audio thread must not go through AudioBuffer's accessors: getWritePointer()
        sets the buffer's internal "cleared" flag, and the message thread's copyFrom() reads
        it - a one-byte data race that ThreadSanitizer reports and that has no business
        existing on an audio callback. */
    float* ringChannels[2] { nullptr, nullptr };
    const float* ringReadChannels[2] { nullptr, nullptr };
    std::atomic<int> writeIndex { 0 };
    std::atomic<int> readIndex { 0 };
    std::atomic<bool> recording { false };
    std::atomic<int> callbacksInFlight { 0 };
    std::atomic<juce::int64> samplesWritten { 0 };
    std::atomic<juce::int64> droppedSamples { 0 };
    std::atomic<double> startSteps { 0.0 };
    std::atomic<int> startTrackIndex { -1 };
    std::atomic<double> takeSampleRate { 44100.0 };
    std::atomic<float> inputLevel { 0.0f };

    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::File currentFile;
    juce::String lastError;

    /** Configuration of the *next* take. Updated by prepare(). */
    double sampleRate = 44100.0;
    int channels = 2;
    int preparedBlockSize = 512;

    /** Configuration of the *current* take. Frozen by start(). */
    int takeChannels = 2;

    bool failWritesForTests = false;
    bool writeFailed = false;

    juce::AudioBuffer<float> drainBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputRecorder)
};
}
