/* NUX DAW - offline (device independent) audio engine tests.

   Everything here runs with no sound card at all: the engine is prepared for offline
   rendering and blocks are pulled by hand. That makes the DSP fully testable on a
   build machine, and it is why the audio engine can keep moving without hardware.

   What these tests can prove:  signal content, frequency, level, clipping, ramps,
                                channel behaviour, buffer sizes, sample rates,
                                WAV writing and reading.
   What they cannot prove:      that a speaker makes a noise or a microphone captures
                                one. Those are hardware checks - see
                                docs/hardware-checklist.md and AUDIO_TEST_REPORT.md.
*/
#include "SignalAnalysis.h"
#include "../src/nux_engine/AudioEngine.h"
#include "../src/nux_engine/TestTone.h"
#include "../src/nux_engine/InputRecorder.h"
#include "../src/nux_core/TrackModel.h"
#include <memory>

namespace nux::tests
{
class OfflineAudioTests final : public juce::UnitTest
{
public:
    OfflineAudioTests() : juce::UnitTest ("NUX offline audio", "NUX") {}

    /** Pulls N blocks from the engine into one long buffer. */
    static juce::AudioBuffer<float> renderEngine (AudioEngine& engine, int blocks, int blockSize)
    {
        juce::AudioBuffer<float> total (2, blocks * blockSize);
        total.clear();

        juce::AudioBuffer<float> scratch (2, blockSize);

        for (int i = 0; i < blocks; ++i)
        {
            scratch.clear();
            engine.renderBlockForTest (scratch, blockSize);

            for (int ch = 0; ch < 2; ++ch)
                total.copyFrom (ch, i * blockSize, scratch, ch, 0, blockSize);
        }

        return total;
    }

    void prepareEngine (AudioEngine& engine, double sampleRate, int blockSize)
    {
        project = ProjectState::createDefault();
        engine.setProjectState (&project);
        engine.prepareForOfflineRendering (sampleRate, blockSize, 2);
    }

