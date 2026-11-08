#include "ChannelStrip.h"

namespace nux
{
//==============================================================================
// StripParameters
//==============================================================================
void StripParameters::setInsert (int index, InsertType newType)
{
    if (! juce::isPositiveAndBelow (index, constants::maxTrackInserts))
        return;

    inserts[(size_t) index].type = newType;
    insertTypes[(size_t) index].store ((int) newType, std::memory_order_release);
}

void StripParameters::setInsertParams (int index, float p1, float p2, float p3)
{
    if (! juce::isPositiveAndBelow (index, constants::maxTrackInserts))
        return;

    auto& slot = inserts[(size_t) index];
    slot.p1 = p1;
    slot.p2 = p2;
    slot.p3 = p3;
}

InsertType StripParameters::getInsertType (int index) const
{
    if (! juce::isPositiveAndBelow (index, constants::maxTrackInserts))
        return InsertType::none;

    return (InsertType) insertTypes[(size_t) index].load (std::memory_order_acquire);
}

void StripParameters::loadFrom (const TrackState& track)
{
    volumeDb.store (track.volumeDb);
    pan.store (track.pan);
    muted.store (track.muted);

    for (int i = 0; i < constants::maxTrackInserts; ++i)
    {
        inserts[(size_t) i] = track.inserts[(size_t) i];
        insertTypes[(size_t) i].store ((int) track.inserts[(size_t) i].type, std::memory_order_release);
    }
}

void StripParameters::copyTo (TrackState& track) const
{
    track.volumeDb = volumeDb.load();
    track.pan      = pan.load();
    track.muted    = muted.load();

    for (int i = 0; i < constants::maxTrackInserts; ++i)
        track.inserts[(size_t) i] = inserts[(size_t) i];
}

void StripParameters::clearInserts()
{
    for (int i = 0; i < constants::maxTrackInserts; ++i)
    {
        inserts[(size_t) i] = {};
        insertTypes[(size_t) i].store ((int) InsertType::none, std::memory_order_release);
    }
}

//==============================================================================
// InsertProcessor
//==============================================================================
void InsertProcessor::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    blockSize  = juce::jmax (32, maxBlockSize);
    channels   = juce::jlimit (1, 2, numChannels);

    dryCopy.setSize (channels, blockSize, false, true, true);
    wetCopy.setSize (channels, blockSize, false, true, true);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, (juce::uint32) channels };

    lowShelf.prepare (spec);
    midPeak.prepare (spec);
    highShelf.prepare (spec);
    lowShelf.coefficients     = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sampleRate, 220.0f, 0.7f, 1.0f);
    midPeak.coefficients      = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, 1000.0f, 1.0f, 1.0f);
    highShelf.coefficients    = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 6500.0f, 0.7f, 1.0f);

    compressor.prepare (spec);
    lastCompThreshold = 999.0f;

    delayLine.setMaximumDelayInSamples ((int) (sampleRate * 2.0));
    delayLine.prepare (spec);
    delayDamp.prepare (spec);
    delayDamp.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 7000.0f, 0.7f);

    reverb.prepare (spec);

    reset();
}

void InsertProcessor::reset()
{
    lowShelf.reset();
    midPeak.reset();
    highShelf.reset();
    compressor.reset();
    delayLine.reset();
    delayDamp.reset();
    reverb.reset();
    currentType = InsertType::none;
    needsFadeIn = false;
    lastLowGain = lastHighGain = 999.0f;
    lastMidFreq = -1.0f;
    lastDelayMs = -1.0f;
    lastCompThreshold = 999.0f;
}

void InsertProcessor::configureFor (InsertType newType, const InsertSlot& slot)
{
    if (newType == currentType)
        return;

    currentType = newType;
    needsFadeIn = true;

    lowShelf.reset();
    midPeak.reset();
    highShelf.reset();
    compressor.reset();
    delayLine.reset();
    delayDamp.reset();
    reverb.reset();

    lastLowGain = lastHighGain = 999.0f;
    lastMidFreq = -1.0f;
    lastDelayMs = -1.0f;
    lastCompThreshold = 999.0f;

    juce::ignoreUnused (slot);
}

