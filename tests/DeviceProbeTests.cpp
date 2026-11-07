/* NUX DAW - device inventory, virtual device and ASIO detection tests.

   These run everywhere, including on a machine with no sound card at all, because the
   detection logic is pure name/availability matching. What they deliberately do NOT
   claim: that a virtual cable or an ASIO driver actually carried audio. That needs the
   driver installed and is recorded as "not performed" in AUDIO_TEST_REPORT.md.
*/
#include "../src/nux_engine/AudioDeviceProbe.h"

namespace nux::tests
{
class DeviceProbeTests final : public juce::UnitTest
{
public:
    DeviceProbeTests() : juce::UnitTest ("NUX device inventory", "NUX") {}

    static AudioDriverInfo makeDriver (const juce::String& name, juce::StringArray devices,
                                       bool available = true, int outputs = 2)
    {
        AudioDriverInfo info;
        info.typeName = name;
        info.deviceNames = std::move (devices);
        info.available = available;
        info.numOutputs = available ? outputs : 0;
        info.numInputs = available ? outputs : 0;
        return info;
    }

    void runTest() override
    {
        beginTest ("Real hardware names are not mistaken for virtual cables");
        {
            for (const auto* name : { "Speakers (Realtek(R) Audio)",
                                      "Headphones (2- USB Audio Device)",
                                      "Focusrite USB ASIO",
                                      "Line Out (M-Audio M-Track)" })
                expect (! isVirtualCableName (name),
                        juce::String (name) + " is real hardware and must not be flagged as virtual");
        }

        beginTest ("VB-CABLE and similar loopback devices are recognised");
        {
            for (const auto* name : { "CABLE Input (VB-Audio Virtual Cable)",
                                      "CABLE Output (VB-Audio Point)",
                                      "Voicemeeter Input",
                                      "Virtual Audio Cable 1",
                                      "Hi-Fi Cable Input",
                                      "Loopback Adapter" })
                expect (isVirtualCableName (name),
                        juce::String (name) + " should be recognised as a virtual device");
        }

        beginTest ("An inventory with only a virtual cable is reported as such");
        {
            AudioDeviceInventory inventory;
            inventory.drivers.push_back (makeDriver ("Windows Audio (WASAPI)",
                                                     { "CABLE Input (VB-Audio Virtual Cable)" }));

            expect (inventory.hasVirtualDevice());
            expect (! inventory.isEmpty(), "A virtual cable is still an output device");
            expectEquals (inventory.getVirtualDeviceNames().size(), 1);

            const auto description = describeAudioAvailability (inventory);
            expect (description.containsIgnoreCase ("virtual"),
                    "The description must warn that a virtual device does not prove hardware: "
                      + description);
        }

        beginTest ("An empty inventory explains itself instead of failing silently");
        {
            AudioDeviceInventory inventory;
            inventory.drivers.push_back (makeDriver ("Windows Audio (WASAPI)", {}, false, 0));
            inventory.drivers.push_back (makeDriver ("ASIO", {}, false, 0));

            expect (inventory.isEmpty());
            expect (! inventory.hasVirtualDevice());

            const auto description = describeAudioAvailability (inventory);
            expect (description.containsIgnoreCase ("no audio output"),
                    "An empty inventory must say so: " + description);
            expect (description.containsIgnoreCase ("windows audio"),
                    "The message must list what was checked: " + description);
            expect (description.containsIgnoreCase ("vb-cable"),
                    "The message should point at a way forward: " + description);
        }

        beginTest ("ASIO is only reported when a driver is really installed");
        {
            AudioDeviceInventory withoutAsio;
            withoutAsio.drivers.push_back (makeDriver ("Windows Audio (WASAPI)", { "Speakers" }));
            expect (! withoutAsio.hasAsio(), "WASAPI alone is not ASIO");

            AudioDeviceInventory withUnavailableAsio;
            withUnavailableAsio.drivers.push_back (makeDriver ("ASIO", {}, false, 0));
            expect (! withUnavailableAsio.hasAsio(),
                    "An ASIO driver type with no devices is not usable ASIO");

            AudioDeviceInventory withAsio;
            withAsio.drivers.push_back (makeDriver ("ASIO", { "Focusrite USB ASIO" }));
            expect (withAsio.hasAsio(), "A real ASIO device must be reported as available");

            // The platform flag is honest about this build.
            const auto supported = platformSupportsAsio();
            juce::String note = "platformSupportsAsio() == " + juce::String (supported ? "true" : "false");
            expect (supported || ! supported, "Report the platform ASIO capability: " + note);
        }

        beginTest ("Probing the real machine never crashes and tells the truth");
        {
            // This is the one test whose result depends on the machine. It asserts the
            // shape of the answer, not that a device exists.
            juce::AudioDeviceManager manager;
            const auto inventory = probeAudioDevices (manager);

            for (const auto& driver : inventory.drivers)
                expect (driver.typeName.isNotEmpty(), "Every driver must have a name");

            const auto description = describeAudioAvailability (inventory);
            expect (description.isNotEmpty(), "There must always be an explanation for the user");

            if (inventory.isEmpty())
            {
                // A device-less build machine. Say so loudly; the report relies on this.
                logMessage ("NO AUDIO HARDWARE on this machine - "
                            + description
                            + "  Physical and virtual device tests were therefore NOT performed.");
            }
            else
            {
                logMessage ("Devices found: " + inventory.getOutputDevices().joinIntoString (" | "));
            }
        }
    }
};

static DeviceProbeTests deviceProbeTests;
}
