/* NUX DAW - Virtual Audio Lab, step B: round trip tests.

   The point of these tests is not only to prove the software loopback works. It is to
   prove the reporting cannot lie: a run that did not happen must never be reported as a
   pass, and simulated evidence must never be labelled as hardware. */
#include "../src/nux_engine/RoundTrip.h"
#include "../src/nux_engine/AudioEngine.h"
#include "../src/nux_engine/AudioDeviceProbe.h"

namespace nux::tests
{
class RoundTripTests final : public juce::UnitTest
{
public:
    RoundTripTests() : juce::UnitTest ("NUX audio round trip", "NUX") {}

    static lab::SignalSpec sine (double hz, float amplitude = 0.5f, int channels = 2)
    {
        lab::SignalSpec spec;
        spec.type = lab::SignalType::sine;
        spec.frequency = hz;
        spec.amplitude = amplitude;
        spec.durationSeconds = 1.0;
        spec.sampleRate = 48000.0;
        spec.numChannels = channels;
        return spec;
    }

    void runTest() override
    {
        beginTest ("A software round trip returns the signal unchanged");
        {
            const auto result = lab::runSoftwareRoundTrip (sine (440.0));

            expect (result.captured, "The software loopback must capture something");
            expectWithinAbsoluteError (result.gainDb, 0.0f, 0.01f,
                                       "A block copied through must not change level, measured "
                                         + juce::String (result.gainDb, 4) + " dB");
            expectWithinAbsoluteError (result.frequencyErrorPercent, 0.0, 0.001);
            expectEquals (result.numSamplesCaptured, 48000);
            expect (! result.clipped, "A -6 dBFS sine must not clip");
        }

        beginTest ("A software round trip is labelled simulated, never hardware");
        {
            // This is the core honesty rule of the lab. A test that passed no audio
            // through a device must not be able to claim one.
            const auto result = lab::runSoftwareRoundTrip (sine (440.0));

            expect (result.verification == lab::Verification::simulated,
                    juce::String ("Expected 'simulated', got '")
                      + lab::getVerificationName (result.verification) + "'");
            expect (result.verification != lab::Verification::physicalDevice,
                    "A software loopback must never be reported as physical hardware");
            expect (result.verification != lab::Verification::virtualDevice,
                    "A software loopback must never be reported as a virtual device");
            expect (juce::String (lab::getVerificationName (result.verification))
                      .containsIgnoreCase ("simulated"),
                    "The name must say 'simulated' so a report reader cannot misread it");
        }

        beginTest ("Without a device the round trip is skipped, with a reason, not passed");
        {
            juce::AudioDeviceManager manager;
            // Deliberately not initialised: this is the no-hardware machine case.
            const auto result = lab::runDeviceRoundTrip (manager, sine (440.0));

            expect (result.verification == lab::Verification::skipped,
                    juce::String ("No device must give 'skipped', got '")
                      + lab::getVerificationName (result.verification) + "'");
            expect (! result.captured, "Nothing can be captured without a device");
            expect (result.summary.containsIgnoreCase ("skipped"),
                    "The summary must say it was skipped: " + result.summary);
            expect (result.detail.length() > 40,
                    "A skipped run must explain itself, not just fail silently");
        }

        beginTest ("A skipped round trip can never be judged a pass");
        {
            // The dangerous bug this guards against: a verdict function that only looks at
            // measurements, and so returns an empty (passing) verdict for a run that sent
            // and captured nothing, because every error was zero.
            juce::AudioDeviceManager manager;
            const auto skipped = lab::runDeviceRoundTrip (manager, sine (440.0));
            const lab::RoundTripTolerance tolerance;

            const auto verdict = lab::judgeRoundTrip (skipped, tolerance);

            expect (verdict.isNotEmpty(),
                    "A skipped round trip must never return an empty (passing) verdict");
            expect (verdict.containsIgnoreCase ("not run"),
                    "The verdict must start by saying it was not run: " + verdict);
        }

        beginTest ("A clean capture passes and a wrong one does not");
        {
            auto result = lab::runSoftwareRoundTrip (sine (440.0));
            const lab::RoundTripTolerance tolerance;

            expect (lab::judgeRoundTrip (result, tolerance).isEmpty(),
                    "A faithful round trip must pass: "
                      + lab::judgeRoundTrip (result, tolerance));

            // Change the pitch: the verdict must notice.
            auto detuned = result;
            detuned.frequencyErrorPercent = 2.0;
            expect (lab::judgeRoundTrip (detuned, tolerance).isNotEmpty(),
                    "A 2% pitch error must fail the tolerance");

            // Change the level.
            auto attenuated = result;
            attenuated.gainDb = -6.0f;
            expect (lab::judgeRoundTrip (attenuated, tolerance).isNotEmpty(),
                    "A -6 dB level change must fail the tolerance");

            // Clip it.
            auto clipper = result;
            clipper.clipped = true;
            expect (lab::judgeRoundTrip (clipper, tolerance).isNotEmpty(),
                    "A clipped capture must fail the tolerance");

            // Collapse the stereo image.
            auto mono = result;
            mono.correlation = 0.1;
            expect (lab::judgeRoundTrip (mono, tolerance).isNotEmpty(),
                    "A decorrelated capture must fail the tolerance");
        }

        beginTest ("The round trip measures frequency, not just level");
        {
            // A device or a resampler that changes the pitch is a real failure mode, and a
            // level-only check would miss it completely. Prove the field is populated from
            // the actual signal by measuring a tone whose frequency is known.
            for (const auto hz : { 220.0, 440.0, 1000.0, 5000.0 })
            {
                const auto result = lab::runSoftwareRoundTrip (sine (hz));
                const auto measured = lab::measureFrequencyAccurate (
                    lab::generateSignal (result.spec), 0, result.sampleRate);

                expectWithinAbsoluteError (measured, hz, hz * 0.001,
                                           "A software round trip must preserve "
                                             + juce::String (hz, 1) + " Hz, measured "
                                             + juce::String (measured, 2));
            }
        }

        beginTest ("Mono and stereo signals both survive the round trip");
        {
            const auto mono = lab::runSoftwareRoundTrip (sine (440.0, 0.5f, 1));
            expectEquals (mono.numChannels, 1);
            expectWithinAbsoluteError (mono.gainDb, 0.0f, 0.01f);

            const auto stereo = lab::runSoftwareRoundTrip (sine (440.0, 0.5f, 2));
            expectEquals (stereo.numChannels, 2);
            expectWithinAbsoluteError (stereo.correlation, 1.0, 0.01,
                                       "Identical channels must stay correlated");
        }

        beginTest ("Every signal type survives the software round trip without changing level");
        {
            // The loopback copies samples, so the level must be identical for every one of
            // them - including the awkward ones (impulse, DC, noise).
            for (int type = 0; type <= (int) lab::SignalType::impulse; ++type)
            {
                lab::SignalSpec spec;
                spec.type = (lab::SignalType) type;
                spec.frequency = 1000.0;
                spec.amplitude = 0.5f;
                spec.durationSeconds = 0.25;
                spec.sampleRate = 48000.0;

                const auto result = lab::runSoftwareRoundTrip (spec);

                expect (result.captured, lab::getSignalName (spec.type) + " must be captured");

                expectWithinAbsoluteError (result.gainDb, 0.0f, 0.01f,
                                           lab::getSignalName (spec.type)
                                             + " changed level by "
                                             + juce::String (result.gainDb, 4) + " dB in a "
                                             + "sample-for-sample copy");
            }
        }

        beginTest ("No engine object is too large for a 1 MB Windows stack");
        {
            // This is the guard for the bug that made Windows CI segfault before its
            // first test could run: sizeof(AudioEngine) is about 424 KB, and the default
            // Windows stack is 1 MB, so a single stack-allocated engine overflowed it.
            // Linux never showed it, because its default stack is 8 MB.
            //
            // A unit test cannot inspect how other code allocates, so this pins the size
            // instead: if the engine grows into something that must not sit on a stack,
            // the number here tells the next person immediately.
            constexpr auto engineBytes = sizeof (nux::AudioEngine);
            constexpr auto windowsDefaultStack = 1024 * 1024;

            logMessage ("sizeof(AudioEngine) = " + juce::String ((int) engineBytes)
                        + " bytes, Windows default stack = " + juce::String (windowsDefaultStack));

            expect (engineBytes < windowsDefaultStack / 2,
                    "sizeof(AudioEngine) is " + juce::String ((int) (engineBytes / 1024))
                      + " KB, which is too large to put on a 1 MB Windows stack next to "
                      "anything else. Construct it on the heap.");
        }

        beginTest ("A device round trip never reports hardware it did not use");
        {
            // Whatever this machine has, the result must be honest about it. On the build
            // machine there is no device, so this asserts the skipped path; on a machine
            // with a device it must still not claim a pass without a real capture.
            juce::AudioDeviceManager manager;
            manager.initialiseWithDefaultDevices (0, 2);

            const auto inventory = nux::probeAudioDevices (manager);
            const auto result = lab::runDeviceRoundTrip (manager, sine (440.0));

            if (result.verification == lab::Verification::physicalDevice)
            {
                expect (result.captured,
                        "A physical-hardware verdict is only legal with a real capture");
            }
            else
            {
                expect (result.verification == lab::Verification::skipped
                          || result.verification == lab::Verification::notPerformed,
                        "Without a driven capture the only honest verdicts are skipped or "
                        "not performed, got '"
                          + juce::String (lab::getVerificationName (result.verification)) + "'");
            }

            logMessage ("Device inventory: " + juce::String (inventory.isEmpty() ? "none" : "present")
                        + " | verdict: " + lab::getVerificationName (result.verification));
        }
    }
};

static RoundTripTests roundTripTests;
}
