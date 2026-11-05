/* NUX DAW - audio file loading and caching for audio clips.

   Audio clips reference a file inside the project's `media/` folder. Files are
   decoded once into RAM (as 44.1 kHz / device-rate float buffers) and shared
   between every clip that uses them, so a clip can be played, duplicated or
   re-triggered without touching the disk from the audio thread.

   The audio thread only ever reads SampleBuffer objects that are already
   prepared; all decoding and resampling happens on the message thread. */
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <memory>
#include <vector>

namespace nux
{
/** A decoded audio file, safe to read from the audio thread once prepared. */
struct SampleBuffer
{
    juce::AudioBuffer<float> audio;
    double sourceSampleRate = 44100.0;

    /** Fades applied to avoid clicks at the clip boundaries. */
    int fadeInSamples = 0;
    int fadeOutSamples = 0;

    bool isEmpty() const noexcept { return audio.getNumSamples() == 0; }
    int getNumSamples() const noexcept { return audio.getNumSamples(); }
    int getNumChannels() const noexcept { return audio.getNumChannels(); }
    double getLengthSeconds() const noexcept
    {
        return sourceSampleRate > 0.0 ? (double) audio.getNumSamples() / sourceSampleRate : 0.0;
    }
};

class AudioClipStore final
{
public:
    AudioClipStore();
    ~AudioClipStore() = default;

    /** Loads (or returns the cached) buffer for an absolute path. Returns nullptr on
        failure and fills `error`. Never call this from the audio thread. */
    std::shared_ptr<const SampleBuffer> load (const juce::File& file, juce::String& error);

    /** Returns a previously loaded buffer, or nullptr. Reads a lock-free snapshot. */
    std::shared_ptr<const SampleBuffer> find (const juce::String& path) const;

    /** Drops entries that no project clip refers to any more. */
    void prune (const juce::StringArray& keepPaths);

    void clear();

    int getNumLoaded() const noexcept { return (int) buffers.size(); }
    juce::int64 getMemoryUsageBytes() const noexcept;

private:
    std::shared_ptr<const SampleBuffer> decode (const juce::File& file, juce::String& error);

    juce::AudioFormatManager formatManager;
    std::map<juce::String, std::shared_ptr<const SampleBuffer>> buffers;
    juce::CriticalSection loadLock;
};
}
