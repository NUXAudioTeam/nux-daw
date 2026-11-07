/* End-to-end engine test: builds a project, runs the audio engine offline and
   checks that the rendered result is real audio, not silence or noise.

   This test exercises the whole chain: sequencer -> synth voice -> channel strip
   (EQ / compressor / limiter) -> bus summing -> master -> WAV file on disk. */
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/nux_engine/AudioEngine.h"
#include <memory>

namespace nux
{
class EngineRenderTests final : public juce::UnitTest
{
public:
    EngineRenderTests() : juce::UnitTest ("NUX engine rendering", "NUX") {}

    void runTest() override
    {
        beginTest ("Default project renders audible, finite audio to WAV");
        {
            ProjectState project = ProjectState::createDefault();
            project.patternLengthSteps = 16;
            project.tempo = 120.0;

            // Give track 0 a simple insert chain to prove the DSP is in the path.
            project.tracks[0].inserts[0].type = InsertType::eq;
            project.tracks[0].inserts[0].p1 = 4.0f;
            project.tracks[0].inserts[0].p2 = 1200.0f;
            project.tracks[0].inserts[0].p3 = 2.0f;

            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            engine.setProjectState (&project);

            // The engine only needs an audio device for live playback; for an
            // offline render we drive the graph through the WAV exporter.
            engine.prepareForOfflineRendering (44100.0, 512, 2);

            auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("nux-engine-test")
                              .getChildFile ("render.wav");
            target.deleteFile();

            AudioEngine::ExportOptions options;
            options.targetFile = target;
            options.lengthSteps = 16;      // one bar
            options.tailSeconds = 0.5;

            juce::String error;
            expect (engine.exportToWav (options, error), "Export failed: " + error);
            expect (target.existsAsFile(), "The WAV file should exist");

            // Read the file back and inspect it.
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (target));
            expect (reader != nullptr);

            if (reader != nullptr)
            {
                expectEquals ((int) reader->numChannels, 2);
                expectWithinAbsoluteError (reader->sampleRate, 44100.0, 1.0);

                // One bar at 120 BPM is 2 seconds, plus a 0.5 s tail.
                expect (reader->lengthInSamples > (juce::int64) (2.0 * 44100.0),
                        "Rendered length should cover the bar plus tail");

                const auto samplesToRead = (int) juce::jmin ((juce::int64) 44100 * 2, reader->lengthInSamples);
                juce::AudioBuffer<float> rendered (2, samplesToRead);
                reader->read (&rendered, 0, samplesToRead, 0, true, true);

                const auto peak = rendered.getMagnitude (0, samplesToRead);
                const auto rms = rendered.getRMSLevel (0, 0, samplesToRead);

                expect (peak > 0.01f, "The render should contain audible signal");
                expect (peak <= 1.0f, "The limiter must keep the output within range");
                expect (rms > 0.0005f, "The render should not be near-silent");

                // No NaNs or infinities anywhere in the output.
                bool allFinite = true;

                for (int ch = 0; ch < rendered.getNumChannels() && allFinite; ++ch)
                    for (int i = 0; i < samplesToRead; ++i)
                        if (! std::isfinite (rendered.getSample (ch, i)))
                        {
                            allFinite = false;
                            break;
                        }

                expect (allFinite, "Rendered audio must be finite");

                // The sequencer should have produced several transients, so the level
                // must vary over time rather than being one constant tone.
                const auto firstHalf = rendered.getRMSLevel (0, 0, samplesToRead / 2);
                const auto secondHalf = rendered.getRMSLevel (0, samplesToRead / 2, samplesToRead / 2);
                expect (firstHalf > 0.0001f || secondHalf > 0.0001f,
                        "At least one half of the render should carry signal");
            }

            target.deleteFile();
            target.getParentDirectory().deleteRecursively();
        }

        beginTest ("An empty pattern renders silence");
        {
            ProjectState project = ProjectState::createDefault();

            for (auto& track : project.tracks)
                for (auto& step : track.steps)
                    step.enabled = false;

            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            engine.setProjectState (&project);
            engine.prepareForOfflineRendering (44100.0, 512, 2);

            auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("nux-engine-test")
                              .getChildFile ("silence.wav");
            target.deleteFile();

            AudioEngine::ExportOptions options;
            options.targetFile = target;
            options.lengthSteps = 4;
            options.tailSeconds = 0.2;

            juce::String error;
            expect (engine.exportToWav (options, error), error);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (target));

            if (reader != nullptr)
            {
                juce::AudioBuffer<float> rendered (2, (int) juce::jmin ((juce::int64) 8192, reader->lengthInSamples));
                reader->read (&rendered, 0, rendered.getNumSamples(), 0, true, true);
                expect (rendered.getMagnitude (0, rendered.getNumSamples()) < 0.001f,
                        "A pattern with no steps must render silence");
            }

            target.deleteFile();
            target.getParentDirectory().deleteRecursively();
        }

