/* NUX DAW - Virtual Audio Lab, step C: a real device round trip.

   Step B closed the software loop. This closes the device loop: NUX plays a known signal
   into a chosen output, captures what comes back on a chosen input, and measures it with
   the instruments from step A.

   The value of this file is not the measurement. It is that a result can only claim what
   actually happened:

     * no matching device        -> skipped, with the reason the driver gave
     * a virtual cable carried it -> virtualDevice, and the summary says what that proves
     * real hardware carried it   -> physicalDevice, the only hardware-level pass

   Nothing here can turn a missing device into a pass, which is the failure mode that makes'
   hardware test reports worthless. */
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "DeviceSelection.h"
#include "RoundTrip.h"

namespace nux::lab
{
struct LoopbackRequest
{
    DeviceChoice choice;
    SignalSpec spec;
    double sampleRate = 48000.0;
    int bufferSize = 512;
    double seconds = 1.0;
};

struct LoopbackOutcome
{
    RoundTripResult result;
    bool ran = false;          // did audio actually flow through a device
    juce::String error;        // why it did not run, when ran == false
    juce::String verdict;      // judgeRoundTrip's answer; empty means it passed
    juce::String deviceError;  // the driver's own words, when it refused to open
};

/** Runs the round trip through a device.

    Opens its own device manager so a lab run can never leave the application's audio
    broken, and so a failure here is reported rather than silently changing the session.
    Safe to call from a non-audio thread; it blocks for roughly `seconds`. */
LoopbackOutcome runDeviceLoopback (const LoopbackRequest& request);

/** The same run, against an already-populated option list. Split out so the decision
    logic can be tested on a machine with no sound card: with no devices it must return
    skipped, and it must never invent a verdict. */
LoopbackOutcome runDeviceLoopback (const LoopbackRequest& request,
                                   const std::vector<DeviceOption>& options);
}
