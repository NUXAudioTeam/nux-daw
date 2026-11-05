#include "PluginSlot.h"

namespace nux
{
PluginSlot::PluginSlot (std::unique_ptr<juce::AudioPluginInstance> instanceToUse,
                        double sampleRateToUse,
                        int maximumBlockSize,
                        int numChannelsToUse,
                        bool midiInputToUse)
    : instance (std::move (instanceToUse)),
      wantsMidi (midiInputToUse)
{
    jassert (instance != nullptr);

    if (instance != nullptr)
    {
        displayName = instance->getName();
        formatName  = instance->getPluginDescription().pluginFormatName;
        instrument  = instance->getPluginDescription().isInstrument;

        prepareInternal (sampleRateToUse, maximumBlockSize, numChannelsToUse);
    }
}

PluginSlot::PluginSlot (juce::String displayNameIn, bool isInstrumentIn)
    : displayName (std::move (displayNameIn)),
      instrument (isInstrumentIn)
{
    // Placeholder slot with no live instance: used by the unit tests so the insert
    // chain and the UI can be exercised without a plugin binary being present.
    numInputs  = instrument ? 0 : 2;
    numOutputs = 2;
    suppressesInput = instrument;
    prepared.store (true, std::memory_order_release);
}

PluginSlot::~PluginSlot()
{
    releaseInstance();
}

void PluginSlot::releaseInstance()
{
    prepared.store (false, std::memory_order_release);

    if (instance != nullptr)
    {
        instance->releaseResources();
        instance->setRateAndBufferSizeDetails (0.0, 0);
        instance.reset();
    }
}

//==============================================================================
void PluginSlot::prepareInternal (double newSampleRate, int newBlockSize, int newChannels)
{
    juce::ignoreUnused (newChannels);

    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    blockSize  = juce::jmax (16, newBlockSize);

    numInputs  = juce::jmax (0, instance->getTotalNumInputChannels());
    numOutputs = juce::jmax (0, instance->getTotalNumOutputChannels());

    instrument      = instance->getPluginDescription().isInstrument;
    suppressesInput = instrument && numOutputs > 0;
    wantsMidi       = instance->acceptsMidi() || instrument;

    // The plugin is handed exactly as many channels as it declares: the VST3
    // wrapper maps its buses onto the buffer's channel count, so a mismatch here
    // would silently drop a bus.
    const auto pluginChannels = juce::jmax (1, numInputs, numOutputs);

    pluginBuffer.setSize (pluginChannels, blockSize, false, true, true);
    pluginBuffer.clear();

    instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
    instance->prepareToPlay (sampleRate, blockSize);

    prepared.store (true, std::memory_order_release);
}

void PluginSlot::reset()
{
    if (instance != nullptr && prepared.load (std::memory_order_acquire))
        instance->reset();

    for (auto& peak : peakLevel)
        peak.store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
bool PluginSlot::process (juce::AudioBuffer<float>& buffer, int numSamples, juce::MidiBuffer* midi)
{
    if (instance == nullptr || ! prepared.load (std::memory_order_acquire))
        return false;

    if (numSamples <= 0 || numSamples > blockSize)
        return false;

    // A plugin with no audio outputs cannot contribute anything; running it would
    // only risk writing into a buffer with no channels.
    if (numOutputs <= 0)
    {
        buffer.clear (0, numSamples);
        return true;
    }

    const auto pluginChannels = pluginBuffer.getNumChannels();

    // Load the strip audio into the plugin buffer. An instrument never receives the
    // dry track signal, so its input is silence.
    for (int ch = 0; ch < pluginChannels; ++ch)
    {
        auto* destination = pluginBuffer.getWritePointer (ch);

        if (! suppressesInput && ch < buffer.getNumChannels())
            juce::FloatVectorOperations::copy (destination, buffer.getReadPointer (ch), numSamples);
        else
            juce::FloatVectorOperations::clear (destination, numSamples);
    }

    emptyMidi.clear();

    juce::MidiBuffer& midiToUse = (midi != nullptr && wantsMidi) ? *midi : emptyMidi;

    instance->processBlock (pluginBuffer, midiToUse);

    // Publish the result. Channels the plugin did not write are cleared rather
    // than left holding the dry signal.
    const auto stripChannels = buffer.getNumChannels();

    for (int ch = 0; ch < stripChannels; ++ch)
    {
        if (ch < numOutputs)
            juce::FloatVectorOperations::copy (buffer.getWritePointer (ch),
                                               pluginBuffer.getReadPointer (ch),
                                               numSamples);
        else
            juce::FloatVectorOperations::clear (buffer.getWritePointer (ch), numSamples);
    }

    updatePeaks (pluginBuffer, numSamples);
    return true;
}

void PluginSlot::updatePeaks (const juce::AudioBuffer<float>& source, int numSamples)
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto index = juce::jmin (ch, source.getNumChannels() - 1);
        const auto value = index >= 0 ? source.getMagnitude (index, 0, numSamples) : 0.0f;

        const auto previous = peakLevel[(size_t) ch].load (std::memory_order_relaxed);
        peakLevel[(size_t) ch].store (juce::jmax (value, previous * 0.92f), std::memory_order_relaxed);
    }
}

//==============================================================================
juce::String PluginSlot::getStatusText() const
{
    if (instance == nullptr)
        return "plugin unavailable";

    if (! prepared.load (std::memory_order_acquire))
        return "preparing";

    juce::String text = displayName;

    if (numOutputs == 0)
        text << "  (no audio output)";

    return text;
}

void PluginSlot::prepareForNewSampleRate (double newSampleRate, int newBlockSize, int newChannels)
{
    if (instance == nullptr)
        return;

    prepareInternal (newSampleRate, newBlockSize, newChannels);
}
}
