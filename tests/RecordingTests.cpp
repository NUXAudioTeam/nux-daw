/* NUX DAW - audio input recording tests.

   The recorder is the one part of the engine that touches both the audio thread and
   the disk, so these tests check the contract between them: input pushed from the
   audio thread ends up in the file, the take length is right, the file is a valid
   WAV that can be read back and turned into a clip, and nothing is allocated or
   written when recording is not active. */
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/nux_engine/InputRecorder.h"

#include <cmath>

namespace nux
{
class RecordingTests final : public juce::UnitTest
{
public:
    RecordingTests() : juce::UnitTest ("NUX input recording", "NUX") {}

    static juce::File makeTempFolder()
    {
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("nux-recording-tests");
        folder.createDirectory();
        return folder;
    }

    /** Pushes `blocks` blocks of a sine tone through the recorder, then drains. */
    static void feedTone (InputRecorder& recorder, int blocks, int blockSize, double frequency)
    {
        juce::AudioBuffer<float> input (2, blockSize);
        const float* channels[2] = { input.getReadPointer (0), input.getReadPointer (1) };

        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sampleIndex = block * blockSize + i;
                const auto value = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                            * frequency * sampleIndex / 44100.0);
                input.setSample (0, i, value);
                input.setSample (1, i, value);
            }

            recorder.pushInput (channels, 2, blockSize);
            recorder.drainToFile();
        }
    }

    void runTest() override
    {
        const auto folder = makeTempFolder();

        beginTest ("A take is written to a readable WAV file");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            const auto file = folder.getChildFile ("take-1.wav");
            juce::String error;

            expect (recorder.start (file, 4.0, error), "Recording failed to start: " + error);
            expect (recorder.isRecording());
            expect (file.existsAsFile());

            // One second of audio.
            const auto blocks = (int) std::ceil (44100.0 / 512.0);
            feedTone (recorder, blocks, 512, 440.0);

            recorder.stop();

            expect (! recorder.isRecording());
            expect (file.existsAsFile(), "The take file is missing");

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            expect (reader != nullptr, "The recorded file cannot be read back");

            if (reader == nullptr)
                return;

            expectEquals ((int) reader->numChannels, 2);
            expectWithinAbsoluteError (reader->sampleRate, 44100.0, 1.0);
            expect (reader->bitsPerSample == 24 || reader->bitsPerSample == 32,
                    "A take should be written at 24-bit (got " + juce::String (reader->bitsPerSample) + ")");

            // The length must match roughly one second of pushed audio.
            expectWithinAbsoluteError ((double) reader->lengthInSamples, 44100.0, 1500.0);

            juce::AudioBuffer<float> readBack (2, (int) reader->lengthInSamples);
            reader->read (&readBack, 0, (int) reader->lengthInSamples, 0, true, true);

            // And it must contain the tone we pushed in, not silence.
            expect (readBack.getMagnitude (0, (int) reader->lengthInSamples) > 0.2f,
                    "The recorded take is silent");
        }

        beginTest ("The take remembers where it started on the timeline");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 256, 2);

            const auto file = folder.getChildFile ("take-2.wav");
            juce::String error;

            expect (recorder.start (file, 12.5, error), error);
            feedTone (recorder, 10, 256, 220.0);
            recorder.stop();

            expectWithinAbsoluteError (recorder.getStartPositionSteps(), 12.5, 0.001);
            expect (recorder.getRecordedSeconds() > 0.0);
            expectEquals (recorder.getRecordingFile().getFileName(), juce::String ("take-2.wav"));
        }

        beginTest ("Pushing audio while not recording writes nothing");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            const auto file = folder.getChildFile ("take-3.wav");
            file.deleteFile();

            // No start(): the recorder must ignore the input entirely.
            feedTone (recorder, 8, 512, 440.0);

            expect (! file.existsAsFile(), "Audio was written without a take being started");
            expectEquals ((int) recorder.getRecordedSamples(), 0);
        }

        beginTest ("A mono input is captured to a stereo take");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            const auto file = folder.getChildFile ("take-mono.wav");
            juce::String error;
            expect (recorder.start (file, 0.0, error), error);

            juce::AudioBuffer<float> mono (1, 512);

            for (int i = 0; i < 512; ++i)
                mono.setSample (0, i, 0.25f);

            const float* channels[1] = { mono.getReadPointer (0) };

            for (int block = 0; block < 20; ++block)
            {
                recorder.pushInput (channels, 1, 512);
                recorder.drainToFile();
            }

            recorder.stop();

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            expect (reader != nullptr);

            if (reader == nullptr)
                return;

            expectEquals ((int) reader->numChannels, 2);

            juce::AudioBuffer<float> readBack (2, 512);
            reader->read (&readBack, 0, 512, 0, true, true);

            // Both channels carry the mono signal.
            expectWithinAbsoluteError (readBack.getSample (0, 100), 0.25f, 0.01f);
            expectWithinAbsoluteError (readBack.getSample (1, 100), 0.25f, 0.01f);
        }

        beginTest ("A silent input is reported as silent");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::AudioBuffer<float> silent (2, 512);
            silent.clear();
            const float* channels[2] = { silent.getReadPointer (0), silent.getReadPointer (1) };

            const auto file = folder.getChildFile ("take-silent.wav");
            juce::String error;
            expect (recorder.start (file, 0.0, error), error);

            for (int block = 0; block < 40; ++block)
                recorder.pushInput (channels, 0, 512);   // zero input channels

            expect (recorder.getInputLevel() < 0.001f,
                    "A take with no input must report no level");

            recorder.stop();
        }

        beginTest ("Aborting a take removes the file");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            const auto file = folder.getChildFile ("take-abort.wav");
            juce::String error;
            expect (recorder.start (file, 0.0, error), error);
            expect (file.existsAsFile());

            recorder.abort();

            expect (! recorder.isRecording());
            expect (! file.existsAsFile(), "An aborted take must not leave a file behind");
        }

        beginTest ("Starting a second take while one is running is refused");
        {
            InputRecorder recorder;
            recorder.prepare (44100.0, 512, 2);

            juce::String error;
            expect (recorder.start (folder.getChildFile ("take-a.wav"), 0.0, error), error);

            juce::String secondError;
            expect (! recorder.start (folder.getChildFile ("take-b.wav"), 0.0, secondError));
            expect (secondError.isNotEmpty(), "The refusal must explain itself");

            recorder.stop();
        }
    }
};

static RecordingTests recordingTests;
}