void InsertProcessor::process (juce::AudioBuffer<float>& buffer, const InsertSlot& slot, int samplesToProcess)
{
    configureFor (slot.type, slot);

    const auto numSamples = juce::jlimit (0, buffer.getNumSamples(), samplesToProcess);
    const auto numChannels = juce::jmin (channels, buffer.getNumChannels());
    blockSamples = numSamples;

    if (numSamples == 0 || numChannels == 0)
        return;

    if (slot.bypassed || slot.type == InsertType::none)
    {
        currentType = slot.type;
        needsFadeIn = false;
        return;
    }

    if (dryCopy.getNumSamples() < numSamples || dryCopy.getNumChannels() < numChannels)
    {
        dryCopy.setSize (numChannels, numSamples, false, true, true);
        wetCopy.setSize (numChannels, numSamples, false, true, true);
    }

    for (int ch = 0; ch < numChannels; ++ch)
        dryCopy.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    switch (slot.type)
    {
        case InsertType::eq:         processEq (buffer, slot); break;
        case InsertType::compressor: processCompressor (buffer, slot); break;
        case InsertType::delay:      processDelay (buffer, slot); break;
        case InsertType::reverb:     processReverb (buffer, slot); break;
        case InsertType::none:
        default:                     return;
    }

    if (needsFadeIn)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.applyGainRamp (ch, 0, numSamples, 0.0f, 1.0f);
        needsFadeIn = false;
    }
    else
    {
        // Keep the extra headroom that insert processing can introduce in check.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = std::tanh (data[i] * 1.05f);
        }
    }
}

void InsertProcessor::processEq (juce::AudioBuffer<float>& buffer, const InsertSlot& slot)
{
    const auto numSamples = blockSamples;

    // p1 = low shelf gain (dB), p2 = mid frequency (Hz), p3 = high shelf gain (dB)
    const auto lowGain  = juce::jlimit (-18.0f, 18.0f, slot.p1);
    const auto midFreq  = juce::jlimit (120.0f, 12000.0f, slot.p2 > 0.0f ? slot.p2 : 1000.0f);
    const auto highGain = juce::jlimit (-18.0f, 18.0f, slot.p3);

    if (std::abs (lowGain - lastLowGain) > 0.01f)
    {
        lowShelf.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRate, 220.0f, 0.7f, dbToGain (lowGain));
        lastLowGain = lowGain;
    }

    if (std::abs (midFreq - lastMidFreq) > 1.0f || std::abs (highGain - lastHighGain) > 0.01f)
    {
        midPeak.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            sampleRate, midFreq, 1.0f, dbToGain (juce::jlimit (-9.0f, 9.0f, highGain * 0.4f)));
        lastMidFreq = midFreq;
    }

    if (std::abs (highGain - lastHighGain) > 0.01f)
    {
        highShelf.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, juce::jmin (6500.0f, (float) sampleRate * 0.4f), 0.7f, dbToGain (highGain));
        lastHighGain = highGain;
    }

    juce::dsp::AudioBlock<float> fullBlock (buffer);
    juce::dsp::AudioBlock<float> block = fullBlock.getSubBlock (0, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);

    lowShelf.process (context);
    midPeak.process (context);
    highShelf.process (context);
}

void InsertProcessor::processCompressor (juce::AudioBuffer<float>& buffer, const InsertSlot& slot)
{
    const auto numSamples = blockSamples;

    // p1 = threshold (dB), p2 = ratio, p3 = attack (ms)
    const auto threshold = juce::jlimit (-60.0f, 0.0f, slot.p1 < 0.0f ? slot.p1 : -18.0f);
    const auto ratio     = juce::jlimit (1.0f, 20.0f, slot.p2 > 1.0f ? slot.p2 : 3.0f);
    const auto attack    = juce::jlimit (0.1f, 200.0f, slot.p3 > 0.0f ? slot.p3 : 12.0f);

    if (std::abs (threshold - lastCompThreshold) > 0.05f
        || std::abs (ratio - lastCompRatio) > 0.05f
        || std::abs (attack - lastCompAttack) > 0.5f)
    {
        compressor.setThreshold (threshold);
        compressor.setRatio (ratio);
        compressor.setAttack (attack);
        compressor.setRelease (140.0f);
        lastCompThreshold = threshold;
        lastCompRatio = ratio;
        lastCompAttack = attack;
    }

    // Make-up gain compensates for the gain reduction at the threshold.
    const auto makeup = dbToGain ((1.0f - 1.0f / ratio) * std::abs (threshold) * 0.5f);

    juce::dsp::AudioBlock<float> block = juce::dsp::AudioBlock<float> (buffer).getSubBlock (0, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);
    compressor.process (context);

    buffer.applyGain (makeup);
}

