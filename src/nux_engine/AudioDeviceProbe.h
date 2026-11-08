/* NUX DAW - audio device and driver probing.

   This answers questions the UI and the test report need to ask *before* anything is
   opened, and it answers them honestly: it reports what is actually installed rather
   than what the build system could theoretically support.

   It is used for two things:
     * telling the user why no device is available, with a useful message, and
     * producing the device inventory in AUDIO_TEST_REPORT.md. */
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace nux
{
struct AudioDriverInfo
{
    juce::String typeName;          // "Windows Audio (WASAPI)", "ASIO", "DirectSound"...
    bool available = false;         // is the driver installed and usable right now
    int numOutputs = 0;             // output channels the driver currently offers
    int numInputs = 0;
    juce::StringArray deviceNames;  // the devices it can open
    juce::String detail;            // why it is unavailable, when it is
};

struct AudioDeviceInventory
{
    std::vector<AudioDriverInfo> drivers;

    /** Every output-capable device, across all drivers, as "driver: device". */
    juce::StringArray getOutputDevices() const;

    /** True when some device is a known virtual cable (VB-CABLE, Voicemeeter...). */
    bool hasVirtualDevice() const;

    /** Names that look like a virtual loopback cable. */
    juce::StringArray getVirtualDeviceNames() const;

    bool hasAsio() const;

    /** True when no driver offers a single output channel. */
    bool isEmpty() const;
};

/** Builds the inventory without opening anything. */
AudioDeviceInventory probeAudioDevices (juce::AudioDeviceManager& manager);

/** A one-paragraph explanation of why audio is unavailable, for the UI and the report. */
juce::String describeAudioAvailability (const AudioDeviceInventory& inventory);

/** True when a name looks like a virtual audio cable rather than real hardware. */
bool isVirtualCableName (const juce::String& deviceName);

/** True when the running platform can offer ASIO at all (Windows builds only). */
bool platformSupportsAsio();
}
