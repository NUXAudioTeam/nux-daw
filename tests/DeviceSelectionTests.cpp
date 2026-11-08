/* Device selection and the virtual loopback decision logic.

   This machine has no sound card, which is exactly why these tests are written the way
   they are: every rule is exercised against device *names*, so the rules are checked even
   when the devices are not there. What cannot be checked here is whether a real cable
   carries audio - that is step C on hardware, and AUDIO_TEST_REPORT.md keeps saying so. */
#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "../src/nux_engine/DeviceSelection.h"
#include "../src/nux_engine/VirtualLoopback.h"
#include "../src/nux_engine/RoundTrip.h"

namespace
{
using namespace nux;
using nux::lab::LoopbackRequest;

std::vector<DeviceOption> vbcableMachine()
{
    // The names VB-CABLE actually installs on Windows.
    auto options = buildDeviceOptions ("Windows Audio (WASAPI)",
                                       { "Speakers (Realtek(R) Audio)",
                                         "CABLE Input (VB-Audio Virtual Cable)" },
                                       { "Microphone (Realtek(R) Audio)",
                                         "CABLE Output (VB-Audio Virtual Cable)" });
    return options;
}

std::vector<DeviceOption> hardwareOnlyMachine()
{
    return buildDeviceOptions ("Windows Audio (WASAPI)",
                               { "Speakers (Realtek(R) Audio)" },
                               { "Microphone (Realtek(R) Audio)" });
}
}

class DeviceSelectionTests final : public juce::UnitTest
{
public:
    DeviceSelectionTests() : juce::UnitTest ("NUX device selection", "NUX") {}

