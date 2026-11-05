/* NUX DAW - plays audio clips in sync with the transport.

   Each track gets one player. The player is given an immutable snapshot of the
   track's clips (start step, length, source offset) plus shared SampleBuffers.
   On every block it converts the transport position into a sample position and
   copies the matching slice out of the cached buffer, crossfading the clip edges
   so nothing clicks.

   Everything the audio thread touches is plain data owned by the engine; the
   message thread publishes a new snapshot and the audio thread picks it up at the
   start of the next block. */
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <vector>

#include "AudioClipStore.h"

namespace nux
{
/** One playable clip, flattened for the audio thread. */
struct ClipSnapshot
{
    std::shared_ptr<const SampleBuffer> buffer;
    int startStep = 0;
    int lengthSteps = 16;
    juce::int64 sourceStartSample = 0;
    float gain = 1.0f;
};

class AudioClipPlayer
{
public:
    AudioClipPlayer() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Replaces the clip list. Called from the message thread between blocks. */
    void setClips (std::vector<ClipSnapshot> newClips);

    /** Renders the clips that fall inside this block. `stepsPerSample` comes from the
        transport; `startStep` is the transport position at the first sample. */
    void render (juce::AudioBuffer<float>& output, int numSamples,
                 double startStep, double stepsPerSample, int loopLengthSteps, bool looping);

    int getNumClips() const noexcept { return (int) clips.size(); }

private:
    void renderClip (const ClipSnapshot& clip, juce::AudioBuffer<float>& output, int numSamples,
                     double startStep, double stepsPerSample, int loopLengthSteps, bool looping);

    std::vector<ClipSnapshot> clips;
    juce::AudioBuffer<float> clipBuffer;
    double sampleRate = 44100.0;
    int blockSize = 512;
    bool prepared = false;
};
}