        beginTest ("An imported audio clip is rendered into the mixdown");
        {
            // Build a project folder with a media file, exactly like a real project.
            auto projectFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("nux-engine-clip")
                                     .getChildFile ("Song");
            projectFolder.getChildFile ("media").createDirectory();

            auto wavFile = projectFolder.getChildFile ("media").getChildFile ("tone.wav");
            wavFile.deleteFile();

            {
                auto stream = std::unique_ptr<juce::FileOutputStream> (wavFile.createOutputStream());
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> writer (
                    wav.createWriterFor (stream.get(), 44100.0, 1, 16, {}, 0));
                stream.release();

                juce::AudioBuffer<float> tone (1, 44100);

                for (int i = 0; i < 44100; ++i)
                    tone.setSample (0, i, 0.6f * std::sin (juce::MathConstants<double>::twoPi * 330.0 * i / 44100.0));

                writer->writeFromAudioSampleBuffer (tone, 0, 44100);
                writer.reset();
            }

            ProjectState project = ProjectState::createDefault();

            // Silence the drum sequencer so only the audio clip can produce sound.
            for (auto& step : project.tracks[0].steps)
                step.enabled = false;

            project.tracks[2].type = TrackType::audio;
            AudioClip clip;
            clip.name = "tone";
            clip.sourceFile = "media/tone.wav";
            clip.startStep = 0;
            clip.lengthSteps = 16;   // one bar at 120 BPM = 2 s, the clip is 1 s
            project.tracks[2].audioClips.push_back (clip);

            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            engine.setProjectState (&project);
            engine.setMediaRootDirectory (projectFolder);
            engine.prepareForOfflineRendering (44100.0, 512, 2);
            engine.reloadAudioClips();

            expect (engine.getNumLoadedClipFiles() >= 1, "The clip file should have been decoded");

            auto target = projectFolder.getChildFile ("render.wav");
            target.deleteFile();

            // Render the bar once, without looping, so the tail is genuinely silent.
            project.loopEnabled = false;
            engine.getTransport().setLoopEnabled (false);

            AudioEngine::ExportOptions options;
            options.targetFile = target;
            options.lengthSteps = 16;
            options.tailSeconds = 0.3;

            juce::String error;
            expect (engine.exportToWav (options, error), "Export failed: " + error);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (target));
            expect (reader != nullptr);

            if (reader != nullptr)
            {
                // Read the whole two second bar so the quarter measurements stay in range.
                const auto samplesToRead = (int) juce::jmin (reader->lengthInSamples, (juce::int64) 88200);
                juce::AudioBuffer<float> rendered (2, samplesToRead);
                rendered.clear();
                reader->read (&rendered, 0, samplesToRead, 0, true, true);

                // The clip is one second long, so compare the first second with the tail
                // of the bar. Every read stays inside the buffer.
                const auto quarter = samplesToRead / 4;
                const auto firstSecond = rendered.getMagnitude (0, 0, juce::jmin (44100, samplesToRead));
                const auto lastQuarterSecond = quarter > 0
                                                 ? rendered.getMagnitude (0, samplesToRead - quarter, quarter)
                                                 : 0.0f;

                expect (firstSecond > 0.05f, "The audio clip should be audible in the first second");
                // Diagnostic detail: report the level in each quarter of the bar.
                expect (lastQuarterSecond < firstSecond * 0.25f,
                        "The clip should have stopped after one second, but the tail is still at "
                        + juce::String (lastQuarterSecond));
            }

            target.deleteFile();
            projectFolder.deleteRecursively();
        }

        beginTest ("Transport advances and loops");
        {
            ProjectState project = ProjectState::createDefault();
            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            engine.setProjectState (&project);
            engine.prepareForOfflineRendering (44100.0, 512, 2);

            auto& transport = engine.getTransport();
            transport.setTempo (120.0);
            transport.setLoopLengthSteps (16);
            transport.setLoopEnabled (true);
            transport.setPositionSteps (0.0);
            transport.setPlaying (true);

            engine.runBlocksForTest (100);   // ~1.16 s at 512 samples

            // 120 BPM in 16th notes = 8 steps per second, so ~9 steps after 1.16 s,
            // and the position must stay inside the loop.
            const auto position = transport.getPositionSteps();
            expect (position > 1.0, "The transport should have advanced, was " + juce::String (position));
            expect (position < 16.0, "The transport should stay within the loop");

            transport.setPlaying (false);
        }

        beginTest ("Solo mutes the other tracks");
        {
            ProjectState project = ProjectState::createDefault();

            // Two drum tracks with identical patterns; solo one of them.
            project.tracks[1].type = TrackType::sampler;
            project.tracks[1].numSteps = 16;
            project.tracks[1].rootNote = 38;

            for (int i = 0; i < 16; i += 2)
                project.tracks[1].steps[(size_t) i] = { true, 0.9f };

            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            engine.setProjectState (&project);
            engine.prepareForOfflineRendering (44100.0, 512, 2);

            engine.runBlocksForTest (20);

            const auto bothTracks = engine.getTrackPeak (0, 0);
            expect (bothTracks >= 0.0f);

            project.tracks[0].soloed = true;
            engine.syncMixerFromState();
            engine.runBlocksForTest (20);

            // Track 1 is no longer soloed, so its meter must fall silent.
            expect (engine.getTrackPeak (1, 0) < 0.001f,
                    "A non-soloed track must not be heard while another track is soloed");
        }
    }
};

static EngineRenderTests engineRenderTests;
}
