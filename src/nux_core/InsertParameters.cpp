#include "InsertParameters.h"

namespace nux
{
juce::String InsertParameterDescriptor::formatValue (double value) const
{
    // Big ranges read better without decimals, small ones need them.
    const auto decimals = maximum - minimum > 100.0 ? 0 : (maximum - minimum > 4.0 ? 1 : 2);

    auto text = juce::String (value, decimals);

    if (unit.isNotEmpty())
        text << " " << unit;

    return text;
}

std::vector<InsertParameterDescriptor> getInsertParameters (InsertType type)
{
    // The meanings below match exactly what ChannelStrip::process* interprets.
    switch (type)
    {
        case InsertType::eq:
            return {
                { "LOW",  "dB", -18.0, 18.0, 0.0,    0.5 },                    // p1 low shelf gain
                { "MID",  "Hz", 120.0, 12000.0, 1000.0, 10.0, true },          // p2 mid frequency
                { "HIGH", "dB", -18.0, 18.0, 0.0,    0.5 }                     // p3 high shelf gain
            };

        case InsertType::compressor:
            return {
                { "THRESH", "dB", -60.0, 0.0, -18.0, 0.5 },                    // p1 threshold
                { "RATIO",  "",   1.0, 20.0, 3.0,   0.1 },                     // p2 ratio
                { "ATTACK", "ms", 0.1, 200.0, 12.0,  0.1, true }                // p3 attack
            };

        case InsertType::delay:
            return {
                { "TIME", "ms", 5.0, 1500.0, 350.0, 1.0 },                     // p1 delay time
                { "FB",   "%",  0.0, 92.0,   35.0,  1.0 },                     // p2 feedback
                { "MIX",  "%",  0.0, 100.0,  25.0,  1.0 }                      // p3 wet mix
            };

        case InsertType::reverb:
            return {
                { "SIZE",  "%", 0.0, 100.0, 70.0, 1.0 },                       // p1 room size
                { "DAMP",  "%", 0.0, 100.0, 45.0, 1.0 },                       // p2 damping
                { "MIX",   "%", 0.0, 100.0, 22.0, 1.0 }                        // p3 wet level
            };

        case InsertType::plugin:
        case InsertType::none:
        default:
            return {};
    }
}

void applyInsertDefaults (InsertSlot& slot, InsertType type)
{
    slot.type = type;
    slot.bypassed = false;
    slot.p1 = slot.p2 = slot.p3 = 0.0f;

    // plugin / none carry no generic parameters.
    if (type == InsertType::plugin || type == InsertType::none)
        return;

    const auto descriptors = getInsertParameters (type);

    std::vector<double> defaults;

    for (const auto& descriptor : descriptors)
        defaults.push_back (descriptor.defaultValue);

    writeInsertValues (slot, defaults);
}

juce::String getInsertDisplayName (InsertType type)
{
    switch (type)
    {
        case InsertType::eq:         return "Equaliser";
        case InsertType::compressor: return "Compressor";
        case InsertType::delay:      return "Delay";
        case InsertType::reverb:     return "Reverb";
        case InsertType::plugin:     return "Plugin";
        case InsertType::none:
        default:                     return "Empty";
    }
}

std::vector<double> readInsertValues (const InsertSlot& slot)
{
    // Symmetric with writeInsertValues: the caller always works in the units the
    // descriptors describe (so a "%" parameter reads 0..100, not 0..1).
    const auto descriptors = getInsertParameters (slot.type);

    auto fromStorage = [&descriptors] (size_t index, float value)
    {
        if (index < descriptors.size())
        {
            const auto& descriptor = descriptors[index];

            if (descriptor.unit == "%")
                return (double) value * 100.0;
        }

        return (double) value;
    };

    return { fromStorage (0, slot.p1), fromStorage (1, slot.p2), fromStorage (2, slot.p3) };
}

void writeInsertValues (InsertSlot& slot, const std::vector<double>& values)
{
    // Percent-style descriptors are stored as 0..1 inside the slot, because that is
    // what the DSP expects; the descriptor range is only for the UI.
    const auto descriptors = getInsertParameters (slot.type);

    auto toStorage = [&descriptors] (size_t index, double value)
    {
        if (index < descriptors.size())
        {
            const auto& descriptor = descriptors[index];

            // Percent parameters are always stored as a 0..1 fraction, whatever their
            // upper bound is (delay feedback tops out at 92 %, for example).
            if (descriptor.unit == "%")
                return (float) (value / 100.0);
        }

        return (float) value;
    };

    if (values.size() > 0) slot.p1 = toStorage (0, values[0]);
    if (values.size() > 1) slot.p2 = toStorage (1, values[1]);
    if (values.size() > 2) slot.p3 = toStorage (2, values[2]);
}
}
