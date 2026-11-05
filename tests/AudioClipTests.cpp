/* Tests for audio clip decoding and playback.

   These write real WAV files to a temporary folder, load them through the clip
   store, and then run the clip player against a known transport position so the
   expected sample values are checkable rather than "something came out". */
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/nux_core/Constants.h"
#include "../src/nux_engine/AudioClipPlayer.h"
#include "../src/nux_engine/AudioClipStore.h"

namespace nux
{
namespace
{
/** Writes a mono/stereo test tone to a 16-bit WAV inside `folder`. */
juce::File writeTestWav (const juce::File& folder, const juce::String& name,
                         int numSamples, double sampleRate, float amplitude, int numChannels = 1)
{
    folder.createDirectory();
    auto file = folder.getChildFile (name);
    file.deleteFile();

    file.deleteFile();

    auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, (unsigned int) numChannels, 16, {}, 0));

    if (writer == nullptr)
        return {};

    stream.release();

    juce::AudioBuffer<float> buffer (numChannels, numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (ch, i, amplitude * std::sin (juce::MathConstants<double>::twoPi
                                                            * 220.0 * i / sampleRate));

    writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
    writer.reset();

    return file;
}

} // namespace

class AudioClipTests final : public juce::UnitTest
{
public:
    AudioClipTests() : juce::UnitTest ("NUX audio clips", "NUX") {}

