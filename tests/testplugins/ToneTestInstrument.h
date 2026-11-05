/* NUX DAW test plugin: a minimal instrument.

   Generates a continuous sine tone so the host tests can prove that MIDI notes
   reach a hosted instrument and that its audio ends up in the mixdown. The
   frequency follows the last note-on, and note-off silences it. */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace nuxtest
{
class ToneTestInstrument final : public juce::AudioProcessor
{
public:
    ToneTestInstrument()
        : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    void prepareToPlay (double newSampleRate, int) override
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        phase = 0.0;
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOn())
            {
                frequency = juce::MidiMessage::getMidiNoteInHertz (message.getNoteNumber());
                active = true;
                notesReceived.fetch_add (1);
            }
            else if (message.isNoteOff() || message.isAllNotesOff())
            {
                active = false;
                notesReceived.fetch_add (1);
            }
        }

        if (! active)
            return;

        const auto increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto value = (float) (0.5 * std::sin (phase));
            phase += increment;

            if (phase > juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, value);
        }
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return "NUX Test Tone"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    std::atomic<int> notesReceived { 0 };

private:
    double sampleRate = 44100.0;
    double phase = 0.0;
    double frequency = 440.0;
    bool active = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneTestInstrument)
};
}