    void runTest() override
    {
        beginTest ("A 440 Hz tone is accurate at every supported sample rate");
        {
            for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
            {
                auto enginePtr = std::make_unique<AudioEngine>();
                auto& engine = *enginePtr;
                prepareEngine (engine, rate, 512);

                engine.setTestToneEnabled (true);
                engine.setTestToneLevel (1.0f);

                const auto total = renderEngine (engine, 60, 512);

                // Measure the tail, after the ramp has settled.
                juce::AudioBuffer<float> tail (2, total.getNumSamples() / 2);
                tail.copyFrom (0, 0, total, 0, total.getNumSamples() / 2, tail.getNumSamples());
                tail.copyFrom (1, 0, total, 1, total.getNumSamples() / 2, tail.getNumSamples());

                const auto analysis = analyse (tail, 0, rate);

                // 0.5% tolerance: generous for a zero-crossing estimate, tight enough to
                // catch a wrong sample rate or a broken phase increment.
                expectWithinAbsoluteError (analysis.frequency, 440.0, 440.0 * 0.005,
                                           "440 Hz tone at " + juce::String (rate) + " Hz sample rate "
                                             + "measured " + juce::String (analysis.frequency, 2) + " Hz");
                expect (analysis.peak > 0.05f,
                        "The tone must actually be audible at " + juce::String (rate) + " Hz");
                expect (isFinite (total), "The output must contain no NaN or infinity");
            }
        }

        beginTest ("The tone level in dBFS matches the requested level");
        {
            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            prepareEngine (engine, 44100.0, 512);
            engine.setTestToneEnabled (true);
            engine.setTestToneLevel (1.0f);

            const auto total = renderEngine (engine, 80, 512);

            // The generator applies 0.35 as full scale, so 100% maps to about -9 dBFS.
            const auto analysis = analyse (total, 0, 44100.0);
            const auto expected = juce::Decibels::gainToDecibels (0.35f);

            expectWithinAbsoluteError (analysis.peakDb, expected, 1.0f,
                                       "A full level tone should sit near " + juce::String (expected, 1)
                                         + " dBFS but measured " + juce::String (analysis.peakDb, 1));
        }

        beginTest ("The tone never clips, at any level");
        {
            for (const auto level : { 0.25f, 0.5f, 0.75f, 1.0f })
            {
                auto enginePtr = std::make_unique<AudioEngine>();
                auto& engine = *enginePtr;
                prepareEngine (engine, 44100.0, 512);
                engine.setTestToneEnabled (true);
                engine.setTestToneLevel (level);

                const auto total = renderEngine (engine, 40, 512);
                const auto analysis = analyse (total, 0, 44100.0);

                expect (! analysis.clips,
                        "The tone clipped at level " + juce::String (level, 2)
                          + " (peak " + juce::String (analysis.peakDb, 2) + " dBFS)");
                expect (analysis.peakDb < -0.5f,
                        "The tone must keep headroom below 0 dBFS (measured "
                          + juce::String (analysis.peakDb, 2) + ")");
            }
        }

        beginTest ("Both channels carry the same tone");
        {
            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            prepareEngine (engine, 48000.0, 256);
            engine.setTestToneEnabled (true);

            const auto total = renderEngine (engine, 60, 256);

            const auto difference = channelDifference (total);
            expect (difference < 0.0001f,
                    "Left and right must be identical for a mono test tone (difference "
                      + juce::String (difference, 6) + ")");

            const auto left = analyse (total, 0, 48000.0);
            const auto right = analyse (total, 1, 48000.0);
            expectWithinAbsoluteError (left.frequency, right.frequency, 1.0);
        }

        beginTest ("Turning the tone on and off produces ramps, not clicks");
        {
            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            prepareEngine (engine, 44100.0, 256);

            // Silence, then on, then off, all rendered into one buffer.
            juce::AudioBuffer<float> total (2, 256 * 120);
            total.clear();

            juce::AudioBuffer<float> scratch (2, 256);
            int offset = 0;

            auto pull = [&] (int blocks)
            {
                for (int i = 0; i < blocks; ++i)
                {
                    scratch.clear();
                    engine.renderBlockForTest (scratch, 256);

                    for (int ch = 0; ch < 2; ++ch)
                        total.copyFrom (ch, offset, scratch, ch, 0, 256);

                    offset += 256;
                }
            };

            pull (20);                                    // silence
            engine.setTestToneEnabled (true);
            pull (50);                                    // ramp up and run
            engine.setTestToneEnabled (false);
            pull (50);                                    // ramp down to silence

            const auto amplitude = analyse (total, 0, 44100.0).peak;
            const auto limit = sineStepLimit (amplitude, 440.0, 44100.0);

            const auto step = largestStep (total, 0);
            expect (step < limit * 2.5f,
                    "Switching the tone on or off must not click (largest step "
                      + juce::String (step, 5) + ", sine slope limit "
                      + juce::String (limit, 5) + ")");

            // The tail must actually be silent.
            juce::AudioBuffer<float> tail (2, 256 * 20);
            tail.copyFrom (0, 0, total, 0, total.getNumSamples() - 256 * 20, 256 * 20);
            expect (analyse (tail, 0, 44100.0).peak < 0.001f,
                    "The tone must reach silence after being switched off");
        }

        beginTest ("The engine renders every buffer size from 32 to 2048");
        {
            for (const auto blockSize : { 32, 64, 128, 256, 512, 1024, 2048 })
            {
                auto enginePtr = std::make_unique<AudioEngine>();
                auto& engine = *enginePtr;
                prepareEngine (engine, 44100.0, blockSize);
                engine.setTestToneEnabled (true);

                juce::AudioBuffer<float> buffer (2, blockSize);
                buffer.clear();
                engine.renderBlockForTest (buffer, blockSize);

                expect (isFinite (buffer),
                        "A block size of " + juce::String (blockSize) + " produced invalid samples");

                // Several blocks, so a mis-sized internal buffer cannot hide.
                auto peak = 0.0f;

                for (int i = 0; i < 30; ++i)
                {
                    buffer.clear();
                    engine.renderBlockForTest (buffer, blockSize);
                    peak = juce::jmax (peak, buffer.getMagnitude (0, blockSize));
                }

                expect (peak > 0.05f,
                        "A block size of " + juce::String (blockSize) + " produced no audio (peak "
                          + juce::String (peak, 4) + ")");
                expect (peak <= 1.0f,
                        "A block size of " + juce::String (blockSize) + " clipped (peak "
                          + juce::String (peak, 4) + ")");
            }
        }

        beginTest ("A note plays through the built-in synth and lands in the mix");
        {
            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            prepareEngine (engine, 44100.0, 512);

            // Track 0 is a sampler track with the built-in synth.
            engine.triggerPreviewNote (0, 60, 0.9f);

            const auto total = renderEngine (engine, 30, 512);
            const auto analysis = analyse (total, 0, 44100.0);

            expect (analysis.peak > 0.01f,
                    "A note on the built-in synth must produce audio (peak "
                      + juce::String (analysis.peak, 4) + ")");
            expect (isFinite (total), "The synth output must be finite");
        }

        beginTest ("Mute silences a track and solo isolates it");
        {
            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            prepareEngine (engine, 44100.0, 512);

            engine.triggerPreviewNote (0, 60, 0.9f);
            expect (analyse (renderEngine (engine, 20, 512), 0).peak > 0.01f,
                    "The track should be audible before muting");

            // Mute lives in the model; syncMixerFromState pushes it into the graph.
            project.tracks[0].muted = true;
            engine.syncMixerFromState();
            engine.triggerPreviewNote (0, 60, 0.9f);

            expect (analyse (renderEngine (engine, 20, 512), 0).peak < 0.001f,
                    "A muted track must be silent");

            project.tracks[0].muted = false;
            project.tracks[0].soloed = true;
            engine.syncMixerFromState();
            engine.triggerPreviewNote (0, 60, 0.9f);

            const auto soloedPeak = analyse (renderEngine (engine, 20, 512), 0).peak;
            expect (soloedPeak > 0.001f,
                    "A soloed track must still be audible (peak "
                      + juce::String (soloedPeak, 4) + ")");
            expect (isFinite (renderEngine (engine, 4, 512)), "Solo rendering must stay finite");

            // Release the solo so it cannot leak into the following tests.
            project.tracks[0].soloed = false;
            engine.syncMixerFromState();
        }

        beginTest ("The master fader scales program material");
        {
            // Program material, not the test tone: the tone is added after the master
            // chain on purpose, so that a fader at -inf cannot make a working device
            // look broken. The fader is tested with a real track instead.
            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            prepareEngine (engine, 44100.0, 512);

            // Measure a settled tail: the fader is smoothed, so the first blocks still
            // carry the previous gain and would flatter the result.
            auto settledPeak = [this, &engine]
            {
                // Re-trigger each time: runBlocksForTest consumes the queued note, so a
                // second attempt would measure silence instead of the synth.
                engine.triggerPreviewNote (0, 60, 0.9f);

                // A synth note is only ~0.2 s long and ends in a release ramp, so the
                // peak is taken over the whole render, not over a tail.
                return analyse (renderEngine (engine, 24, 512), 0, 44100.0).peak;
            };

            const auto loud = settledPeak();
            expect (loud > 0.01f, "The synth must be audible before touching the master fader");

            project.master.volumeDb = -12.0f;
            engine.syncMixerFromState();

            // The master fader is smoothed over 20 ms. Let the smoothing finish on a
            // silent graph first, or the measurement would still contain the old gain.
            engine.panic();
            engine.runBlocksForTest (6);

            const auto quiet = settledPeak();

            expect (quiet < loud * 0.5f,
                    "Pulling the master fader down must reduce the output (loud "
                      + juce::String (loud, 4) + ", quiet " + juce::String (quiet, 4) + ")");
            expect (quiet > 0.0f, "The master fader must not silence the output completely");
        }

        beginTest ("The test tone ignores mutes and the master fader on purpose");
        {
            // This is a deliberate design rule, so it is pinned by a test: the tone is a
            // device test and must be heard even in an otherwise silent project.
            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            prepareEngine (engine, 44100.0, 512);
            engine.setTestToneEnabled (true);

            const auto normal = analyse (renderEngine (engine, 40, 512), 0).peak;

            for (auto& track : project.tracks)
                track.muted = true;

            project.master.volumeDb = -60.0f;
            engine.syncMixerFromState();

            const auto withEverythingOff = analyse (renderEngine (engine, 40, 512), 0).peak;

            expect (withEverythingOff > 0.05f,
                    "The test tone must still be audible with every track muted and the "
                    "master fader down (peak " + juce::String (withEverythingOff, 4) + ")");
            expectWithinAbsoluteError (withEverythingOff, normal, 0.02f,
                                       "Mutes and the master fader must not change the test tone");
        }

        beginTest ("Rendering is deterministic: the same input gives the same output");
        {
            auto render = [] (juce::AudioBuffer<float>& destination)
            {
                auto enginePtr = std::make_unique<AudioEngine>();
                auto& engine = *enginePtr;
                ProjectState localProject = ProjectState::createDefault();
                engine.setProjectState (&localProject);
                engine.prepareForOfflineRendering (44100.0, 512, 2);
                engine.setTestToneEnabled (true);

                destination.setSize (2, 512 * 40);
                destination.clear();

                juce::AudioBuffer<float> scratch (2, 512);

                for (int i = 0; i < 40; ++i)
                {
                    scratch.clear();
                    engine.renderBlockForTest (scratch, 512);

                    for (int ch = 0; ch < 2; ++ch)
                        destination.copyFrom (ch, i * 512, scratch, ch, 0, 512);
                }
            };

            juce::AudioBuffer<float> first, second;
            render (first);
            render (second);

            auto largestDifference = 0.0f;

            for (int i = 0; i < first.getNumSamples(); ++i)
                largestDifference = juce::jmax (largestDifference,
                                                std::abs (first.getSample (0, i) - second.getSample (0, i)));

            expect (largestDifference < 1.0e-6f,
                    "Two identical renders must match exactly (largest difference "
                      + juce::String (largestDifference, 8) + ")");
        }

        beginTest ("Sample rate changes are handled without artefacts");
        {
            // Preparation at a new rate must not produce garbage on the first block.
            for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                auto enginePtr = std::make_unique<AudioEngine>();
                auto& engine = *enginePtr;
                prepareEngine (engine, rate, 256);
                engine.setTestToneEnabled (true);

                juce::AudioBuffer<float> buffer (2, 256);
                buffer.clear();
                engine.renderBlockForTest (buffer, 256);

                expect (isFinite (buffer),
                        "The first block at " + juce::String (rate) + " Hz must be finite");

                auto peak = 0.0f;

                for (int i = 0; i < 40; ++i)
                {
                    buffer.clear();
                    engine.renderBlockForTest (buffer, 256);
                    peak = juce::jmax (peak, buffer.getMagnitude (0, 256));
                }

                expect (! analyse (buffer, 0, rate).clips,
                        "No clipping at " + juce::String (rate) + " Hz");
            }
        }

