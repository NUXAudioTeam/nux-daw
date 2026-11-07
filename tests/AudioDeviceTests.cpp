/* NUX DAW - test tone, metering and device recovery.

   These cover the parts of Phase 02 that do not need real hardware: the tone
   generator's own contract (frequency, ramp, silence), the output metering the
   settings panel reads, and the reopen path taken when a device disappears.

   Nothing here proves that a physical device makes a sound - that is what the
   manual hardware checklist in docs/ is for. */
#include "../src/nux_engine/AudioEngine.h"
#include "../src/nux_engine/TestTone.h"
#include "../src/nux_core/ProjectDocument.h"

#include <cmath>
#include <memory>

namespace nux::tests
{
class AudioDeviceTests final : public juce::UnitTest
{
public:
    AudioDeviceTests() : juce::UnitTest ("NUX audio device", "NUX") {}

    /** Measures the zero-crossing frequency of a rendered buffer. */
    static double measureFrequency (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        const auto* data = buffer.getReadPointer (0);
        const auto n = buffer.getNumSamples();

        int crossings = 0;
        int firstCrossing = -1;
        int lastCrossing = -1;

        for (int i = 1; i < n; ++i)
        {
            if (data[i - 1] <= 0.0f && data[i] > 0.0f)
            {
                if (firstCrossing < 0)
                    firstCrossing = i;

                lastCrossing = i;
                ++crossings;
            }
        }

        if (crossings < 2 || lastCrossing <= firstCrossing)
            return 0.0;

        const auto cycles = crossings - 1;
        const auto seconds = (double) (lastCrossing - firstCrossing) / sampleRate;

        return seconds > 0.0 ? cycles / seconds : 0.0;
    }

