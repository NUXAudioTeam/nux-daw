/* NUX DAW - recording reliability tests.

   Every group here is a way a recording can be lost, silently shortened, or attributed to
   the wrong track. They run without any audio hardware: the recorder is driven from a
   "virtual" callback thread the test controls, which is enough to exercise the ring buffer
   handshake, the file writing and the finalisation path. Nothing here proves anything about
   a real interface, and the report says so. */
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/nux_engine/InputRecorder.h"

#include <cmath>
#include <thread>

namespace nux
{
class RecordingReliabilityTests final : public juce::UnitTest
{
public:
    RecordingReliabilityTests() : juce::UnitTest ("NUX recording reliability", "NUX") {}

    //==============================================================================
    static juce::File makeTempFolder()
    {
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("nux-recording-reliability")
                          .getChildFile (juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
        folder.createDirectory();
        return folder;
    }

    /** A block of audio whose every sample is unique, so a lost or duplicated block is
        visible in the file rather than hidden by silence. */
    static void fillRamp (juce::AudioBuffer<float>& buffer, juce::int64 firstSample)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, std::fmod ((float) (firstSample + i) * 0.001f, 1.0f));
    }

    static juce::int64 readBackSamples (const juce::File& file)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(),
                                                                            true));
        return reader != nullptr ? reader->lengthInSamples : -1;
    }

    //==============================================================================
    void runTest() override
    {
        const auto folder = makeTempFolder();

        beginTest ("A full ring buffer drops samples and counts them instead of overwriting");
        {
            // 6 s of slack at 44.1 kHz. Pushing more than that without ever draining is not
            // hypothetical: a slow disk, a suspended machine, a timer that stops firing.
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::String error;
            const auto file = folder.getChildFile ("overrun.wav");
            expect (recorder.start (file, 0.0, 0, error), "start: " + error);

            juce::AudioBuffer<float> block (2, 512);
            const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };

            juce::int64 pushed = 0;
            const auto blocks = 2000;   // ~1024000 samples, well past the ring

            for (int i = 0; i < blocks; ++i)
            {
                fillRamp (block, pushed);
                recorder.pushInput (channels, 2, 512);
                pushed += 512;
            }

            const auto dropped = recorder.getDroppedSamples();
            expect (dropped > 0, "an overrun must be reported, not hidden");
            expect (dropped < pushed, "and it must not be everything");

            recorder.drainToFile();
            const auto written = recorder.getRecordedSamples();

            expectEquals (written + dropped, pushed,
                          "every pushed sample is either written or counted as dropped");
            expect (recorder.stop(), "stop: " + recorder.getLastError());
        }

        beginTest ("The last block of a take is not lost when recording stops");
        {
            // stop() used to drain once and close. A callback that had already passed the
            // recording flag then published its samples after the drain, and the tail of
            // every take was silently discarded.
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::String error;
            const auto file = folder.getChildFile ("tail.wav");
            expect (recorder.start (file, 0.0, 0, error), "start: " + error);

            juce::AudioBuffer<float> block (2, 512);
            const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };

            juce::int64 pushed = 0;

            // A callback thread that keeps pushing until the recorder says stop. The point is
            // that the samples it pushed while stop() was running still reach the file.
            std::atomic<bool> go { true };
            std::thread callback ([&]
            {
                while (go.load())
                {
                    fillRamp (block, pushed);
                    recorder.pushInput (channels, 2, 512);
                    pushed += 512;
                    std::this_thread::yield();
                }
            });

            juce::Thread::sleep (30);
            go.store (false);
            callback.join();

            expect (recorder.stop(), "stop: " + recorder.getLastError());

            const auto written = recorder.getRecordedSamples();
            const auto dropped = recorder.getDroppedSamples();

            expectEquals (written + dropped, pushed,
                          "everything the callback captured is written or counted, not lost");
            expectEquals (readBackSamples (file), written,
                          "and the file holds exactly that many samples");
        }

        beginTest ("A device change mid-take does not rewrite the take's length");
        {
            // Reconnecting an interface or changing the buffer size in the settings while a
            // take is running used to change the recorder's sample rate. The writer kept
            // writing at the old rate, so a 10 s take reported itself as 5 s and the clip
            // landed at half its real length.
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::String error;
            const auto file = folder.getChildFile ("ratechange.wav");
            expect (recorder.start (file, 0.0, 0, error), "start: " + error);

            juce::AudioBuffer<float> block (2, 512);
            const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };

            const auto blocks = 100;
            fillRamp (block, 0);

            for (int i = 0; i < blocks; ++i)
            {
                recorder.pushInput (channels, 2, 512);
                recorder.drainToFile();

                if (i == 50)
                {
                    // Exactly what a reconnect does: the device callback prepares again.
                    recorder.prepare (96000.0, 1024, 2);
                }
            }

            const auto expected = (double) (blocks * 512) / 44100.0;
            expectWithinAbsoluteError (recorder.getRecordedSeconds(), expected, 0.001,
                                       "the take still knows its own sample rate");

            expect (recorder.stop(), "stop: " + recorder.getLastError());
            expectWithinAbsoluteError (recorder.getRecordedSeconds(), expected, 0.001,
                                       "and still does after it is finalised");

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(),
                                                                                true));
            expect (reader != nullptr, "the file should be readable");
            if (reader != nullptr)
                expectWithinAbsoluteError (reader->sampleRate, 44100.0, 0.5,
                                           "the file header keeps the rate it was recorded at");
        }

        beginTest ("Preparing a new device while a take runs keeps the buffered audio");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::String error;
            const auto file = folder.getChildFile ("prepare-during-take.wav");
            expect (recorder.start (file, 0.0, 0, error), "start: " + error);

            juce::AudioBuffer<float> block (2, 512);
            const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };
            fillRamp (block, 0);

            for (int i = 0; i < 10; ++i)
                recorder.pushInput (channels, 2, 512);   // left in the ring, not drained

            recorder.prepare (48000.0, 256, 1);          // the device came back different
            recorder.drainToFile();

            expectEquals (recorder.getRecordedSamples(), (juce::int64) 10 * 512,
                          "the audio captured before the device change is still there");
            expect (recorder.stop(), "stop: " + recorder.getLastError());

            recorder.prepare (48000.0, 256, 1);
            juce::String error2;
            expect (recorder.start (folder.getChildFile ("after-change.wav"), 0.0, 0, error2),
                    "the next take uses the new configuration: " + error2);
            expectWithinAbsoluteError (recorder.getTakeSampleRate(), 48000.0, 0.5,
                                       "a new take does pick up the new rate");
            recorder.abort();
        }

        beginTest ("A take remembers the track it started on");
        {
            // The armed track used to be read when the take stopped. Disarming, or arming
            // another track, while recording moved the finished take to the wrong track.
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::String error;
            expect (recorder.start (folder.getChildFile ("track.wav"), 4.0, 2, error), "start: " + error);
            expectEquals (recorder.getStartTrackIndex(), 2, "the take belongs to track 2");

            juce::AudioBuffer<float> block (2, 512);
            const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };
            recorder.pushInput (channels, 2, 512);
            recorder.drainToFile();

            expectEquals (recorder.getStartTrackIndex(), 2, "and still does when it stops");
            expectWithinAbsoluteError (recorder.getStartPositionSteps(), 4.0, 1e-9,
                                       "the start position is the one it was given");
            expect (recorder.stop(), "stop: " + recorder.getLastError());
        }

        beginTest ("A take that could not be finalised is not reported as saved");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::String error;
            const auto file = folder.getChildFile ("write-fail.wav");
            expect (recorder.start (file, 0.0, 0, error), "start: " + error);

            juce::AudioBuffer<float> block (2, 512);
            const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };
            recorder.pushInput (channels, 2, 512);
            recorder.drainToFile();

            recorder.setWriteFailureForTests (true);
            recorder.pushInput (channels, 2, 512);
            recorder.drainToFile();   // the write fails here

            expect (! recorder.stop(), "a take with a failed write must not report success");
            expect (recorder.getLastError().isNotEmpty(), "and must say why");

            // The unread audio is still in the ring, so a caller that retries can still save it.
            expect (recorder.getDroppedSamples() == 0,
                    "a failed write is not the same as dropped audio");
        }

        beginTest ("A finished take reads back exactly the samples that were written");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::String error;
            const auto file = folder.getChildFile ("readback.wav");
            expect (recorder.start (file, 0.0, 0, error), "start: " + error);

            juce::AudioBuffer<float> block (2, 512);
            const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };

            for (int i = 0; i < 40; ++i)
            {
                fillRamp (block, (juce::int64) i * 512);
                recorder.pushInput (channels, 2, 512);
                recorder.drainToFile();
            }

            const auto written = recorder.getRecordedSamples();
            expect (recorder.stop(), "stop: " + recorder.getLastError());
            expect (recorder.getLastError().isEmpty(), "no error when everything worked");

            expectEquals (readBackSamples (file), written,
                          "the finalised header matches the audio");
            expectEquals (written, (juce::int64) 40 * 512, "and nothing was lost along the way");
        }

        beginTest ("Empty takes are refused and leave no file behind");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::String error;
            const auto file = folder.getChildFile ("empty.wav");
            expect (recorder.start (file, 0.0, 0, error), "start: " + error);
            expect (file.existsAsFile(), "the file exists while recording");

            // Stopped immediately: no audio ever arrived.
            expect (recorder.stop(), "stopping an empty take is not an error in itself");
            expectEquals (recorder.getRecordedSamples(), (juce::int64) 0, "nothing was captured");
            recorder.abort();   // what the engine does for a take this short
            expect (! file.existsAsFile(), "an empty take must not be left on disk");
        }

        beginTest ("Rapid start and stop cycles leave the recorder usable");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::AudioBuffer<float> block (2, 512);
            const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };
            fillRamp (block, 0);

            for (int cycle = 0; cycle < 12; ++cycle)
            {
                juce::String error;
                const auto file = folder.getChildFile ("cycle-" + juce::String (cycle) + ".wav");
                expect (recorder.start (file, 0.0, 0, error),
                        "cycle " + juce::String (cycle) + ": " + error);

                recorder.pushInput (channels, 2, 512);
                recorder.drainToFile();

                expect (recorder.stop(), "cycle " + juce::String (cycle) + ": "
                                           + recorder.getLastError());
                expectEquals (recorder.getRecordedSamples(), (juce::int64) 512,
                              "each cycle starts its count from zero");
                expectEquals (recorder.getDroppedSamples(), (juce::int64) 0,
                              "and no stale drops are carried over");
                expectEquals (readBackSamples (file), (juce::int64) 512,
                              "each file holds exactly its own block");
            }
        }

        beginTest ("A minute of audio is written without drift or gaps");
        {
            // Long takes are where a sample-count bug shows up: a per-block rounding error
            // turns into a visibly misplaced clip over a minute.
            InputRecorder recorder;
            recorder.prepare (44100.0, 441, 2);   // a deliberately odd block size

            juce::String error;
            const auto file = folder.getChildFile ("long.wav");
            expect (recorder.start (file, 0.0, 0, error), "start: " + error);

            juce::AudioBuffer<float> block (2, 441);
            const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };
            fillRamp (block, 0);

            const auto blocksPerSecond = 100;
            const auto seconds = 60;

            for (int i = 0; i < blocksPerSecond * seconds; ++i)
            {
                recorder.pushInput (channels, 2, 441);
                recorder.drainToFile();
            }

            const auto expected = (juce::int64) blocksPerSecond * seconds * 441;

            expectEquals (recorder.getRecordedSamples(), expected, "every sample arrived");
            expectEquals (recorder.getDroppedSamples(), (juce::int64) 0, "with no drops");
            expectWithinAbsoluteError (recorder.getRecordedSeconds(), 60.0, 0.01,
                                       "which is exactly a minute");

            expect (recorder.stop(), "stop: " + recorder.getLastError());
            expectEquals (readBackSamples (file), expected, "and the file agrees");
        }

        beginTest ("Pushing while stopping neither loses nor duplicates audio");
        {
            // The threaded version of the tail test, with the numbers checked against the
            // file: the same sample count in, the same count out, no repeats.
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                InputRecorder recorder;
                recorder.prepare (44100.0, 256, 2);

                juce::String error;
                const auto file = folder.getChildFile ("stress-" + juce::String (attempt) + ".wav");
                expect (recorder.start (file, 0.0, 0, error), "start: " + error);

                juce::AudioBuffer<float> block (2, 256);
                const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };
                fillRamp (block, 0);

                std::atomic<bool> go { true };
                std::atomic<juce::int64> pushed { 0 };

                std::thread callback ([&]
                {
                    while (go.load())
                    {
                        recorder.pushInput (channels, 2, 256);
                        pushed.fetch_add (256);

                        // Paced: the test is about the handshake when stopping, and an
                        // unthrottled loop writes a minute of audio per attempt.
                        juce::Thread::sleep (3);
                    }
                });

                for (int i = 0; i < 20; ++i)
                {
                    juce::Thread::sleep (2);
                    recorder.drainToFile();
                }

                go.store (false);
                callback.join();

                expect (recorder.stop(), "stop: " + recorder.getLastError());

                const auto total = recorder.getRecordedSamples() + recorder.getDroppedSamples();
                expectEquals (total, pushed.load(), "no sample vanished and none appeared twice");
            }
        }

        beginTest ("Aborting leaves nothing behind and stops the take");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::String error;
            const auto file = folder.getChildFile ("aborted.wav");
            expect (recorder.start (file, 0.0, 0, error), "start: " + error);

            juce::AudioBuffer<float> block (2, 512);
            const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };
            recorder.pushInput (channels, 2, 512);
            recorder.drainToFile();

            recorder.abort();

            expect (! recorder.isRecording(), "the take is over");
            expect (! file.existsAsFile(), "and the file is gone");
            expectEquals (recorder.getRecordedSamples(), (juce::int64) 0, "counters are reset");
        }

        // These tests write seconds of audio; leaving them behind fills the small tmpfs
        // that /tmp often is, which then breaks unrelated builds.
        folder.getParentDirectory().deleteRecursively();
    }
};

static RecordingReliabilityTests recordingReliabilityTests;
}
