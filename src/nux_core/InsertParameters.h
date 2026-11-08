/* NUX DAW - the editable parameters of a built-in insert.

   The three generic insert parameters (p1, p2, p3) mean something different for
   each effect. This header is the single place that defines those meanings, so the
   UI editor, the DSP layer and the tests all agree on ranges, defaults and units
   instead of each hard-coding its own numbers.

   It lives in nux_core because it is data, not audio: no JUCE audio module needed,
   so it can be unit tested without a device. */
#pragma once

#include <juce_core/juce_core.h>
#include "../nux_core/TrackModel.h"

namespace nux
{
struct InsertParameterDescriptor
{
    juce::String name;         // short label, e.g. "LOW"
    juce::String unit;         // "dB", "Hz", "ms", "%" or empty
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    double interval = 0.01;    // step size for the control
    bool isLogarithmic = false;

    /** Formats a value for the readout, without the unit. */
    juce::String formatValue (double value) const;
};

/** The parameters an insert exposes, in p1, p2, p3 order.

    Returns an empty vector for InsertType::none and InsertType::plugin: a hosted
    plugin draws its own controls and a plugin's audio block is only ever fed from
    an insert whose type is none, so neither needs descriptors. */
std::vector<InsertParameterDescriptor> getInsertParameters (InsertType type);

/** The values an insert should start with when it is first placed. */
void applyInsertDefaults (InsertSlot& slot, InsertType type);

/** Human readable name of an effect, for menus and strip labels. */
juce::String getInsertDisplayName (InsertType type);

/** Reads a slot's parameters in descriptor order, expressed in the descriptors'
    own units (a "%" parameter therefore reads 0..100, even though the slot stores
    0..1 for the DSP). */
std::vector<double> readInsertValues (const InsertSlot& slot);

/** Writes descriptor-order values, in descriptor units, back into a slot. */
void writeInsertValues (InsertSlot& slot, const std::vector<double>& values);
}
