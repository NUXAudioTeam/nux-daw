/* NUX DAW test plugin: a gain effect.

   This is a real VST3 built with juce_add_plugin. The NUX test suite scans it,
   loads it into a channel strip and checks the audio it produces, so the plugin
   hosting path is verified against an actual plugin binary rather than a stub.

   Delivery is deliberately trivial (multiply by the gain parameter) so the test
   can predict the exact output level. */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace nuxtest
{
class GainTestPlugin final : public juce::AudioProcessor
{
public:
    GainTestPlugin()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          parameters (*this, nullptr, "PARAMETERS",
                      { std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain", 1 },
                                                                     "Gain",
                                                                     juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f),
                                                                     1.0f) })
    {
    }

    //==============================================================================
    void prepareToPlay (double, int) override {}

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& out = layouts.getMainOutputChannelSet();
        const auto& in  = layouts.getMainInputChannelSet();

        // Mono or stereo in, matching out.
        return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const auto gain = parameters.getRawParameterValue ("gain")->load();
        buffer.applyGain (gain);

        ++blocksProcessed;
    }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return "NUX Test Gain"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override
    {
        auto state = parameters.copyState();
        std::unique_ptr<juce::XmlElement> xml (state.createXml());

        if (xml != nullptr)
            copyXmlToBinary (*xml, destData);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

        if (xml != nullptr)
            if (xml->hasTagName (parameters.state.getType()))
                parameters.replaceState (juce::ValueTree::fromXml (*xml));
    }

    /** How many blocks this instance has processed; used by the host tests. */
    std::atomic<int> blocksProcessed { 0 };

private:
    juce::AudioProcessorValueTreeState parameters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainTestPlugin)
};
}
