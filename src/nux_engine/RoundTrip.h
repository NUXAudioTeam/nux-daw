/* NUX DAW - Virtual Audio Lab, step B: the round trip.

   Everything measured so far was measured on a buffer the test itself built. That proves
   the maths, not the engine. Step B closes the loop: push a known signal into the engine,
   let it come back out through a real processing path, capture what leaves, and measure
   it with the same instruments.

   Two rules this file exists to enforce:

   1. A round trip is only "verified" when a real device carried the sound. A software
      loopback proves the engine, not the hardware, and the two must never be reported as
      the same thing.
   2. If no device exists, the honest answer is SKIPPED with a reason - never a pass.
      A skipped test that looks green is worse than no test at all. */
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "../src/nux_engine/Measurements.h"
#include "../src/nux_engine/TestSignals.h"

namespace nux::lab
{
/** How much this run can actually claim. Chosen so the weakest evidence cannot be
    mistaken for the strongest. */
enum class Verification
{
    notPerformed,   // nothing was run
    skipped,        // could not run, for a stated reason (no device, no driver)
    simulated,      // software path only: proves the engine, proves nothing about hardware
    virtualDevice,  // a virtual cable carried the audio: proves routing, not the converter
    physicalDevice  // a real output/input pair carried it: the only hardware-level pass
};

const char* getVerificationName (Verification v);

struct RoundTripResult
{
    Verification verification = Verification::notPerformed;
    juce::String summary;         // one line, plain English
    juce::String detail;          // the reason a skipped result was skipped

    juce::String driverType;
    juce::String deviceName;
    double sampleRate = 0.0;
    int bufferSize = 0;
    int numChannels = 0;

    // What went in. Always a known signal.
    SignalSpec spec;
    LevelMeasurement sent;

    // What came back, if anything did.
    bool captured = false;
    LevelMeasurement received;
    double frequencyErrorPercent = 0.0;
    double correlation = 0.0;
    float gainDb = 0.0f;            // received minus sent
    bool clipped = false;
    int numSamplesCaptured = 0;
};

/** Where the signal goes and comes back from. */
enum class RoundTripPath
{
    /** Straight through the engine's own render loop, no device. Always available. */
    softwareLoopback,
    /** Out of a real output device and back into an input device. Needs both. */
    device
};

/** Runs a software round trip: renders the signal through the engine's test path and
    measures it. This one always runs, and always reports as `simulated`. */
RoundTripResult runSoftwareRoundTrip (const SignalSpec& spec);

/** Attempts a device round trip against whatever the device manager has open.

    Without a device this returns a `skipped` result with a real reason and never throws.
    It is not a pass, and AUDIO_TEST_REPORT.md must keep saying so. */
RoundTripResult runDeviceRoundTrip (juce::AudioDeviceManager& manager, const SignalSpec& spec);

/** Tolerance for calling a round trip clean. A device that runs at a slightly different
    clock, or one buffer of latency, is normal; a device that changes the level or the pitch
    is not. */
struct RoundTripTolerance
{
    double maxFrequencyErrorPercent = 0.5;
    float maxGainErrorDb = 1.0f;
    double minCorrelation = 0.95;
    float maxClipPercentage = 0.0f;
};

/** Compares a result against the tolerances and returns a human-readable verdict, or an
    empty string when it passes. Never returns "pass" for a skipped run. */
juce::String judgeRoundTrip (const RoundTripResult& result, const RoundTripTolerance& tolerance);
}