    void runTest() override
    {
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("nux-clip-tests");

        beginTest ("A WAV file decodes into a stereo sample buffer");
        {
            const auto file = writeTestWav (folder, "tone.wav", 44100, 44100.0, 0.5f, 1);
            expect (file.existsAsFile(), "The test WAV should have been written");

            AudioClipStore store;
            juce::String error;
            auto buffer = store.load (file, error);

            expect (buffer != nullptr, "Loading failed: " + error);

            if (buffer != nullptr)
            {
                expectEquals (buffer->getNumChannels(), 2, "Mono sources are stored as stereo");
                expectEquals (buffer->getNumSamples(), 44100);
                expectWithinAbsoluteError (buffer->sourceSampleRate, 44100.0, 1.0);
                expectWithinAbsoluteError (buffer->getLengthSeconds(), 1.0, 0.001);
                expect (buffer->fadeInSamples > 0, "The clip should have edge fades");

                // Both channels must carry the same signal for a mono source.
                const auto left = buffer->audio.getMagnitude (0, 1000, 1000);
                const auto right = buffer->audio.getMagnitude (1, 1000, 1000);
                expectWithinAbsoluteError (left, right, 0.001f);
                expect (left > 0.1f, "The decoded audio should not be silent");
            }

            expectEquals (store.getNumLoaded(), 1);

            // A second load of the same path must hit the cache, not the disk.
            auto again = store.load (file, error);
            expect (again.get() == buffer.get(), "Repeated loads should return the cached buffer");
            expectEquals (store.getNumLoaded(), 1);
        }

        beginTest ("Missing and invalid files fail cleanly");
        {
            AudioClipStore store;
            juce::String error;

            expect (store.load (folder.getChildFile ("does-not-exist.wav"), error) == nullptr);
            expect (error.containsIgnoreCase ("not found"));

            auto junk = folder.getChildFile ("junk.wav");
            folder.createDirectory();
            junk.replaceWithText ("this is not audio");
            expect (store.load (junk, error) == nullptr);
            expect (error.isNotEmpty());
            junk.deleteFile();
        }

        beginTest ("The clip player renders the clip at the right transport position");
        {
            const auto file = writeTestWav (folder, "player.wav", 44100, 44100.0, 0.5f);
            AudioClipStore store;
            juce::String error;
            auto buffer = store.load (file, error);
            expect (buffer != nullptr, error);

            if (buffer != nullptr)
            {
                AudioClipPlayer player;
                player.prepare (44100.0, 512);

                ClipSnapshot clip;
                clip.buffer = buffer;
                clip.startStep = 0;
                clip.lengthSteps = 8;          // 8 sixteenths = 1 second at 120 BPM
                clip.sourceStartSample = 0;
                clip.gain = 1.0f;

                player.setClips ({ clip });
                expectEquals (player.getNumClips(), 1);

                // 120 BPM: one step is 0.125 s, so 4 steps = 0.5 s.
                const auto stepsPerSample = (120.0 / 60.0) * nux::constants::stepsPerBeat / 44100.0;

                // Before the clip starts: silence.
                {
                    juce::AudioBuffer<float> out (2, 512);
                    out.clear();
                    // Looping off, well past the clip: silence. (With looping on, step 100
                    // would wrap into the loop and the clip would legitimately play.)
                    player.render (out, 512, 20.0, stepsPerSample, 16, false);
                    expect (out.getMagnitude (0, 512) < 0.001f, "Nothing should play outside the clip");
                }

                // Inside the clip: real audio.
                {
                    juce::AudioBuffer<float> out (2, 512);
                    out.clear();
                    player.render (out, 512, 2.0, stepsPerSample, 16, true);
                    const auto magnitude = out.getMagnitude (0, 512);
                    expect (magnitude > 0.05f, "The clip should be audible inside its range, got "
                                              + juce::String (magnitude));
                    expect (magnitude <= 0.6f, "The clip must respect its gain");
                }

                // Past the end of the clip: silence again.
                {
                    juce::AudioBuffer<float> out (2, 512);
                    out.clear();
                    player.render (out, 512, 9.5, stepsPerSample, 16, true);
                    expect (out.getMagnitude (0, 512) < 0.001f, "Nothing should play after the clip ends");
                }
            }
        }

        beginTest ("Looped playback wraps clips back into the pattern");
        {
            const auto file = writeTestWav (folder, "loop.wav", 22050, 44100.0, 0.5f);
            AudioClipStore store;
            juce::String error;
            auto buffer = store.load (file, error);
            expect (buffer != nullptr, error);

            if (buffer != nullptr)
            {
                AudioClipPlayer player;
                player.prepare (44100.0, 256);

                ClipSnapshot clip;
                clip.buffer = buffer;
                clip.startStep = 0;
                clip.lengthSteps = 4;
                player.setClips ({ clip });

                const auto stepsPerSample = (120.0 / 60.0) * nux::constants::stepsPerBeat / 44100.0;

                // Second time round the 16 step loop the clip must play again.
                juce::AudioBuffer<float> out (2, 256);
                out.clear();
                player.render (out, 256, 16.0, stepsPerSample, 16, true);
                expect (out.getMagnitude (0, 256) > 0.05f,
                        "A looped clip should retrigger at the start of the next loop pass");
            }
        }

        beginTest ("Stereo sources keep their channels separate");
        {
            auto file = folder.getChildFile ("stereo.wav");
            folder.createDirectory();
            file.deleteFile();

            auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (stream.get(), 44100.0, 2, 16, {}, 0));
            stream.release();

            juce::AudioBuffer<float> source (2, 4410);

            for (int i = 0; i < 4410; ++i)
            {
                source.setSample (0, i, 0.5f);    // left DC
                source.setSample (1, i, -0.5f);   // right DC
            }

            writer->writeFromAudioSampleBuffer (source, 0, 4410);
            writer.reset();

            AudioClipStore store;
            juce::String error;
            auto buffer = store.load (file, error);
            expect (buffer != nullptr, error);

            if (buffer != nullptr)
            {
                const auto left = buffer->audio.getSample (0, 2000);
                const auto right = buffer->audio.getSample (1, 2000);
                expect (left > 0.4f, "Left channel should be positive");
                expect (right < -0.4f, "Right channel should be negative");
            }
        }

        folder.deleteRecursively();
    }
};

static AudioClipTests audioClipTests;
}
