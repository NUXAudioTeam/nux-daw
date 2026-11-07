/* NUX DAW - per-track / per-bus / master processing chain.

   Signal flow for every strip:
     input -> insert chain (EQ, compressor, delay, reverb, hosted plugin) -> fader -> pan -> mute

   A hosted plugin occupies an insert slot exactly like a built-in effect. When the
   slot is marked as an instrument it replaces the strip input instead of
   processing it, which is how a VST3 instrument drives its track.

   The effects are real DSP (SVF biquad EQ, feed-forward compressor, feedback
   delay, FDN reverb) rather than placeholders. When an insert's type changes the
   processor is reconfigured and the block is crossfaded from the dry signal, so
   switching effects can never leak stale filter state into the output. */
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>
#include <array>

#include "../nux_core/TrackModel.h"
#include "PluginSlot.h"

namespace nux
{
/** Everything the audio thread needs from one insert slot, as plain data.

    Deliberately a value: the audio thread copies one of these per slot per block and then
    works from the copy, so nothing it reads can change underneath it. */
struct InsertView
{
    InsertType type = InsertType::none;
    bool bypassed = false;
    float p1 = 0.0f;
    float p2 = 0.0f;
    float p3 = 0.0f;

    bool isActive() const noexcept { return type != InsertType::none && ! bypassed; }
};

/** The publish side of one insert slot.

    This replaces an earlier arrangement where the UI thread copied whole `InsertSlot`
    objects (which own a juce::String) into a buffer the audio thread was reading. That was
    two bugs wearing one coat: a data race on non-atomic members, and a possible allocation
    on the audio thread. Both are gone:

      * the model (`TrackState`) is never read by the audio thread at all, only published
        from it, and
      * the published fields are independent atomics, written with release and read with
        acquire, so the audio thread always sees a whole value rather than a torn one.

    The five fields are not published as one transaction. A block may see a new cutoff with
    the previous resonance - one block of a coefficient change, which is inaudible - but it
    can never see half of a float, which is a click. */
struct AtomicInsertState
{
    std::atomic<int>   type     { (int) InsertType::none };
    std::atomic<bool>  bypassed { false };
    std::atomic<float> p1       { 0.0f };
    std::atomic<float> p2       { 0.0f };
    std::atomic<float> p3       { 0.0f };

    /** Message thread: copies the model into the published values. */
    void publish (const InsertSlot& slot) noexcept;

    /** Audio thread: one consistent-enough snapshot, never a torn read. */
    InsertView load() const noexcept;

    void clear() noexcept;
};

/** Parameters shared between the UI thread and the audio thread. */
struct StripParameters
{
    std::atomic<float> volumeDb { 0.0f };
    std::atomic<float> pan      { 0.0f };
    std::atomic<bool>  muted    { false };

    /** The only insert state the audio thread is allowed to see. */
    std::array<AtomicInsertState, constants::maxTrackInserts> insertState {};

    void setInsert (int index, InsertType newType);
    void setInsertParams (int index, float p1, float p2, float p3);
    InsertType getInsertType (int index) const;
    InsertView getInsertView (int index) const;
    void publishInsert (int index, const InsertSlot& slot);
    void loadFrom (const TrackState& track);
    void copyTo (TrackState& track) const;
    void clearInserts();
};

/** A single insert slot with its own DSP state. */
class InsertProcessor
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Processes the buffer in place. `slot` describes the desired effect. */
    void process (juce::AudioBuffer<float>& buffer, const InsertView& slot, int numSamples);

private:
    void configureFor (InsertType newType, const InsertView& slot);
    void processEq (juce::AudioBuffer<float>& buffer, const InsertView& slot);
    void processCompressor (juce::AudioBuffer<float>& buffer, const InsertView& slot);
    void processDelay (juce::AudioBuffer<float>& buffer, const InsertView& slot);
    void processReverb (juce::AudioBuffer<float>& buffer, const InsertView& slot);

    double sampleRate = 44100.0;
    int blockSize = 512;
    int channels = 2;

    InsertType currentType = InsertType::none;
    bool needsFadeIn = false;

    juce::AudioBuffer<float> dryCopy;
    juce::AudioBuffer<float> wetCopy;

    juce::dsp::IIR::Filter<float> lowShelf, midPeak, highShelf;
    juce::dsp::IIR::Filter<float> delayDamp;
    juce::dsp::Compressor<float> compressor;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 192000 };
    juce::dsp::Reverb reverb;

    // Cached settings so coefficients are only rebuilt when a value really changes.
    float lastLowGain = 999.0f, lastMidFreq = -1.0f, lastHighGain = 999.0f;
    float lastDelayMs = -1.0f, lastDelayFb = -1.0f;
    float lastCompThreshold = 999.0f, lastCompRatio = -1.0f, lastCompAttack = -1.0f;
    int blockSamples = 0;
};

class MasterBus;

class ChannelStrip
{
public:
    ChannelStrip();
    virtual ~ChannelStrip() = default;

