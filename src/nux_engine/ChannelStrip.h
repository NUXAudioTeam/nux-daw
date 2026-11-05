/* NUX DAW - per-track / per-bus / master processing chain.

   Signal flow for every strip:
     input -> insert chain (EQ, compressor, delay, reverb) -> fader -> pan -> mute

   The effects are real DSP (SVF biquad EQ, feed-forward compressor, feedback
   delay, FDN reverb) rather than placeholders. When an insert's type changes the
   processor is reconfigured and the block is crossfaded from the dry signal, so
   switching effects can never leak stale filter state into the output. */
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>

#include "../nux_core/TrackModel.h"

namespace nux
{
/** Parameters shared between the UI thread and the audio thread. */
struct StripParameters
{
    std::atomic<float> volumeDb { 0.0f };
    std::atomic<float> pan      { 0.0f };
    std::atomic<bool>  muted    { false };

    /** Slot types are published atomically; the full slot data is read by the
        audio thread from `inserts` and is only rewritten while the engine is idle
        (an audio callback is never running while the UI edits a slot). */
    std::array<InsertSlot, constants::maxTrackInserts> inserts {};
    std::array<std::atomic<int>, constants::maxTrackInserts> insertTypes {};

    void setInsert (int index, InsertType newType);
    void setInsertParams (int index, float p1, float p2, float p3);
    InsertType getInsertType (int index) const;
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
    void process (juce::AudioBuffer<float>& buffer, const InsertSlot& slot, int numSamples);

private:
    void configureFor (InsertType newType, const InsertSlot& slot);
    void processEq (juce::AudioBuffer<float>& buffer, const InsertSlot& slot);
    void processCompressor (juce::AudioBuffer<float>& buffer, const InsertSlot& slot);
    void processDelay (juce::AudioBuffer<float>& buffer, const InsertSlot& slot);
    void processReverb (juce::AudioBuffer<float>& buffer, const InsertSlot& slot);

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

    /** Announces the block length for the buffers that follow. Must be called once
        per audio block before any process() call. */
    void beginBlock (int numSamples) noexcept
    {
        currentBlockSamples = juce::jlimit (0, blockSize, numSamples);
    }

    int getCurrentBlockSamples() const noexcept { return currentBlockSamples; }

    /** Clears the buffer and runs the strip over it. */
    void process (juce::AudioBuffer<float>& buffer, float inputGain = 1.0f);

    StripParameters& getParameters() noexcept { return params; }
    const StripParameters& getParameters() const noexcept { return params; }

    float getPeakLevel (int channel) const noexcept
    {
        return peakLevel[(size_t) juce::jlimit (0, 1, channel)].load (std::memory_order_relaxed);
    }

    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept { return blockSize; }

protected:
    void processInserts (juce::AudioBuffer<float>& buffer);
    void applyFaderPanMute (juce::AudioBuffer<float>& buffer);

    StripParameters params;

protected:
    void updateMeters (const juce::AudioBuffer<float>& buffer);

    double sampleRate = 44100.0;
    int blockSize = 512;
    int channels = 2;

    std::array<InsertProcessor, constants::maxTrackInserts> inserts;
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