void InsertProcessor::processDelay (juce::AudioBuffer<float>& buffer, const InsertSlot& slot)
{
    const auto numSamples = blockSamples;

    // p1 = delay time (ms), p2 = feedback (0..0.95), p3 = mix (0..1)
    const auto delayMs = juce::jlimit (5.0f, 1500.0f, slot.p1 > 0.0f ? slot.p1 : 350.0f);
    const auto feedback = juce::jlimit (0.0f, 0.92f, slot.p2);
    const auto mix = juce::jlimit (0.0f, 1.0f, slot.p3);

    if (std::abs (delayMs - lastDelayMs) > 0.5f || std::abs (feedback - lastDelayFb) > 0.001f)
    {
        delayLine.setDelay ((float) (delayMs * 0.001 * sampleRate));
        lastDelayMs = delayMs;
        lastDelayFb = feedback;
    }

    const auto numChannels = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        const auto* dry = dryCopy.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto delayed = delayLine.popSample (ch);
            const auto damped = delayDamp.processSample (delayed);
            delayLine.pushSample (ch, dry[i] + damped * feedback);
            data[i] = dry[i] * (1.0f - mix * 0.5f) + damped * mix;
        }
    }
}

void InsertProcessor::processReverb (juce::AudioBuffer<float>& buffer, const InsertSlot& slot)
{
    const auto numSamples = blockSamples;

    // p1 = room size (0..1), p2 = damping (0..1), p3 = mix (0..1)
    juce::Reverb::Parameters rp;
    rp.roomSize = juce::jlimit (0.0f, 1.0f, slot.p1);
    rp.damping  = juce::jlimit (0.0f, 1.0f, slot.p2 > 0.0f ? slot.p2 : 0.5f);
    rp.wetLevel = juce::jlimit (0.0f, 1.0f, slot.p3);
    rp.dryLevel = 1.0f - rp.wetLevel * 0.5f;
    rp.width    = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    juce::dsp::AudioBlock<float> block = juce::dsp::AudioBlock<float> (buffer).getSubBlock (0, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context);
}

//==============================================================================
// ChannelStrip
//==============================================================================
ChannelStrip::ChannelStrip() = default;

void ChannelStrip::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    blockSize  = juce::jmax (32, maxBlockSize);
    channels   = juce::jlimit (1, 2, numChannels);

    for (auto& insert : inserts)
        insert.prepare (sampleRate, blockSize, channels);

    smoothedGain.reset (sampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (dbToGain (params.volumeDb.load()));

    peakLevel[0].store (0.0f);
    peakLevel[1].store (0.0f);
    prepared = true;
}

void ChannelStrip::reset()
{
    for (auto& insert : inserts)
        insert.reset();

    smoothedGain.setCurrentAndTargetValue (dbToGain (params.volumeDb.load()));
    peakLevel[0].store (0.0f);
    peakLevel[1].store (0.0f);
}

void ChannelStrip::processInserts (juce::AudioBuffer<float>& buffer)
{
    for (int i = 0; i < constants::maxTrackInserts; ++i)
    {
        const auto type = params.insertTypes[(size_t) i].load (std::memory_order_acquire);

        if (type == (int) InsertType::none)
            continue;

        inserts[(size_t) i].process (buffer, params.inserts[(size_t) i], getCurrentBlockSamples());
    }
}

