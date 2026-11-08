#include "AudioDeviceProbe.h"

namespace nux
{
namespace
{
/** Names that identify a virtual loopback cable rather than a sound card.

    Kept deliberately broad: a user who installs VB-CABLE should see NUX say "that is
    a virtual device", whatever exact variant they installed. */
const char* const virtualCableKeywords[] =
{
    "cable",            // VB-CABLE, CABLE-A, CABLE-B
    "voicemeeter",      // VB-Audio Voicemeeter
    "virtual",          // "Virtual Audio Cable", "Virtual Desktop"
    "vb-audio",
    "hi-fi cable",
    "loopback",
    "null audio",
    "dummy"
};
}

bool isVirtualCableName (const juce::String& deviceName)
{
    for (const auto* keyword : virtualCableKeywords)
        if (deviceName.containsIgnoreCase (keyword))
            return true;

    return false;
}

bool platformSupportsAsio()
{
   #if defined (JUCE_ASIO) && JUCE_ASIO && JUCE_WINDOWS
    return true;
   #else
    // ASIO is compiled in only on Windows with JUCE_ASIO enabled. Reporting this
    // honestly is the whole point: without it, "no ASIO device found" and "this build
    // has no ASIO at all" would look identical in the report.
    return false;
   #endif
}

juce::StringArray AudioDeviceInventory::getOutputDevices() const
{
    juce::StringArray result;

    for (const auto& driver : drivers)
        for (const auto& name : driver.deviceNames)
            result.add (driver.typeName + ": " + name);

    return result;
}

juce::StringArray AudioDeviceInventory::getVirtualDeviceNames() const
{
    juce::StringArray result;

    for (const auto& driver : drivers)
        for (const auto& name : driver.deviceNames)
            if (isVirtualCableName (name))
                result.add (name);

    return result;
}

bool AudioDeviceInventory::hasVirtualDevice() const
{
    return ! getVirtualDeviceNames().isEmpty();
}

bool AudioDeviceInventory::hasAsio() const
{
    for (const auto& driver : drivers)
        if (driver.typeName.containsIgnoreCase ("asio") && driver.available)
            return true;

    return false;
}

bool AudioDeviceInventory::isEmpty() const
{
    for (const auto& driver : drivers)
        if (driver.numOutputs > 0)
            return false;

    return true;
}

AudioDeviceInventory probeAudioDevices (juce::AudioDeviceManager& manager)
{
    AudioDeviceInventory inventory;

    // A device manager that has never been initialised has no device types at all, and
    // an empty list would look like "no drivers exist" rather than "nothing was opened".
    // Register the platform's own types so the probe answers the real question.
    auto& types = manager.getAvailableDeviceTypes();

    if (types.isEmpty())
    {
        // createAudioDeviceTypes fills an OwnedArray the caller owns; hand it over to
        // the manager so the probe can then enumerate them normally.
        juce::OwnedArray<juce::AudioIODeviceType> created;
        manager.createAudioDeviceTypes (created);

        for (auto* type : created)
            manager.addAudioDeviceType (std::unique_ptr<juce::AudioIODeviceType> (type));

        created.clear (false);   // ownership moved into the manager
    }

    for (auto* type : manager.getAvailableDeviceTypes())
    {
        if (type == nullptr)
            continue;

        AudioDriverInfo info;
        info.typeName = type->getTypeName();

        // Scanning is what fills in the device list; without it the type reports
        // nothing even when devices exist.
        type->scanForDevices();

        info.deviceNames = type->getDeviceNames (false);   // output devices
        const auto inputNames = type->getDeviceNames (true);

        // getDefaultDeviceIndex() happily returns an index even when the type found no
        // devices at all, so the channel counts are derived from the *list*, not from
        // the default index: a driver with no devices offers no channels.
        info.numOutputs = info.deviceNames.isEmpty() ? 0 : juce::jmax (1, 2);
        info.numInputs  = inputNames.isEmpty()      ? 0 : juce::jmax (1, 2);

        info.available = info.numOutputs > 0 || info.numInputs > 0;

        if (! info.available)
        {
            if (info.typeName.containsIgnoreCase ("asio"))
                info.detail = "ASIO is compiled in but no ASIO driver is installed.";
            else
                info.detail = "This driver reported no devices.";
        }
        else
        {
            info.detail = juce::String (info.deviceNames.size()) + " output, "
                        + juce::String (inputNames.size()) + " input device(s)";
        }

        // An input-only driver is still usable for recording; do not call it missing.
        if (info.deviceNames.isEmpty() && ! inputNames.isEmpty())
            info.numOutputs = 0;

        inventory.drivers.push_back (info);
    }

    return inventory;
}

juce::String describeAudioAvailability (const AudioDeviceInventory& inventory)
{
    if (inventory.drivers.empty())
        return "No audio driver types are available in this build. The platform's device "
               "types could not be registered, so no device can be opened. Physical and "
               "virtual device tests cannot run in this environment.";

    if (inventory.isEmpty())
    {
        juce::String message = "No audio output device was found. Checked: ";

        for (size_t i = 0; i < inventory.drivers.size(); ++i)
        {
            if (i > 0)
                message << ", ";

            message << inventory.drivers[i].typeName;

            if (inventory.drivers[i].detail.isNotEmpty())
                message << " (" << inventory.drivers[i].detail << ")";
        }

        message << ". Install or enable a sound device, or install a virtual cable such "
                   "as VB-CABLE to route audio without hardware.";

        return message;
    }

    juce::String message;

    for (const auto& driver : inventory.drivers)
        if (driver.available)
            message << driver.typeName << ": " << driver.detail << ". ";

    if (inventory.hasVirtualDevice())
        message << "Virtual device detected (" << inventory.getVirtualDeviceNames().joinIntoString (", ")
                << ") - useful for routing tests, but it does not verify real hardware. ";

    return message.trim();
}
}
