/* NUX DAW - built-in insert parameter tests.

   The insert editor writes p1/p2/p3 through a shared descriptor table. These tests
   pin down that table: its ranges, its defaults, the percent storage convention,
   and that a value written by the UI really does change the audio the DSP makes. */
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "../src/nux_core/InsertParameters.h"
#include "../src/nux_engine/ChannelStrip.h"

namespace nux
{
class InsertParameterTests final : public juce::UnitTest
{
public:
    InsertParameterTests() : juce::UnitTest ("NUX insert parameters", "NUX") {}

    void runTest() override
    {
        beginTest ("Every built-in effect exposes three parameters");
        {
            for (auto type : { InsertType::eq, InsertType::compressor, InsertType::delay, InsertType::reverb })
            {
                const auto descriptors = getInsertParameters (type);

                expectEquals ((int) descriptors.size(), 3);

                for (const auto& descriptor : descriptors)
                {
                    expect (descriptor.name.isNotEmpty(), "A parameter needs a label");
                    expect (descriptor.maximum > descriptor.minimum,
                            descriptor.name + " must have a positive range");
                    expect (descriptor.defaultValue >= descriptor.minimum
                                && descriptor.defaultValue <= descriptor.maximum,
                            descriptor.name + " has a default outside its own range");
                    expect (descriptor.interval > 0.0);
                }
            }

            // Empty and plugin slots have nothing generic to edit.
            expect (getInsertParameters (InsertType::none).empty());
            expect (getInsertParameters (InsertType::plugin).empty());
        }

        beginTest ("Descriptor ranges match what the DSP actually accepts");
        {
            // These numbers are the clamps inside ChannelStrip; if one side moves the
            // other has to move with it, or the UI will lie about what it controls.
            const auto eq = getInsertParameters (InsertType::eq);
            expectWithinAbsoluteError (eq[0].minimum, -18.0, 0.001);   // p1 low shelf
            expectWithinAbsoluteError (eq[0].maximum, 18.0, 0.001);
            expectWithinAbsoluteError (eq[1].minimum, 120.0, 0.001);   // p2 mid frequency
            expectWithinAbsoluteError (eq[1].maximum, 12000.0, 0.001);
            expect (eq[1].isLogarithmic, "A frequency control must be logarithmic to feel right");

            const auto comp = getInsertParameters (InsertType::compressor);
            expectWithinAbsoluteError (comp[0].minimum, -60.0, 0.001);
            expectWithinAbsoluteError (comp[1].maximum, 20.0, 0.001);

            const auto delay = getInsertParameters (InsertType::delay);
            expectWithinAbsoluteError (delay[0].maximum, 1500.0, 0.001);
            expectWithinAbsoluteError (delay[1].maximum, 92.0, 0.001);   // feedback is clamped at 0.92
        }

        beginTest ("Applying defaults produces a usable effect");
        {
            for (auto type : { InsertType::eq, InsertType::compressor, InsertType::delay, InsertType::reverb })
            {
                InsertSlot slot;
                applyInsertDefaults (slot, type);

                expect (slot.type == type);
                expect (! slot.bypassed, "A freshly added effect must not start bypassed");

                const auto descriptors = getInsertParameters (type);
                const auto values = readInsertValues (slot);

                for (size_t i = 0; i < descriptors.size(); ++i)
                    expectWithinAbsoluteError (values[i], descriptors[i].defaultValue, 0.001);
            }
        }

        beginTest ("Percent parameters round trip through their storage convention");
        {
            InsertSlot slot;
            applyInsertDefaults (slot, InsertType::delay);

            // The UI shows 0..100 %, the slot stores 0..1 because that is what the
            // DSP multiplies with. Writing 60 % must therefore store 0.6.
            writeInsertValues (slot, { 500.0, 60.0, 80.0 });   // 60 % feedback, 80 % mix

            const auto& descriptors = getInsertParameters (InsertType::delay);
            expect (descriptors[1].unit == "%", "Feedback is a percent parameter");

            expectWithinAbsoluteError ((double) slot.p2, 0.60, 0.001);
            expectWithinAbsoluteError ((double) slot.p3, 0.80, 0.001);
            expectWithinAbsoluteError ((double) slot.p1, 500.0, 0.001);   // ms stays ms

            // Reading gives back exactly what was written, in the same units.
            const auto values = readInsertValues (slot);
            expectWithinAbsoluteError (values[1], 60.0, 0.001);
            expectWithinAbsoluteError (values[2], 80.0, 0.001);
        }

        beginTest ("An edited parameter changes the audio");
        {
            // Delay mix at 0 must be dry; at 100 % the delayed signal is louder than
            // the dry signal after the first echo. This proves the slot values the
            // editor writes are the same ones the DSP reads.
            const auto renderWithMix = [] (float mixPercent)
            {
                ChannelStrip strip;
                strip.prepare (44100.0, 512, 2);
                strip.beginBlock (512);

                auto& params = strip.getParameters();
                InsertSlot slot;
                applyInsertDefaults (slot, InsertType::delay);
                writeInsertValues (slot, { 50.0, 0.0, (double) mixPercent });   // 50 ms, no feedback
                params.inserts[0] = slot;
                params.insertTypes[0].store ((int) InsertType::delay);

                juce::AudioBuffer<float> buffer (2, 512);

                // An impulse, then silence: with a wet mix the echo shows up later.
                float peakAfterImpulse = 0.0f;

                for (int block = 0; block < 8; ++block)
                {
                    buffer.clear();

                    if (block == 0)
                    {
                        buffer.setSample (0, 0, 1.0f);
                        buffer.setSample (1, 0, 1.0f);
                    }

                    strip.beginBlock (512);
                    strip.process (buffer, 1.0f);

                    if (block > 0)
                        peakAfterImpulse = juce::jmax (peakAfterImpulse, buffer.getMagnitude (0, 512));
                }

                return peakAfterImpulse;
            };

            const auto dry = renderWithMix (0.0f);
            const auto wet = renderWithMix (100.0f);

            expect (wet > dry + 0.05f,
                    "A 100 % wet delay must produce an audible echo (dry " + juce::String (dry)
                        + ", wet " + juce::String (wet) + ")");
        }

        beginTest ("Every effect has a readable name");
        {
            expectEquals (getInsertDisplayName (InsertType::eq), juce::String ("Equaliser"));
            expectEquals (getInsertDisplayName (InsertType::compressor), juce::String ("Compressor"));
            expectEquals (getInsertDisplayName (InsertType::delay), juce::String ("Delay"));
            expectEquals (getInsertDisplayName (InsertType::reverb), juce::String ("Reverb"));
            expectEquals (getInsertDisplayName (InsertType::none), juce::String ("Empty"));
        }

        beginTest ("Parameter values are formatted with their units");
        {
            const auto delay = getInsertParameters (InsertType::delay);
            expect (delay[0].formatValue (350.0).contains ("ms"));
            expect (delay[2].formatValue (25.0).contains ("%"));
            expect (delay[2].formatValue (25.0).contains ("25"));
        }
    }
};

static InsertParameterTests insertParameterTests;
}