void ChannelStrip::applyFaderPanMute (juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = juce::jmin (channels, buffer.getNumChannels());
    const auto numSamples  = buffer.getNumSamples();

    if (params.muted.load (std::memory_order_relaxed))
    {
        buffer.clear();
        return;
    }

    smoothedGain.setTargetValue (dbToGain (params.volumeDb.load (std::memory_order_relaxed)));
    smoothedGain.applyGain (buffer, numSamples);

    if (numSamples <= 0)
        return;

    if (numChannels >= 2)
    {
        const auto pan = juce::jlimit (-1.0f, 1.0f, params.pan.load (std::memory_order_relaxed));

        if (std::abs (pan) > 0.001f)
        {
            // Constant-power pan law.
            const auto angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            buffer.applyGain (0, 0, numSamples, std::cos (angle) * juce::MathConstants<float>::sqrt2);
            buffer.applyGain (1, 0, numSamples, std::sin (angle) * juce::MathConstants<float>::sqrt2);
        }
    }
}

void ChannelStrip::updateMeters (const juce::AudioBuffer<float>& buffer)
{
    const auto numSamples = juce::jmax (0, juce::jmin (getCurrentBlockSamples(), buffer.getNumSamples()));

    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        const auto peak = buffer.getMagnitude (ch, 0, numSamples);
        auto current = peakLevel[(size_t) ch].load (std::memory_order_relaxed);

        // Fast attack, slow release so the meter is readable.
        current = peak > current ? peak : current * 0.86f;
        peakLevel[(size_t) ch].store (current, std::memory_order_relaxed);
    }
}

void ChannelStrip::process (juce::AudioBuffer<float>& buffer, float inputGain)
{
    if (! prepared || getCurrentBlockSamples() == 0)
        return;

    if (buffer.getNumSamples() < getCurrentBlockSamples())
        beginBlock (buffer.getNumSamples());

    if (! juce::approximatelyEqual (inputGain, 1.0f))
        buffer.applyGain (inputGain);

    processInserts (buffer);
    applyFaderPanMute (buffer);
    updateMeters (buffer);
}

//==============================================================================
// StereoBus
//==============================================================================
void StereoBus::prepareBus (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    blockSize  = juce::jmax (32, maxBlockSize);

    prepare (sampleRate, blockSize, 2);

    accumulator.setSize (2, blockSize, false, true, true);
    sendAccumulator.setSize (2, blockSize, false, true, true);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 2 };

    delayLine.setMaximumDelayInSamples ((int) (sampleRate * 2.0));
    delayLine.prepare (spec);
    reverb.prepare (spec);

    smoothedDelaySend.reset (sampleRate, 0.05);
    smoothedReverbSend.reset (sampleRate, 0.05);
    smoothedDelaySend.setCurrentAndTargetValue (0.0f);
    smoothedReverbSend.setCurrentAndTargetValue (0.0f);

    delaySamplesCurrent = pendingDelaySamples.load();
    busPrepared = true;
    reset();
}

void StereoBus::beginBlock (int numSamples) noexcept
{
    ChannelStrip::beginBlock (numSamples);

    const auto samples = juce::jmin (getCurrentBlockSamples(), accumulator.getNumSamples());
    accumulator.clear (0, samples);
    sendAccumulator.clear (0, samples);
}

void StereoBus::addFromTrack (const juce::AudioBuffer<float>& buffer)
{
    if (! busPrepared)
        return;

    const auto numSamples = juce::jmin (buffer.getNumSamples(), getCurrentBlockSamples());

    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        accumulator.addFrom (ch, 0, buffer, ch, 0, numSamples);
}

