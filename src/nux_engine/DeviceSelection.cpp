#include "DeviceSelection.h"
#include "AudioDeviceProbe.h"

namespace nux
{
namespace
{
/** Lower case, letters and digits only. Used to compare cable halves. */
juce::String simplify (const juce::String& text)
{
    juce::String out;

    for (auto c : text.toLowerCase())
        if (juce::CharacterFunctions::isLetterOrDigit (c))
            out += juce::String::charToString (c);

    return out;
}
}

juce::String DeviceOption::getLabel() const
{
    auto label = deviceName;

    if (isVirtual)
        label += " [virtual]";

    return label;
}

std::vector<DeviceOption> buildDeviceOptions (const juce::String& typeName,
                                              const juce::StringArray& outputNames,
                                              const juce::StringArray& inputNames)
{
    std::vector<DeviceOption> options;

    const auto findOrAdd = [&options, &typeName] (const juce::String& name) -> DeviceOption&
    {
        for (auto& option : options)
            if (option.deviceName == name)
                return option;

        DeviceOption fresh;
        fresh.typeName = typeName;
        fresh.deviceName = name;
        fresh.isVirtual = isVirtualCableName (name);
        options.push_back (fresh);
        return options.back();
    };

    for (const auto& name : outputNames)
        if (name.isNotEmpty())
            findOrAdd (name).canOutput = true;

    for (const auto& name : inputNames)
        if (name.isNotEmpty())
            findOrAdd (name).canInput = true;

    return options;
}

std::vector<DeviceOption> listDeviceOptions (juce::AudioDeviceManager& manager)
{
    std::vector<DeviceOption> options;

    for (auto* type : manager.getAvailableDeviceTypes())
    {
        if (type == nullptr)
            continue;

        // Scanning is what makes the names real; without it a type reports nothing.
        type->scanForDevices();

        const auto outputs = type->getDeviceNames (false);
        const auto inputs  = type->getDeviceNames (true);
        const auto typeName = type->getTypeName();

        for (auto& option : buildDeviceOptions (typeName, outputs, inputs))
        {
            // A name offered by two drivers is two choices, not one.
            bool alreadyListed = false;

            for (auto& existing : options)
                if (existing.typeName == option.typeName && existing.deviceName == option.deviceName)
                    alreadyListed = true;

            if (! alreadyListed)
                options.push_back (option);
        }
    }

    return options;
}

juce::String getCableFamilyKey (const juce::String& deviceName)
{
    auto key = simplify (deviceName);

    // VB-CABLE and friends describe the two halves as Input and Output. Removing those
    // words is what lets the playback half and the capture half be recognised as a pair.
    key = key.replace ("input", "").replace ("output", "");

    // "cable" on its own is the family name; the parenthetical is already stripped by
    // simplify() keeping letters, so trim the leading generic words as well.
    return key;
}

VirtualCablePair findVirtualCablePair (const std::vector<DeviceOption>& options)
{
    VirtualCablePair pair;

    for (const auto& output : options)
    {
        if (! output.isVirtual || ! output.canOutput)
            continue;

        const auto key = getCableFamilyKey (output.deviceName);

        if (key.isEmpty())
            continue;

        for (const auto& input : options)
        {
            if (! input.isVirtual || ! input.canInput)
                continue;

            if (input.typeName != output.typeName)
                continue;

            if (getCableFamilyKey (input.deviceName) != key)
                continue;

            // The playback and capture halves must be different devices. A single device
            // that advertises both is a driver quirk, not a cable.
            if (input.deviceName == output.deviceName)
                continue;

            pair.found = true;
            pair.typeName = output.typeName;
            pair.outputName = output.deviceName;
            pair.inputName = input.deviceName;
            pair.explanation = "NUX will play into \"" + output.deviceName + "\" and listen on \""
                             + input.deviceName + "\". A virtual cable carries samples inside "
                               "the operating system, so a clean result proves the routing and "
                               "the engine path. It says nothing about a real converter, a "
                               "speaker or a microphone.";
            return pair;
        }
    }

    pair.explanation = "No matching virtual cable was found. A round trip without hardware "
                       "needs both halves of a cable such as VB-CABLE: install it, then "
                       "reopen this panel. Until then the round trip stays SKIPPED, which is "
                       "not a pass.";
    return pair;
}

DeviceSelection makeSelection (const DeviceChoice& choice,
                               const std::vector<DeviceOption>& options,
                               bool requireInput)
{
    DeviceSelection selection;

    const auto findOption = [&options] (const juce::String& name, bool wantOutput)
    {
        for (const auto& option : options)
            if (option.deviceName == name && option.worksFor (wantOutput))
                return option;

        return DeviceOption {};
    };

    const auto output = findOption (choice.outputName, true);

    if (choice.outputName.isEmpty())
    {
        selection.error = "No output device is selected, so there is nothing to play through.";
        selection.explanation = "Pick the device NUX should play into before running a round trip.";
        return selection;
    }

    if (output.deviceName.isEmpty())
    {
        selection.error = "\"" + choice.outputName + "\" is not an output device on this "
                          "machine. It may have been unplugged, or the driver may have "
                          "renamed it.";
        selection.explanation = "Nothing was opened. Reopen the panel to refresh the list.";
        return selection;
    }

    DeviceOption input;

    if (requireInput)
    {
        if (choice.inputName.isEmpty())
        {
            selection.error = "No input device is selected, so nothing can come back in. A "
                              "round trip needs both directions.";
            selection.explanation = "Pick the recording side of the same cable or interface.";
            return selection;
        }

        input = findOption (choice.inputName, false);

        if (input.deviceName.isEmpty())
        {
            selection.error = "\"" + choice.inputName + "\" is not an input device on this "
                              "machine. It may have been unplugged.";
            selection.explanation = "Nothing was opened. Reopen the panel to refresh the list.";
            return selection;
        }
    }

    selection.usesVirtualCable = output.isVirtual || (requireInput && input.isVirtual);

    if (output.typeName != (requireInput ? input.typeName : output.typeName))
    {
        selection.error = "The output (\"" + output.typeName + "\") and input (\""
                        + input.typeName + "\") come from different drivers. A single device "
                          "cannot be open on two drivers at once, so the round trip would "
                          "capture silence.";
        selection.explanation = "Choose both sides from the same driver.";
        return selection;
    }

    selection.setup.outputDeviceName = output.deviceName;
    selection.setup.inputDeviceName = requireInput ? input.deviceName : juce::String();
    selection.setup.useDefaultOutputChannels = true;
    selection.setup.useDefaultInputChannels = true;
    selection.valid = true;

    if (selection.usesVirtualCable)
        selection.explanation = "This pairing uses a virtual cable. A pass proves the routing "
                                "and the engine path; it does not prove a converter, a "
                                "speaker or a microphone.";
    else
        selection.explanation = "This pairing uses real hardware. A pass here is the strongest "
                                "result the lab can produce.";

    return selection;
}
}
