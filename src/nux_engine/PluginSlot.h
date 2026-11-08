/* NUX DAW - one hosted VST3 plugin inside a channel strip.

   A PluginSlot owns a live juce::AudioPluginInstance and is processed by a
   ChannelStrip, so a hosted plugin sits in the same insert chain as the
   built-in EQ / compressor / delay / reverb.

   Threading contract
   ------------------
   * The audio thread only calls processBlock() / handles the buffers it already
     owns. It never allocates and never blocks.
   * Creating and destroying an instance, opening an editor and changing the bus
     layout are message-thread operations.
   * When a slot has to be rebuilt (new plugin, new bus layout, new block size)
     the engine swaps in a *new* PluginSlot object and retires the old one after
     a short delay, so a block in flight can never be rendered with a destroyed
     instance. That is the whole reason this class is held through a
     std::unique_ptr instead of by value.
   * Plugin parameter changes arrive from the GUI through the AudioProcessor
     methods, which the VST3 wrapper already guards internally with a lock that
     the audio thread participates in. */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

namespace nux
{
class PluginRetirementQueue;

class PluginSlot
{
public:
    PluginSlot (std::unique_ptr<juce::AudioPluginInstance> instanceToUse,
                double sampleRateToUse,
                int maximumBlockSize,
                int numChannelsToUse,
                bool midiInputToUse);

    /** Legacy-ish constructor kept for the unit tests: builds an empty slot that
        only holds a name, so the surrounding machinery can be exercised without
        a real plugin binary. */
    PluginSlot (juce::String displayName, bool isInstrumentIn);

    ~PluginSlot();

    //==============================================================================
    juce::AudioPluginInstance* getInstance() const noexcept { return instance.get(); }
    juce::String getName() const { return displayName; }
    juce::String getFormatName() const { return formatName; }
    bool isInstrument() const noexcept { return instrument; }
    bool isPrepared() const noexcept { return prepared.load (std::memory_order_acquire); }

    /** True when the plugin asked for samples but got none this block. */
    int getNumAudioInputs() const noexcept { return numInputs; }
    int getNumAudioOutputs() const noexcept { return numOutputs; }

    float getPeakLevel (int channel) const noexcept
    {
        return peakLevel[(size_t) juce::jlimit (0, 1, channel)].load (std::memory_order_relaxed);
    }

    /** Clears the plugin's internal state (delay lines, tails, voices). */
    void reset();

    /** Runs the plugin over `buffer`.

        The buffer is the strip's working buffer and always has
        `max (numInputs, numOutputs, 2)` channels. When the plugin has fewer
        outputs than the buffer the extra channels are cleared; when it has more
        outputs than the buffer only the first channels are kept. Returns true if
        the buffer holds new audio. */
    bool process (juce::AudioBuffer<float>& buffer, int numSamples, juce::MidiBuffer* midi);

    /** True when this slot's output replaces the strip input (instruments). */
    bool replacesInput() const noexcept { return instrument && numOutputs > 0; }

    /** Short human readable status for the mixer strip. */
    juce::String getStatusText() const;

    /** Message thread only: re-prepare the live instance for a new sample rate or
        block size. Must not run while the audio callback is using this slot. */
    void prepareForNewSampleRate (double newSampleRate, int newBlockSize, int newChannels);

private:
    void prepareInternal (double newSampleRate, int newBlockSize, int newChannels);
    void updatePeaks (const juce::AudioBuffer<float>& buffer, int numSamples);
    void releaseInstance();

    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::String displayName { "Plugin" };
    juce::String formatName;
    bool instrument = false;

    std::atomic<bool> prepared { false };

    double sampleRate = 44100.0;
    int blockSize = 512;
    int numChannels = 2;
    int numInputs = 0;
    int numOutputs = 0;
    bool wantsMidi = false;
    bool suppressesInput = false;

    /** Channel pointer tables handed to the plugin. Sized once in prepare() so
        process() never allocates. */
    juce::AudioBuffer<float> pluginBuffer;
    juce::MidiBuffer emptyMidi;
    bool processedSomething = false;

    std::array<std::atomic<float>, 2> peakLevel { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSlot)
};
}
