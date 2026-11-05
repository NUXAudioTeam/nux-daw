#include "AudioClipPlayer.h"

namespace nux
{
void AudioClipPlayer::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    blockSize = juce::jmax (32, maxBlockSize);
    clipBuffer.setSize (2, blockSize, false, true, true);
    prepared = true;
}

void AudioClipPlayer::reset()
{
    // Nothing is cached between blocks: the transport position is the clock.
}

void AudioClipPlayer::setClips (std::vector<ClipSnapshot> newClips)
{
    clips = std::move (newClips);
}

void AudioClipPlayer::render (juce::AudioBuffer<float>& output, int numSamples,
                              double startStep, double stepsPerSample,
                              int loopLengthSteps, bool looping)
{
    if (! prepared || clips.empty() || numSamples <= 0)
        return;

    for (const auto& clip : clips)
        renderClip (clip, output, numSamples, startStep, stepsPerSample, loopLengthSteps, looping);
}

void AudioClipPlayer::renderClip (const ClipSnapshot& clip, juce::AudioBuffer<float>& output,
                                 int numSamples, double startStep, double stepsPerSample,
                                 int loopLengthSteps, bool looping)
{
    if (clip.buffer == nullptr || clip.buffer->isEmpty())
        return;

    if (clip.lengthSteps <= 0 || clip.startStep < 0 || stepsPerSample <= 0.0)
        return;

    const auto& source = *clip.buffer;
    const auto sourceSamples = source.getNumSamples();
    const auto sourceRatio = source.sourceSampleRate > 0.0 ? source.sourceSampleRate / sampleRate : 1.0;
    const auto numChannels = juce::jmin (output.getNumChannels(), source.getNumChannels());

    int written = 0;

    while (written < numSamples)
    {
        // Where the playhead is, wrapped into the loop.
        auto playheadStep = startStep + stepsPerSample * (double) written;

        if (looping && loopLengthSteps > 0)
            playheadStep = std::fmod (playheadStep, (double) loopLengthSteps);

        auto stepsIntoClip = playheadStep - (double) clip.startStep;

        // Outside the clip: jump straight to the moment it next becomes relevant.
        if (stepsIntoClip < 0.0 || stepsIntoClip >= (double) clip.lengthSteps)
        {
            double samplesToSkip;

            if (stepsIntoClip < 0.0)
            {
                samplesToSkip = std::ceil (-stepsIntoClip / stepsPerSample);
            }
            else if (looping && loopLengthSteps > 0)
            {
                // Next retrigger is on the next pass of the loop.
                const auto stepsToLoopEnd = (double) loopLengthSteps - playheadStep;

                if (stepsToLoopEnd <= 1.0e-9)
                    samplesToSkip = 1.0;   // wrap immediately
                else if (clip.startStep < stepsToLoopEnd)
                    samplesToSkip = std::ceil ((stepsToLoopEnd + (double) clip.startStep) / stepsPerSample);
                else
                    samplesToSkip = std::ceil ((double) clip.startStep / stepsPerSample);
            }
            else
            {
                return;   // nothing else to play in this block
            }

            const auto skip = juce::jlimit (1, numSamples - written, (int) juce::jmax (1.0, samplesToSkip));
            written += skip;
            continue;
        }

        // Samples until the clip (or the loop) ends, whichever comes first.
        auto stepsRemaining = (double) clip.lengthSteps - stepsIntoClip;

        if (looping && loopLengthSteps > 0)
        {
            const auto stepsToLoopEnd = (double) loopLengthSteps - playheadStep;
            stepsRemaining = juce::jmin (stepsRemaining, juce::jmax (0.0, stepsToLoopEnd));
        }

        const auto samplesAvailable = (int) std::ceil (stepsRemaining / stepsPerSample) + 1;
        const auto samples = juce::jlimit (1, numSamples - written, samplesAvailable);

        auto sourcePosition = clip.sourceStartSample
                            + (juce::int64) std::llround (stepsIntoClip / stepsPerSample * sourceRatio);

        for (int i = 0; i < samples; ++i)
        {
            if (sourcePosition < 0 || sourcePosition >= sourceSamples)
                break;

            float fade = 1.0f;

            if (source.fadeInSamples > 0 && sourcePosition < (juce::int64) source.fadeInSamples)
                fade = (float) sourcePosition / (float) source.fadeInSamples;

            const auto samplesToEnd = sourceSamples - sourcePosition;

            if (source.fadeOutSamples > 0 && samplesToEnd < (juce::int64) source.fadeOutSamples)
                fade = juce::jmin (fade, (float) samplesToEnd / (float) source.fadeOutSamples);

            const auto gain = fade * clip.gain;

            for (int ch = 0; ch < numChannels; ++ch)
                output.addSample (ch, written + i, source.audio.getSample (ch, (int) sourcePosition) * gain);

            ++sourcePosition;
        }

        written += samples;
    }
}
}
