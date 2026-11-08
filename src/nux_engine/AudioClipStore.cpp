#include "AudioClipStore.h"

namespace nux
{
AudioClipStore::AudioClipStore()
{
    formatManager.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg and MP3 where available
}

std::shared_ptr<const SampleBuffer> AudioClipStore::load (const juce::File& file, juce::String& error)
{
    error.clear();

    if (! file.existsAsFile())
    {
        error = "Audio file not found: " + file.getFullPathName();
        return nullptr;
    }

    const auto key = file.getFullPathName();

    {
        const juce::ScopedLock lock (loadLock);
        const auto found = buffers.find (key);

        if (found != buffers.end())
            return found->second;
    }

    auto decoded = decode (file, error);

    if (decoded == nullptr)
        return nullptr;

    {
        const juce::ScopedLock lock (loadLock);
        buffers[key] = decoded;
    }

    return decoded;
}

std::shared_ptr<const SampleBuffer> AudioClipStore::decode (const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Unsupported or unreadable audio file: " + file.getFileName();
        return nullptr;
    }

    const auto numSamples = (int) juce::jmin (reader->lengthInSamples, (juce::int64) 60 * 60 * 48000);

    if (numSamples <= 0)
    {
        error = "The audio file is empty: " + file.getFileName();
        return nullptr;
    }

    auto buffer = std::make_shared<SampleBuffer>();

    // Always store stereo: mono sources are duplicated so the mixer path is uniform.
    buffer->audio.setSize (2, numSamples, false, true, false);
    reader->read (&buffer->audio, 0, numSamples, 0, true, true);

    if (reader->numChannels == 1)
        buffer->audio.copyFrom (1, 0, buffer->audio, 0, 0, numSamples);

    buffer->sourceSampleRate = juce::jmax (4000.0, reader->sampleRate);

    // Short fades so a clip never starts or ends on a click.
    const auto fade = (int) juce::jmax (16.0, buffer->sourceSampleRate * 0.004);
    buffer->fadeInSamples = juce::jmin (fade, numSamples / 2);
    buffer->fadeOutSamples = buffer->fadeInSamples;

    return buffer;
}

std::shared_ptr<const SampleBuffer> AudioClipStore::find (const juce::String& path) const
{
    const juce::ScopedLock lock (loadLock);
    const auto found = buffers.find (path);
    return found == buffers.end() ? nullptr : found->second;
}

void AudioClipStore::prune (const juce::StringArray& keepPaths)
{
    const juce::ScopedLock lock (loadLock);

    for (auto it = buffers.begin(); it != buffers.end();)
    {
        if (! keepPaths.contains (it->first))
            it = buffers.erase (it);
        else
            ++it;
    }
}

void AudioClipStore::clear()
{
    const juce::ScopedLock lock (loadLock);
    buffers.clear();
}

juce::int64 AudioClipStore::getMemoryUsageBytes() const noexcept
{
    const juce::ScopedLock lock (loadLock);
    juce::int64 total = 0;

    for (const auto& entry : buffers)
        if (entry.second != nullptr)
            total += (juce::int64) entry.second->getNumSamples()
                   * entry.second->getNumChannels() * (juce::int64) sizeof (float);

    return total;
}
}
