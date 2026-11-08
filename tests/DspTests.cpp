#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../src/nux_core/TrackModel.h"

namespace nux
{
class DspTests final : public juce::UnitTest
{
public:
    DspTests() : juce::UnitTest ("NUX DSP building blocks", "NUX") {}

    void runTest() override
    {
        beginTest ("Synthesiser renders audio for a note-on");
        {
            struct TestSound final : public juce::SynthesiserSound
            {
                bool appliesToNote (int) override { return true; }
                bool appliesToChannel (int) override { return true; }
            };

            juce::Synthesiser synth;
            synth.addSound (new TestSound());

            // A deterministic test voice: a 440 Hz sine that always plays.
            struct TestVoice final : public juce::SynthesiserVoice
            {
                bool canPlaySound (juce::SynthesiserSound*) override { return true; }
                void startNote (int, float, juce::SynthesiserSound*, int) override { phase = 0.0; }
                void stopNote (float, bool) override { clearCurrentNote(); }
                void pitchWheelMoved (int) override {}
                void controllerMoved (int, int) override {}
                void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
                {
                    for (int i = 0; i < numSamples; ++i)
                    {
                        const auto value = (float) std::sin (phase) * 0.5f;
                        phase += juce::MathConstants<double>::twoPi * 440.0 / getSampleRate();

                        for (int ch = 0; ch < out.getNumChannels(); ++ch)
                            out.addSample (ch, startSample + i, value);
                    }
                }
                double phase = 0.0;
            };

            synth.addVoice (new TestVoice());
            synth.setCurrentPlaybackSampleRate (44100.0);

            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);

            synth.renderNextBlock (buffer, midi, 0, 512);

            const auto magnitude = buffer.getMagnitude (0, 512);
            expect (magnitude > 0.01f, "The synthesiser should produce signal after a note-on");
            expect (buffer.getMagnitude (0, 512) <= 1.0f, "Output must stay in range");
        }

        beginTest ("IIR low shelf raises low frequency energy");
        {
            constexpr double sampleRate = 48000.0;
            constexpr int numSamples = 8192;

            auto measure = [&] (float gainDb)
            {
                juce::dsp::IIR::Filter<float> filter;
                filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                    sampleRate, 220.0f, 0.7f, dbToGain (gainDb));

                juce::AudioBuffer<float> buffer (1, numSamples);

                for (int i = 0; i < numSamples; ++i)
                    buffer.setSample (0, i, std::sin (juce::MathConstants<double>::twoPi * 80.0
                                                      * i / sampleRate));

                juce::dsp::AudioBlock<float> block (buffer);
                juce::dsp::ProcessContextReplacing<float> context (block);
                filter.process (context);

                return buffer.getRMSLevel (0, numSamples / 2, numSamples / 2);
            };

            const auto flat = measure (0.0f);
            const auto boosted = measure (12.0f);

            expectWithinAbsoluteError (boosted / flat, 3.98f, 0.6f);   // +12 dB = x3.98
        }

        beginTest ("Compressor reduces peaks above the threshold");
        {
            juce::dsp::Compressor<float> compressor;
            compressor.prepare ({ 44100.0, 512, 1 });
            compressor.setThreshold (-24.0f);
            compressor.setRatio (8.0f);
            compressor.setAttack (2.0f);
            compressor.setRelease (50.0f);

            juce::AudioBuffer<float> buffer (1, 4096);

            for (int i = 0; i < 4096; ++i)
                buffer.setSample (0, i, 0.9f * std::sin (juce::MathConstants<double>::twoPi * 200.0 * i / 44100.0));

            const auto before = buffer.getMagnitude (0, 0, 4096);

            for (int block = 0; block < 8; ++block)
            {
                juce::dsp::AudioBlock<float> audioBlock (buffer);
                juce::dsp::ProcessContextReplacing<float> context (audioBlock);
                compressor.process (context);
            }

            const auto after = buffer.getMagnitude (0, 0, 4096);

            expect (after < before, "The compressor should reduce the peak level");
            expect (after > 0.0001f, "The compressor must not mute the signal");
        }

        beginTest ("Delay line returns the input after the set delay");
        {
            juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> delay { 1024 };
            delay.prepare ({ 44100.0, 512, 1 });
            delay.setDelay (100.0f);

            delay.pushSample (0, 1.0f);

            float peak = 0.0f;
            int peakIndex = -1;

            for (int i = 0; i < 200; ++i)
            {
                const auto value = delay.popSample (0);
                delay.pushSample (0, 0.0f);

                if (std::abs (value) > peak)
                {
                    peak = std::abs (value);
                    peakIndex = i;
                }
            }

            expect (peak > 0.9f, "The impulse should come back out of the delay");
            expectEquals (peakIndex, 100);
        }

        beginTest ("SmoothedValue reaches its target");
        {
            juce::SmoothedValue<float> smoothed;
            smoothed.reset (44100.0, 0.005);
            smoothed.setCurrentAndTargetValue (0.0f);
            smoothed.setTargetValue (1.0f);

            for (int i = 0; i < 1000; ++i)
                smoothed.getNextValue();

            expectWithinAbsoluteError (smoothed.getCurrentValue(), 1.0f, 0.001f);
        }
    }
};

static DspTests dspTests;
}