        beginTest ("A rendered take is written as a WAV that reads back sample-accurately");
        {
            // This is the full offline recording path: generate, capture, write, read.
            auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("nux-offline-wav");
            folder.createDirectory();

            const auto file = folder.getChildFile ("offline-render.wav");
            file.deleteFile();

            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::String error;
            expect (recorder.start (file, 0.0, 0, error), "Could not start the take: " + error);

            // Push a known 440 Hz tone through the recorder, as the audio thread would.
            juce::AudioBuffer<float> block (2, 512);

            for (int i = 0; i < 60; ++i)
            {
                for (int n = 0; n < 512; ++n)
                {
                    const auto value = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                * 440.0 * (i * 512 + n) / 44100.0);
                    block.setSample (0, n, value);
                    block.setSample (1, n, value);
                }

                const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };
                recorder.pushInput (channels, 2, 512);
                recorder.drainToFile();
            }

            recorder.stop();

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            expect (reader != nullptr, "The rendered take must read back");

            if (reader == nullptr)
                return;

            juce::AudioBuffer<float> readBack (2, (int) reader->lengthInSamples);
            reader->read (&readBack, 0, (int) reader->lengthInSamples, 0, true, true);

            const auto analysis = analyse (readBack, 0, 44100.0);

            expectWithinAbsoluteError (analysis.frequency, 440.0, 3.0,
                                       "The WAV must still contain 440 Hz (measured "
                                         + juce::String (analysis.frequency, 2) + ")");
            expectWithinAbsoluteError (analysis.peak, 0.5f, 0.02f,
                                       "The WAV peak must match what was recorded (peak "
                                         + juce::String (analysis.peak, 4) + ")");
            expect (! analysis.clips, "The recorded WAV must not be clipped");
            expect (isFinite (readBack), "The recorded WAV must contain only finite samples");
        }
    }

private:
    ProjectState project;
};

static OfflineAudioTests offlineAudioTests;
}
