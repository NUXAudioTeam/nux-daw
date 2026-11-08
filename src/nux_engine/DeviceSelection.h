/* NUX DAW - choosing which device plays and which records.

   Until now the engine always opened whatever the operating system called the default
   device, which is fine for a demo and useless for two real jobs:

   * letting a user with a real interface pick it, and
   * letting the Virtual Audio Lab route NUX out through a virtual cable and back in
     through the matching recording side, so a round trip can happen with no hardware.

   Everything here is deliberately pure: it looks at lists of names and returns a
   selection with an explanation or an error. Opening the device is somebody else's job,
   which is what makes this testable on a machine with no sound card at all. */
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <vector>

namespace nux
{
/** One selectable device, as the platform describes it. */
struct DeviceOption
{
    juce::String typeName;      // "Windows Audio (WASAPI)", "ASIO", "ALSA"...
    juce::String deviceName;    // "CABLE Input (VB-Audio Virtual Cable)"
    bool canOutput = false;
    bool canInput = false;
    bool isVirtual = false;     // a virtual cable rather than a converter

    /** "CABLE Input (VB-Audio Virtual Cable) [virtual]" - what a menu should show. */
    juce::String getLabel() const;

    /** True when the platform lists this name for playback and/or capture. */
    bool worksFor (bool output) const noexcept { return output ? canOutput : canInput; }
};

/** Every device the manager can see, output and input sides merged by name. */
std::vector<DeviceOption> listDeviceOptions (juce::AudioDeviceManager& manager);

/** Merges two name lists into options. Split out so the merging rules can be tested
    without a sound card, which is the only way they get tested on this machine. */
std::vector<DeviceOption> buildDeviceOptions (const juce::String& typeName,
                                              const juce::StringArray& outputNames,
                                              const juce::StringArray& inputNames);

/** What the user asked for. Empty names mean "not chosen yet". */
struct DeviceChoice
{
    juce::String typeName;
    juce::String outputName;
    juce::String inputName;
};

/** The result of checking a choice: either a setup that can be applied, or a reason why
    it cannot. Never both, and never a silent failure. */
struct DeviceSelection
{
    bool valid = false;
    juce::String error;         // set when valid == false
    juce::String explanation;   // always set: what this pairing can and cannot prove

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    bool usesVirtualCable = false;
};

/** Checks a choice against the devices that actually exist.

    `requireInput` is true for a round trip, where a signal has to come back. */
DeviceSelection makeSelection (const DeviceChoice& choice,
                               const std::vector<DeviceOption>& options,
                               bool requireInput);

/** A virtual cable's two halves: the playback device you write into and the recording
    device you read back from. VB-CABLE names them "CABLE Input" and "CABLE Output". */
struct VirtualCablePair
{
    bool found = false;
    juce::String typeName;
    juce::String outputName;  // where NUX sends audio
    juce::String inputName;   // where NUX listens
    juce::String explanation; // what this does and does not prove
};

/** Finds a matching pair of virtual devices, or reports that there is none. */
VirtualCablePair findVirtualCablePair (const std::vector<DeviceOption>& options);

/** Strips the "Input"/"Output" half out of a cable name so the two halves can be
    matched: "CABLE Input (VB-Audio Virtual Cable)" and
    "CABLE Output (VB-Audio Virtual Cable)" both become one family key. */
juce::String getCableFamilyKey (const juce::String& deviceName);
}