    void runTest() override
    {
        beginTest ("The test tone is a 440 Hz sine on both channels");
        {
            TestToneGenerator tone;
            tone.prepare (44100.0);
            tone.setFrequency (440.0);
            tone.setLevel (0.5f);

            juce::AudioBuffer<float> buffer (2, 44100);
            buffer.clear();

            // Render enough blocks for the ramp to reach full level before measuring.
            for (int block = 0; block < 90; ++block)
            {
                juce::AudioBuffer<float> chunk (2, 512);
                chunk.clear();
                tone.render (chunk, 512);

                if (block >= 80)
                    buffer.copyFrom (0, (block - 80) * 512, chunk, 0, 0, 512),
                    buffer.copyFrom (1, (block - 80) * 512, chunk, 1, 0, 512);
            }

            const auto frequency = measureFrequency (buffer, 44100.0);
            expectWithinAbsoluteError (frequency, 440.0, 5.0,
                                       "The test tone should be 440 Hz (measured "
                                         + juce::String (frequency, 1) + " Hz)");

            // Both channels carry the same signal.
            expectWithinAbsoluteError (buffer.getMagnitude (0, 0, buffer.getNumSamples()),
                                       buffer.getMagnitude (1, 0, buffer.getNumSamples()), 0.001f,
                                       "The test tone should be identical on both channels");

            expect (buffer.getMagnitude (0, 0, buffer.getNumSamples()) > 0.1f,
                    "The test tone should be audible at 50% level");
        }

        beginTest ("The test tone does not clip at full level");
        {
            TestToneGenerator tone;
            tone.prepare (48000.0);
            tone.setLevel (1.0f);

            juce::AudioBuffer<float> buffer (2, 48000);
            buffer.clear();

            for (int block = 0; block < 100; ++block)
            {
                juce::AudioBuffer<float> chunk (2, 480);
                chunk.clear();
                tone.render (chunk, 480);

                if (block >= 10)
                    buffer.copyFrom (0, (block - 10) * 480, chunk, 0, 0, 480);
            }

            expect (buffer.getMagnitude (0, 0, buffer.getNumSamples()) < 0.999f,
                    "A full level test tone must stay below 0 dBFS");
        }

        beginTest ("The tone ramps in and out instead of clicking");
        {
            TestToneGenerator tone;
            tone.prepare (44100.0);
            tone.setLevel (1.0f);

            juce::AudioBuffer<float> firstBlock (2, 512);
            firstBlock.clear();
            tone.render (firstBlock, 512);

            // The very first sample must be near zero: switching a sine on at full
            // amplitude would produce a click.
            expect (std::abs (firstBlock.getSample (0, 0)) < 0.01f,
                    "The tone must fade in, not jump to full level (first sample "
                      + juce::String (firstBlock.getSample (0, 0), 4) + ")");

            for (int i = 0; i < 200; ++i)
            {
                juce::AudioBuffer<float> chunk (2, 512);
                chunk.clear();
                tone.render (chunk, 512);
            }

            // Switching it off must fade out rather than stop dead.
            tone.setLevel (0.0f);

            auto lastSample = 0.0f;

            for (int i = 0; i < 200; ++i)
            {
                juce::AudioBuffer<float> chunk (2, 512);
                chunk.clear();
                tone.render (chunk, 512);
                lastSample = chunk.getMagnitude (0, 512);

                if (lastSample <= 0.0001f)
                    break;
            }

            expect (lastSample <= 0.0001f, "The tone must reach silence after being switched off");
            expect (! tone.isActive(), "A muted tone must report itself as inactive");
        }

        beginTest ("A stopped test tone contributes nothing to the mix");
        {
            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            ProjectState project = ProjectState::createDefault();
            engine.setProjectState (&project);
            engine.prepareForOfflineRendering (44100.0, 512, 2);

            juce::AudioBuffer<float> output (2, 512);

            auto renderPeak = [&engine, &output]
            {
                auto peak = 0.0f;

                for (int i = 0; i < 20; ++i)
                {
                    output.clear();
                    engine.renderBlockForTest (output, 512);
                    peak = juce::jmax (peak, output.getMagnitude (0, 512));
                }

                return peak;
            };

            expect (renderPeak() < 0.01f, "Nothing should be sounding before the tone starts");

            engine.setTestToneEnabled (true);
            expect (engine.isTestToneEnabled(), "The engine should report the tone as running");

            const auto tonePeak = renderPeak();
            expect (tonePeak > 0.05f,
                    "The test tone must reach the output (peak " + juce::String (tonePeak, 4)
                      + ", level " + juce::String (engine.getTestToneLevel(), 3)
                      + ", out peak " + juce::String (engine.getOutputPeak (0), 4) + ")");

            engine.setTestToneEnabled (false);
            for (int i = 0; i < 40; ++i)
                output.clear(), engine.renderBlockForTest (output, 512);

            expect (renderPeak() < 0.01f, "The output must be silent again after stopping the tone");
            expect (! engine.isTestToneEnabled(), "A stopped tone must report itself as off");
        }

        beginTest ("The output meter follows what was actually sent");
        {
            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            ProjectState project = ProjectState::createDefault();
            engine.setProjectState (&project);
            engine.prepareForOfflineRendering (44100.0, 512, 2);

            juce::AudioBuffer<float> output (2, 512);

            for (int i = 0; i < 10; ++i)
            {
                output.clear();
                engine.renderBlockForTest (output, 512);
            }

            expect (engine.getOutputPeak (0) < 0.01f, "A silent engine must meter silence");

            engine.setTestToneEnabled (true);

            for (int i = 0; i < 30; ++i)
            {
                output.clear();
                engine.renderBlockForTest (output, 512);
            }

            const auto metered = juce::jmax (engine.getOutputPeak (0), engine.getOutputPeak (1));
            expect (metered > 0.05f,
                    "The output meter must show the tone (metered " + juce::String (metered, 4) + ")");

            // Panic must silence the tone and clear the meters, not leave them lit.
            engine.panic();
            expect (! engine.isTestToneEnabled(), "Panic must stop the test tone");
            expect (engine.getOutputPeak (0) < 0.01f, "Panic must clear the output meter");
        }

        beginTest ("The engine reports a lost device instead of pretending to play");
        {
            auto enginePtr = std::make_unique<AudioEngine>();
            auto& engine = *enginePtr;
            ProjectState project = ProjectState::createDefault();
            engine.setProjectState (&project);

            expect (! engine.isDeviceLost(), "A fresh engine has not lost a device");

            // The audioDeviceError callback is what JUCE calls when a driver fails.
            engine.audioDeviceError ("test device failure");
            expect (engine.isDeviceLost(), "A device error must be reported as a lost device");
            expect (engine.getLastDeviceError().contains ("test device failure"),
                    "The device error text must be kept for the UI");

            // Reopening on a machine with no device must fail cleanly, not crash.
            const auto reopened = engine.tryReopenDevice();
            expect (! reopened || engine.isAudioAvailable(),
                    "Reopening either works or reports that it did not");
        }
    }
};

static AudioDeviceTests audioDeviceTests;
}
