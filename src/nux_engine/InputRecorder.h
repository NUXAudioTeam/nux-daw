/* NUX DAW - recording the audio input into a clip.

   The recorder is deliberately split in two halves so the audio thread never
   allocates, never locks and never touches the disk:

   * pushInput() runs on the audio thread. It copies the incoming samples into a
     pre-allocated ring buffer and returns immediately.
   * drainToFile() runs on the message thread (or a timer). It moves whatever has
     been captured into a WAV file and tracks the take's length.

   Recording starts at the transport position, so the finished clip can be placed
   on the timeline where it was actually played. */
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

    /** Prepares the ring buffer. Safe to call whenever the device changes. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** Starts a take. Creates the file; returns false and fills `error` on failure.
        Message thread only. */
    bool start (const juce::File& file, double startPositionSteps, juce::String& error);

    /** Stops the take and finishes the file. Message thread only. */
    void stop();

    bool isRecording() const noexcept { return recording.load (std::memory_order_acquire); }

    /** Number of samples captured so far. */
    juce::int64 getRecordedSamples() const noexcept { return samplesWritten.load (std::memory_order_relaxed); }

    /** Length of the current (or last) take in seconds. */
    double getRecordedSeconds() const noexcept;

    /** The transport position the take started at, in steps. */
    double getStartPositionSteps() const noexcept { return startSteps.load (std::memory_order_relaxed); }

    /** The file the take is being written to (empty when never recorded). */
    juce::File getRecordingFile() const;

    /** True when the input is silent, so the UI can warn that nothing will be
        captured. Updated on every pushInput call. */
    float getInputLevel() const noexcept { return inputLevel.load (std::memory_order_relaxed); }

    /** Audio thread: appends the input to the ring buffer. Never allocates. */
    void pushInput (const float* const* inputChannels, int numInputChannels, int numSamples);

    /** Message thread: writes buffered samples to the file. Call regularly. */
    void drainToFile();

    /** Drops everything without writing (failed take). */
    void abort();

private:
    void closeFile();

    static constexpr int ringSize = 1 << 18;   // ~6 s at 44.1 kHz, plenty of slack

    juce::AudioBuffer<float> ring { 2, ringSize };
    std::atomic<int> writeIndex { 0 };
    std::atomic<int> readIndex { 0 };
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> samplesWritten { 0 };
    std::atomic<double> startSteps { 0.0 };
    std::atomic<float> inputLevel { 0.0f };

    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::File currentFile;
    double sampleRate = 44100.0;
    int channels = 2;
    int preparedBlockSize = 512;
    juce::AudioBuffer<float> drainBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputRecorder)
};
}