    void prepare (double newSampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Message thread only. Installs (or clears) the plugin used by the given insert
        slot, then marks the slot type. The engine serialises this against the audio
        callback by stopping the device first. Pass nullptr to clear the slot.

        Ownership transfers to the strip. */
    void setPluginSlot (int index, std::unique_ptr<PluginSlot> slot);

    /** Message thread only: re-prepares every hosted plugin after a sample rate or
        block size change. */
    void preparePlugins (double sampleRate, int maxBlockSize, int numChannels);

    /** Drops insert slots that are marked as plugins but hold no instance, so a
        project that references a missing plugin still plays its other effects. */
    void sanitisePluginSlotTypes();

    PluginSlot* getPluginSlot (int index) const noexcept
    {
        return juce::isPositiveAndBelow (index, constants::maxTrackInserts)
                 ? pluginSlots[(size_t) index].get()
                 : nullptr;
    }

    /** True when the strip's input is generated by a hosted instrument, so the
        caller must not render its own source material into the buffer. */
    bool hasInstrumentPlugin() const noexcept
    {
        return instrumentSlotActive.load (std::memory_order_relaxed);
    }

    /** Announces the block length for the buffers that follow. Must be called once
        per audio block before any process() call. */
    void beginBlock (int numSamples) noexcept
    {
        currentBlockSamples = juce::jlimit (0, blockSize, numSamples);
    }

    int getCurrentBlockSamples() const noexcept { return currentBlockSamples; }

    /** Runs the strip over the buffer. `midi` is forwarded to hosted plugin slots
        (instruments consume it, MIDI-aware effects pass it on). */
    void process (juce::AudioBuffer<float>& buffer, float inputGain = 1.0f,
                  juce::MidiBuffer* midi = nullptr);

    StripParameters& getParameters() noexcept { return params; }
    const StripParameters& getParameters() const noexcept { return params; }

    float getPeakLevel (int channel) const noexcept
    {
        return peakLevel[(size_t) juce::jlimit (0, 1, channel)].load (std::memory_order_relaxed);
    }

    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept { return blockSize; }

protected:
    void processInserts (juce::AudioBuffer<float>& buffer, juce::MidiBuffer* midi = nullptr);
    void applyFaderPanMute (juce::AudioBuffer<float>& buffer);

    StripParameters params;

protected:
    void updateMeters (const juce::AudioBuffer<float>& buffer);

    double sampleRate = 44100.0;
    int blockSize = 512;
    int channels = 2;

    std::array<InsertProcessor, constants::maxTrackInserts> inserts;
    std::array<std::unique_ptr<PluginSlot>, constants::maxTrackInserts> pluginSlots;
    std::atomic<bool> instrumentSlotActive { false };
    juce::SmoothedValue<float> smoothedGain { 1.0f };
    std::array<std::atomic<float>, 2> peakLevel { 0.0f, 0.0f };
    int currentBlockSamples = 0;
    bool prepared = false;
};

/** A mixer bus that carries the two built-in send effects (delay + reverb). */
class StereoBus final : public ChannelStrip
{
public:
    void prepareBus (double newSampleRate, int maxBlockSize);

    /** Clears the accumulator for a new block. */
    void beginBlock (int numSamples) noexcept;

    /** Tracks add their post-fader signal into this bus. */
    void addFromTrack (const juce::AudioBuffer<float>& buffer);

    /** Runs inserts, send effects, fader and pan, then adds into the master. */
    void processBus (MasterBus& master);

    void setSendLevels (float delaySend, float reverbSend) noexcept
    {
        delaySendLevel.store (delaySend);
        reverbSendLevel.store (reverbSend);
    }

    void setDelayTimeSamples (double samples) noexcept { pendingDelaySamples = samples; }

private:
    void processSends (juce::AudioBuffer<float>& buffer);

    juce::AudioBuffer<float> accumulator;
    juce::AudioBuffer<float> sendAccumulator;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 192000 };
    juce::dsp::Reverb reverb;
    juce::SmoothedValue<float> smoothedDelaySend { 0.0f }, smoothedReverbSend { 0.0f };
    std::atomic<float> delaySendLevel { 0.0f }, reverbSendLevel { 0.0f };
    std::atomic<double> pendingDelaySamples { 11025.0 };
    double delaySamplesCurrent = 11025.0;
    double sampleRate = 44100.0;
    int blockSize = 512;
    bool busPrepared = false;
};

/** The master strip: sums the buses, runs a soft limiter and meters the output. */
class MasterBus final : public ChannelStrip
{
public:
    void prepareMaster (double newSampleRate, int maxBlockSize);
    void beginBlock (int numSamples) noexcept;
    void addFromBus (const juce::AudioBuffer<float>& buffer);
    void renderMaster (juce::AudioBuffer<float>& output, juce::MidiBuffer* midi = nullptr);

    void setLimiterEnabled (bool shouldLimit) noexcept { limiterEnabled.store (shouldLimit); }
    bool isLimiterEnabled() const noexcept { return limiterEnabled.load(); }
    float getPeakReduction() const noexcept { return peakReduction.load(); }

private:
    juce::AudioBuffer<float> accumulator;
    std::atomic<bool> limiterEnabled { true };
    std::atomic<float> peakReduction { 1.0f };
    float limiterGain = 1.0f;
    bool masterPrepared = false;
};
}