    void runTest() override
    {
        beginTest ("Virtual cables are recognised and real hardware is not");
        {
            const auto options = vbcableMachine();

            const auto* cable = [&options]
            {
                for (const auto& option : options)
                    if (option.deviceName.startsWith ("CABLE Input"))
                        return &option;

                return (const DeviceOption*) nullptr;
            }();

            expect (cable != nullptr, "VB-CABLE's playback device should be in the list");
            expect (cable != nullptr && cable->isVirtual,
                    "CABLE Input must be flagged virtual, not treated as a speaker");
            expect (cable != nullptr && cable->canOutput && ! cable->canInput,
                    "CABLE Input is a playback device only");

            for (const auto& option : options)
                if (option.deviceName.startsWith ("Speakers") || option.deviceName.startsWith ("Microphone"))
                    expect (! option.isVirtual,
                            "\"" + option.deviceName + "\" is real hardware and must not be flagged virtual");
        }

        beginTest ("A virtual cable's two halves are paired, and nothing else is");
        {
            const auto pair = findVirtualCablePair (vbcableMachine());

            expect (pair.found, "VB-CABLE's halves should pair");
            expectEquals (pair.outputName, juce::String ("CABLE Input (VB-Audio Virtual Cable)"));
            expectEquals (pair.inputName,  juce::String ("CABLE Output (VB-Audio Virtual Cable)"));
            expect (pair.explanation.containsIgnoreCase ("does")
                        || pair.explanation.containsIgnoreCase ("says nothing"),
                    "the pairing must state what a virtual result does not prove");

            const auto none = findVirtualCablePair (hardwareOnlyMachine());
            expect (! none.found, "a machine with no cable must not report a pair");
            expect (none.explanation.containsIgnoreCase ("skipped"),
                    "with no cable the answer must point at skipped, not at a pass");
        }

        beginTest ("A cable half never pairs with itself");
        {
            // Some drivers list one name for both directions; that is not a loop back.
            const auto single = buildDeviceOptions ("Windows Audio (WASAPI)",
                                                    { "CABLE Input (VB-Audio Virtual Cable)" },
                                                    { "CABLE Input (VB-Audio Virtual Cable)" });
            const auto pair = findVirtualCablePair (single);
            expect (! pair.found, "one device offering both directions is not a cable pair");
        }

        beginTest ("Halves from different drivers are not a pair");
        {
            auto options = buildDeviceOptions ("Windows Audio (WASAPI)",
                                               { "CABLE Input (VB-Audio Virtual Cable)" }, {});
            for (auto& option : options)
                option.typeName = "DirectSound";

            options.push_back ({ });
            options.back().typeName = "Windows Audio (WASAPI)";
            options.back().deviceName = "CABLE Output (VB-Audio Virtual Cable)";
            options.back().canInput = true;
            options.back().isVirtual = true;

            const auto pair = findVirtualCablePair (options);
            expect (! pair.found,
                    "a playback device on one driver cannot feed a capture device on another");
        }

        beginTest ("An empty selection explains itself instead of opening nothing");
        {
            const DeviceChoice empty;
            const auto selection = makeSelection (empty, vbcableMachine(), true);

            expect (! selection.valid, "an empty choice cannot be valid");
            expect (selection.error.isNotEmpty(), "there must be a reason, not just a refusal");
            expect (selection.setup.outputDeviceName.isEmpty(),
                    "nothing may be opened when the choice is empty");
        }

        beginTest ("A round trip without an input is refused, not attempted");
        {
            DeviceChoice choice;
            choice.typeName = "Windows Audio (WASAPI)";
            choice.outputName = "CABLE Input (VB-Audio Virtual Cable)";

            const auto selection = makeSelection (choice, vbcableMachine(), true);
            expect (! selection.valid, "a round trip needs a return path");
            expect (selection.error.containsIgnoreCase ("input")
                        || selection.error.containsIgnoreCase ("come back"),
                    "the message should say what is missing");

            // The same choice is fine when only playback is wanted.
            const auto playbackOnly = makeSelection (choice, vbcableMachine(), false);
            expect (playbackOnly.valid, "playback alone does not need an input");
        }

        beginTest ("A device that is not there is reported by name");
        {
            DeviceChoice choice;
            choice.typeName = "Windows Audio (WASAPI)";
            choice.outputName = "CABLE Input (VB-Audio Virtual Cable)";
            choice.inputName = "CABLE Output (VB-Audio Virtual Cable)";

            const auto missing = hardwareOnlyMachine();
            const auto selection = makeSelection (choice, missing, true);

            expect (! selection.valid, "a cable that is not installed cannot be selected");
            expect (selection.error.contains ("CABLE Input"),
                    "the error must quote the device that is missing: " + selection.error);
            expect (selection.explanation.containsIgnoreCase ("reopen")
                        || selection.explanation.containsIgnoreCase ("refresh"),
                    "it should tell the user how to fix it");
        }

        beginTest ("A virtual pairing is labelled as one, and a real one is not");
        {
            DeviceChoice virtualChoice;
            virtualChoice.typeName = "Windows Audio (WASAPI)";
            virtualChoice.outputName = "CABLE Input (VB-Audio Virtual Cable)";
            virtualChoice.inputName = "CABLE Output (VB-Audio Virtual Cable)";

            const auto virtualSelection = makeSelection (virtualChoice, vbcableMachine(), true);
            expect (virtualSelection.valid, "a complete cable pairing must be usable");
            expect (virtualSelection.usesVirtualCable, "it must admit that it is a virtual cable");
            expect (virtualSelection.explanation.containsIgnoreCase ("does not prove"),
                    "the explanation must state the limit of the claim");

            DeviceChoice realChoice;
            realChoice.typeName = "Windows Audio (WASAPI)";
            realChoice.outputName = "Speakers (Realtek(R) Audio)";
            realChoice.inputName = "Microphone (Realtek(R) Audio)";

            const auto realSelection = makeSelection (realChoice, hardwareOnlyMachine(), true);
            expect (realSelection.valid, "a real in/out pair is a valid choice");
            expect (! realSelection.usesVirtualCable, "real hardware must not be called virtual");
        }

        beginTest ("Mixing drivers in one round trip is refused with a reason");
        {
            auto options = buildDeviceOptions ("Windows Audio (WASAPI)",
                                               { "CABLE Input (VB-Audio Virtual Cable)" },
                                               { "CABLE Output (VB-Audio Virtual Cable)" });

            // Pretend the capture half came from another driver.
            for (auto& option : options)
                if (option.canInput)
                    option.typeName = "DirectSound";

            DeviceChoice choice;
            choice.typeName = "Windows Audio (WASAPI)";
            choice.outputName = "CABLE Input (VB-Audio Virtual Cable)";
            choice.inputName = "CABLE Output (VB-Audio Virtual Cable)";

            const auto selection = makeSelection (choice, options, true);
            expect (! selection.valid, "one device cannot be open on two drivers");
            expect (selection.error.containsIgnoreCase ("driver"),
                    "the reason should name the driver problem: " + selection.error);
        }

        beginTest ("With no devices a round trip is skipped, never passed");
        {
            LoopbackRequest request;
            request.choice.typeName = "Windows Audio (WASAPI)";
            request.choice.outputName = "CABLE Input (VB-Audio Virtual Cable)";
            request.choice.inputName = "CABLE Output (VB-Audio Virtual Cable)";
            request.spec.type = nux::lab::SignalType::sine;
            request.spec.frequency = 1000.0;
            request.seconds = 0.25;

            // The device list says the cable is missing, so nothing may be opened.
            const auto outcome = nux::lab::runDeviceLoopback (request, hardwareOnlyMachine());

            expect (! outcome.ran, "nothing should have run on a machine without the cable");
            expect (outcome.result.verification == nux::lab::Verification::skipped,
                    "the honest answer is skipped");
            expect (outcome.result.verification != nux::lab::Verification::virtualDevice,
                    "a skipped run must never claim a virtual device carried audio");
            expect (outcome.result.verification != nux::lab::Verification::physicalDevice,
                    "and it must never claim hardware either");
            expect (outcome.error.isNotEmpty(), "the skip needs a reason");
            expect (outcome.result.summary.containsIgnoreCase ("skipped"),
                    "the summary must say skipped out loud: " + outcome.result.summary);
            expect (! outcome.result.captured, "no capture happened");
        }

        beginTest ("An empty device list produces a skip, not a crash");
        {
            LoopbackRequest request;
            request.choice.outputName = "CABLE Input (VB-Audio Virtual Cable)";
            request.choice.inputName = "CABLE Output (VB-Audio Virtual Cable)";

            const auto outcome = nux::lab::runDeviceLoopback (request, {});

            expect (! outcome.ran, "an empty list cannot run anything");
            expect (outcome.result.verification == nux::lab::Verification::skipped,
                    "still skipped, never a pass");
        }

        beginTest ("The cable family key ignores the half and keeps the family");
        {
            expectEquals (getCableFamilyKey ("CABLE Input (VB-Audio Virtual Cable)"),
                          getCableFamilyKey ("CABLE Output (VB-Audio Virtual Cable)"),
                          "both halves of one cable must share a key");

            expect (getCableFamilyKey ("CABLE Input (VB-Audio Virtual Cable)")
                        != getCableFamilyKey ("VAIO3 Input (Voicemeeter)"),
                    "two different cables must not collapse into one family");
        }

        beginTest ("Every option carries enough information to be shown to a user");
        {
            for (const auto& option : vbcableMachine())
            {
                expect (option.deviceName.isNotEmpty(), "a device without a name cannot be shown");
                expect (option.typeName.isNotEmpty(), "a device without a driver cannot be opened");
                expect (option.canOutput || option.canInput,
                        "\"" + option.deviceName + "\" is listed but usable for nothing");
                expect (option.getLabel().contains (option.deviceName),
                        "the label must contain the real name");
            }
        }
    }
};

static DeviceSelectionTests deviceSelectionTests;