void StereoBus::processSends (juce::AudioBuffer<float>& buffer)
{
    const auto numSamples = juce::jmin (buffer.getNumSamples(), sendAccumulator.getNumSamples());
    const auto delayTarget = juce::jlimit (0.0f, 1.0f, delaySendLevel.load());
    const auto reverbTarget = juce::jlimit (0.0f, 1.0f, reverbSendLevel.load());

    delaySamplesCurrent = pendingDelaySamples.load();

    if (delayTarget <= 0.001f && reverbTarget <= 0.001f
        && smoothedDelaySend.getCurrentValue() <= 0.001f
        && smoothedReverbSend.getCurrentValue() <= 0.001f)
        return;

    smoothedDelaySend.setTargetValue (delayTarget);
    smoothedReverbSend.setTargetValue (reverbTarget);

    sendAccumulator.clear();

    const auto delaySamples = (float) juce::jlimit (16.0, sampleRate * 1.9, delaySamplesCurrent);
    delayLine.setDelay (delaySamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto dSend = smoothedDelaySend.getNextValue();
        const auto rSend = smoothedReverbSend.getNextValue();

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto input = buffer.getSample (ch, i);

            // Stereo delay with cross-feedback-free ping-pong style repeats.
            const auto delayed = delayLine.popSample (ch);
            delayLine.pushSample (ch, input * 0.7f + delayed * 0.45f);

            sendAccumulator.addSample (ch, i, delayed * dSend);
        }
    }

    juce::Reverb::Parameters rp;
    rp.roomSize = 0.72f;
    rp.damping  = 0.45f;
    rp.wetLevel = 1.0f;
    rp.dryLevel = 0.0f;
    rp.width    = 1.0f;
    reverb.setParameters (rp);

    juce::dsp::AudioBlock<float> sendBlock (sendAccumulator);
    juce::dsp::ProcessContextReplacing<float> sendContext (sendBlock);
    reverb.process (sendContext);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* target = buffer.getWritePointer (ch);
        const auto* wet = sendAccumulator.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
            target[i] += wet[i] * smoothedReverbSend.getCurrentValue();
    }
}

void StereoBus::processBus (MasterBus& master)
{
    if (! busPrepared)
        return;

    auto& buffer = accumulator;
    const auto numSamples = juce::jmin (getCurrentBlockSamples(), buffer.getNumSamples());

    if (numSamples == 0)
        return;

    processInserts (buffer);
    applyFaderPanMute (buffer);
    processSends (buffer);
    updateMeters (buffer);

    master.addFromBus (buffer);
}

//==============================================================================
// MasterBus
//==============================================================================
void MasterBus::prepareMaster (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    prepare (sampleRate, juce::jmax (32, maxBlockSize), 2);
    accumulator.setSize (2, juce::jmax (32, maxBlockSize), false, true, true);
    limiterGain = 1.0f;
    masterPrepared = true;
    reset();
}

void MasterBus::beginBlock (int numSamples) noexcept
{
    ChannelStrip::beginBlock (numSamples);
    accumulator.clear (0, juce::jmin (getCurrentBlockSamples(), accumulator.getNumSamples()));
}

void MasterBus::addFromBus (const juce::AudioBuffer<float>& buffer)
{
    if (! masterPrepared)
        return;

    const auto numSamples = juce::jmin (buffer.getNumSamples(), getCurrentBlockSamples());

    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        accumulator.addFrom (ch, 0, buffer, ch, 0, numSamples);
}

void MasterBus::renderMaster (juce::AudioBuffer<float>& output, juce::MidiBuffer*)
{
    if (! masterPrepared)
        return;

    auto& buffer = accumulator;
    const auto numSamples = juce::jmin (output.getNumSamples(),
                                        juce::jmin (getCurrentBlockSamples(), buffer.getNumSamples()));

    if (numSamples == 0)
        return;

    processInserts (buffer);
    applyFaderPanMute (buffer);

    if (limiterEnabled.load (std::memory_order_relaxed))
    {
        // Smooth soft-knee limiter at -0.3 dBFS.
        const auto ceiling = 0.966f;

        for (int i = 0; i < numSamples; ++i)
        {
            float peak = 0.0f;

            for (int ch = 0; ch < 2; ++ch)
                peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));

            const auto desiredGain = peak > ceiling ? ceiling / peak : 1.0f;
            limiterGain += (desiredGain - limiterGain) * (desiredGain < limiterGain ? 0.35f : 0.0015f);

            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, i, std::tanh (buffer.getSample (ch, i) * limiterGain));
        }
    }

    peakReduction.store (limiterGain);

    for (int ch = 0; ch < juce::jmin (2, output.getNumChannels()); ++ch)
        output.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    updateMeters (buffer);
}
}
