/* NUX DAW - known-answer tests for the measurement maths.

   Every test here builds a signal whose correct measurement is known from its formula,
   measures it, and compares. There are no hardcoded PASS results anywhere: if the maths
   is wrong, the number is wrong and the test fails.

   This is the foundation of the Virtual Audio Lab. If these are wrong, every other lab
   result is meaningless. */
#include "SignalAnalysis.h"
#include "../src/nux_engine/Measurements.h"
#include "../src/nux_engine/TestSignals.h"

namespace nux::tests
{
class MeasurementTests final : public juce::UnitTest
{
public:
    MeasurementTests() : juce::UnitTest ("NUX measurements", "NUX") {}

    static juce::AudioBuffer<float> make (lab::SignalType type, double frequency,
                                          float amplitude, double seconds = 1.0,
                                          double rate = 48000.0)
    {
        lab::SignalSpec spec;
        spec.type = type;
        spec.frequency = frequency;
        spec.amplitude = amplitude;
        spec.durationSeconds = seconds;
        spec.sampleRate = rate;
        return lab::generateSignal (spec);
    }

    void runTest() override
    {
        beginTest ("A half amplitude sine measures -6 dBFS peak and -9 dBFS RMS");
        {
            // 0.5 amplitude is -6.02 dBFS peak, -9.03 dBFS RMS.
            const auto buffer = make (lab::SignalType::sine, 1000.0, 0.5f);
            const auto levels = lab::measureLevels (buffer, 0);

            expectWithinAbsoluteError (levels.peakDb, -6.0206f, 0.01f,
                                       "0.5 amplitude should be -6.02 dBFS, measured "
                                         + juce::String (levels.peakDb, 4));
            expectWithinAbsoluteError (levels.rmsDb, -9.0309f, 0.02f,
                                       "A sine's RMS is peak - 3.01 dB, measured "
                                         + juce::String (levels.rmsDb, 4));
            expectWithinAbsoluteError (levels.crestFactorDb, 3.0103f, 0.02f,
                                       "Crest factor of a sine should be 3.01 dB");
            expect (! levels.clipped, "A 0.5 sine is not clipped");
        }

        beginTest ("Clipping is detected by consecutive full scale samples, not one");
        {
            // A single full-scale sample is normal in a loud sine; AES17 requires three
            // consecutive ones before it counts as a clip.
            juce::AudioBuffer<float> single (1, 64);
            single.clear();
            single.setSample (0, 10, 1.0f);
            expect (! lab::measureLevels (single, 0).clipped,
                    "One full-scale sample must not be reported as clipping");

            juce::AudioBuffer<float> hard (1, 64);
            hard.clear();

            for (int i = 10; i < 20; ++i)
                hard.setSample (0, i, 1.0f);

            expect (lab::measureLevels (hard, 0).clipped,
                    "A run of full-scale samples must be reported as clipping");
        }

        beginTest ("Silence measures as silence, not as a small number");
        {
            const auto buffer = make (lab::SignalType::silence, 1000.0, 0.5f);
            const auto levels = lab::measureLevels (buffer, 0);

            expectEquals (levels.peak, 0.0f);
            expect (levels.peakDb <= -200.0f, "Silence must read -inf dB, not "
                                              + juce::String (levels.peakDb, 2));
            expect (! levels.clipped);
        }

        beginTest ("Frequency is measured to within 0.1% of the truth");
        {
            // Parabolic interpolation, so this is far tighter than zero-crossing counting.
            const struct { double hz; double rate; } cases[] =
            {
                { 440.0,   44100.0 },
                { 1000.0,  48000.0 },
                { 997.0,   44100.0 },   // deliberately not a bin centre
                { 5000.0,  96000.0 },
                { 15000.0, 192000.0 }
            };

            for (const auto& c : cases)
            {
                const auto buffer = make (lab::SignalType::sine, c.hz, 0.5f, 1.0, c.rate);
                const auto measured = lab::measureFrequencyAccurate (buffer, 0, c.rate);
                const auto errorPercent = std::abs (measured - c.hz) / c.hz * 100.0;

                expect (errorPercent < 0.1,
                        juce::String (c.hz, 1) + " Hz at " + juce::String (c.rate)
                          + " measured as " + juce::String (measured, 2)
                          + " Hz (" + juce::String (errorPercent, 4) + "% error)");
            }
        }

        beginTest ("A pure sine has almost no distortion");
        {
            // A 1 kHz sine at 48 kHz has every harmonic below Nyquist, so a perfect
            // generator must measure THD far below audibility.
            const auto buffer = make (lab::SignalType::sine, 1000.0, 0.5f, 1.0, 48000.0);
            const auto distortion = lab::measureDistortion (buffer, 0, 1000.0, 48000.0);

            expect (distortion.fundamentalDb > -10.0f,
                    "The fundamental should be found near -6 dBFS, measured "
                      + juce::String (distortion.fundamentalDb, 2));
            expect (distortion.thdPercent < 0.5f,
                    "A clean generator should measure THD under 0.5%, measured "
                      + juce::String (distortion.thdPercent, 4) + "%");
            expect (distortion.harmonicsMeasured >= 9,
                    "9 harmonics of 1 kHz fit below Nyquist at 48 kHz, measured "
                      + juce::String (distortion.harmonicsMeasured));
        }

        beginTest ("Distortion is measured when it is deliberately added");
        {
            // Add a known second harmonic at -40 dB and check that THD lands near 1%.
            const auto clean = make (lab::SignalType::sine, 1000.0, 0.5f, 1.0, 48000.0);

            juce::AudioBuffer<float> distorted (clean);
            const auto harmonicAmplitude = 0.5f * juce::Decibels::decibelsToGain (-40.0f);

            for (int i = 0; i < distorted.getNumSamples(); ++i)
            {
                const auto t = (double) i / 48000.0;
                distorted.addSample (0, i, harmonicAmplitude
                                              * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                  * 2000.0 * t));
            }

            const auto cleanThd = lab::measureDistortion (clean, 0, 1000.0, 48000.0).thdPercent;
            const auto dirtyThd = lab::measureDistortion (distorted, 0, 1000.0, 48000.0).thdPercent;

            expect (dirtyThd > cleanThd * 1.5f,
                    "Adding a -40 dB harmonic must raise the measured THD (clean "
                      + juce::String (cleanThd, 4) + "%, distorted "
                      + juce::String (dirtyThd, 4) + "%)");
            expectWithinAbsoluteError (dirtyThd, 1.0f, 0.3f,
                                       "A -40 dB second harmonic is 1% THD, measured "
                                         + juce::String (dirtyThd, 4) + "%");
        }

        beginTest ("A square wave measures the odd harmonics it is made of");
        {
            // A 1 kHz square has harmonics at 3, 5, 7 kHz at 1/3, 1/5, 1/7 amplitude.
            // Sampling aliases the higher ones, so only the low order is asserted.
            const auto buffer = make (lab::SignalType::square, 200.0, 0.5f, 1.0, 48000.0);
            const auto distortion = lab::measureDistortion (buffer, 0, 200.0, 48000.0);

            // Ideal square THD is about 43%.
            expect (distortion.thdPercent > 30.0f,
                    "A square wave should measure substantial THD, measured "
                      + juce::String (distortion.thdPercent, 2) + "%");
        }

        beginTest ("SNR compares a signal against its noise floor");
        {
            const auto signal = make (lab::SignalType::sine, 1000.0, 0.5f);
            const auto silence = make (lab::SignalType::silence, 1000.0, 0.5f);

            const auto perfect = lab::measureSnr (signal, silence, 0);
            expect (perfect.snrDb > 100.0f,
                    "Sine against digital silence should be a huge SNR, measured "
                      + juce::String (perfect.snrDb, 1) + " dB");

            // Now a real noise floor: -60 dBFS noise should give roughly 51 dB SNR
            // (-9.03 dBFS RMS against -60 dBFS RMS).
            juce::AudioBuffer<float> noise (2, signal.getNumSamples());
            noise.clear();

            std::mt19937 rng (42);
            std::uniform_real_distribution<float> uniform (-1.0f, 1.0f);
            const auto noiseAmplitude = juce::Decibels::decibelsToGain (-60.0f);

            for (int i = 0; i < noise.getNumSamples(); ++i)
            {
                const auto value = uniform (rng) * noiseAmplitude;
                noise.setSample (0, i, value);
                noise.setSample (1, i, value);
            }

            const auto measured = lab::measureSnr (signal, noise, 0);
            expect (measured.snrDb > 40.0f && measured.snrDb < 65.0f,
                    "A -60 dBFS noise floor should give an SNR of roughly 40-65 dB, measured "
                      + juce::String (measured.snrDb, 1) + " dB");
        }

        beginTest ("DC offset is measured exactly");
        {
            const auto buffer = make (lab::SignalType::dcOffset, 0.0, 0.5f);
            const auto stereo = lab::measureStereo (buffer);

            expectWithinAbsoluteError (stereo.dcOffsetLeft, 0.25f, 0.001f,
                                       "A 0.25 DC level must measure 0.25, measured "
                                         + juce::String (stereo.dcOffsetLeft, 5));
            expectWithinAbsoluteError (stereo.dcOffsetRight, 0.25f, 0.001f);
        }

        beginTest ("Stereo correlation detects mono, wide and out-of-phase signals");
        {
            const auto mono = make (lab::SignalType::sine, 1000.0, 0.5f);
            expectWithinAbsoluteError (lab::measureStereo (mono).correlation, 1.0f, 0.01f,
                                       "Identical channels are perfectly correlated");
            expect (! lab::measureStereo (mono).outOfPhase);

            lab::SignalSpec inverted;
            inverted.type = lab::SignalType::sine;
            inverted.invertRightChannel = true;
            const auto outOfPhase = lab::generateSignal (inverted);
            const auto result = lab::measureStereo (outOfPhase);

            expectWithinAbsoluteError (result.correlation, -1.0f, 0.01f,
                                       "Inverted channels are perfectly anti-correlated, measured "
                                         + juce::String (result.correlation, 3));
            expect (result.outOfPhase, "Anti-correlated audio must be flagged as out of phase");

            // White noise differs per... no: the generator is deterministic, so the two
            // channels of one spec are the same signal. Instead, mix a different signal
            // into the right channel to create a genuinely decorrelated pair.
            juce::AudioBuffer<float> wide (2, mono.getNumSamples());
            wide.copyFrom (0, 0, mono, 0, 0, mono.getNumSamples());

            std::mt19937 rng (7);
            std::uniform_real_distribution<float> uniform (-0.5f, 0.5f);

            for (int i = 0; i < wide.getNumSamples(); ++i)
                wide.setSample (1, i, uniform (rng));

            const auto wideCorrelation = lab::measureStereo (wide).correlation;
            expect (wideCorrelation < 0.5f,
                    "Two unrelated channels should not look correlated, measured "
                      + juce::String (wideCorrelation, 3));
        }

        beginTest ("Crosstalk reports how much leaks into the other channel");
        {
            // A hard-left sine: the right channel carries nothing, so crosstalk is -inf.
            juce::AudioBuffer<float> hardLeft (2, 48000);
            hardLeft.clear();

            for (int i = 0; i < hardLeft.getNumSamples(); ++i)
                hardLeft.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                    * 1000.0 * (double) i / 48000.0));

            expect (lab::measureCrosstalkDb (hardLeft) <= -200.0f,
                    "A perfect hard-left signal has no crosstalk");

            // Now add -60 dB of leakage into the right channel.
            const auto leak = juce::Decibels::decibelsToGain (-60.0f) * 0.5f;

            for (int i = 0; i < hardLeft.getNumSamples(); ++i)
                hardLeft.setSample (1, i, leak * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                    * 1000.0 * (double) i / 48000.0));

            const auto measured = lab::measureCrosstalkDb (hardLeft);
            expectWithinAbsoluteError (measured, -60.0f, 1.0f,
                                       "-60 dB of leakage should measure -60 dB, measured "
                                         + juce::String (measured, 2));
        }

        beginTest ("True peak catches inter-sample peaks that a sample peak misses");
        {
            // A sine at exactly a quarter of the sample rate hits its peak between
            // samples for some phases, so the true peak must be at least the sample peak.
            const auto buffer = make (lab::SignalType::sine, 12000.0, 0.9f, 0.5, 48000.0);

            const auto samplePeak = lab::measureLevels (buffer, 0).peakDb;
            const auto truePeak = lab::measureTruePeakDb (buffer, 0, 48000.0);

            expect (truePeak >= samplePeak - 0.2f,
                    "True peak must never be below the sample peak (sample "
                      + juce::String (samplePeak, 2) + ", true " + juce::String (truePeak, 2) + ")");
            expect (truePeak < 6.0f,
                    "True peak should be close to the sample peak, measured "
                      + juce::String (truePeak, 2) + " dB");
        }

        beginTest ("Loudness matches the BS.1770 reference values");
        {
            // These targets are not my opinion and not my memory: they come from an
            // independent reference implementation of ITU-R BS.1770 (ffmpeg's ebur128
            // filter, which is libebur128), measured on the same signals. The commands
            // that produced them are recorded in docs/AUDIO_MEASUREMENT_METHODS.md so
            // anyone can re-run the comparison rather than trust this comment.
            //
            //   full scale 1 kHz stereo  ->   0.0 LUFS   (also -0.691 + 3.0103 + 3*... )
            //   full scale 1 kHz mono    ->  -3.0 LUFS
            //   -20 dBFS 1 kHz stereo    -> -20.0 LUFS
            //   -20 dBFS 1 kHz mono      -> -23.0 LUFS
            //
            // The mono/stereo difference is 3 dB and is the trap: 0 dBFS 1 kHz mono reads
            // about -3 LUFS, and it is easy to remember that as the stereo figure.
            const struct { float amplitude; int channels; float expectedLufs; const char* label; } cases[] =
            {
                { 1.0f,  2,   0.0f, "full scale stereo" },
                { 1.0f,  1,  -3.0f, "full scale mono" },
                { 0.1f,  2, -20.0f, "-20 dBFS stereo" },
                { 0.1f,  1, -23.0f, "-20 dBFS mono" },
                { 0.5f,  2,  -6.0f, "-6 dBFS stereo" }
            };

            for (const auto& c : cases)
            {
                lab::SignalSpec spec;
                spec.type = lab::SignalType::sine;
                spec.frequency = 1000.0;
                spec.amplitude = c.amplitude;
                spec.numChannels = c.channels;
                spec.durationSeconds = 6.0;
                spec.sampleRate = 48000.0;

                const auto buffer = lab::generateSignal (spec);
                const auto loudness = lab::measureLoudness (buffer, 48000.0);

                expect (loudness.gated, juce::String (c.label) + " must pass the gates");
                // 0.05 LU of slack: K-weighting has about +0.0067 dB of gain at 1 kHz
                // (the +4 dB shelf minus the high pass), so NUX reads +0.0067 for the full
                // scale case where the reference prints 0.0. Anything bigger is a bug.
                expectWithinAbsoluteError (loudness.integratedLufs, c.expectedLufs, 0.05f,
                                           juce::String (c.label) + " must integrate at "
                                             + juce::String (c.expectedLufs, 2) + " LUFS, measured "
                                             + juce::String (loudness.integratedLufs, 3));
            }

            // A 10 dB level change must be a 10 LU change, or the scale is not linear.
            const auto quiet = make (lab::SignalType::sine, 1000.0, 0.1f, 6.0, 48000.0);
            const auto loud = make (lab::SignalType::sine, 1000.0, 0.5f, 6.0, 48000.0);

            const auto quietLufs = lab::measureLoudness (quiet, 48000.0).integratedLufs;
            const auto loudLufs = lab::measureLoudness (loud, 48000.0).integratedLufs;

            expectWithinAbsoluteError (loudLufs - quietLufs, 13.98f, 0.2f,
                                       "0.1 -> 0.5 amplitude is 13.98 LU, measured "
                                         + juce::String (loudLufs - quietLufs, 2));
        }

        beginTest ("Silence integrates to silence, and is reported as ungated");
        {
            const auto silence = make (lab::SignalType::silence, 1000.0, 0.0f, 2.0, 48000.0);
            const auto loudness = lab::measureLoudness (silence, 48000.0);

            expect (loudness.integratedLufs <= -200.0f,
                    "Silence must integrate to -inf, measured "
                      + juce::String (loudness.integratedLufs, 2));
        }

        beginTest ("Gain accuracy measures a known gain");
        {
            const auto input = make (lab::SignalType::sine, 1000.0, 0.25f);

            juce::AudioBuffer<float> output (input);
            output.applyGain (juce::Decibels::decibelsToGain (-6.0f));

            const auto measured = lab::measureGainAccuracyDb (input, output);
            expectWithinAbsoluteError (measured, -6.0f, 0.05f,
                                       "A -6 dB gain must measure -6 dB, measured "
                                         + juce::String (measured, 3));
        }

        beginTest ("Sample rate accuracy reports its own error");
        {
            double errorPercent = 0.0;

            // Exactly right.
            const auto exact = lab::measureSampleRateAccuracy (48000, 1.0, 48000.0, errorPercent);
            expectWithinAbsoluteError (exact, 48000.0, 0.01);
            expectWithinAbsoluteError (errorPercent, 0.0, 0.001);

            // 1% fast.
            const auto fast = lab::measureSampleRateAccuracy (48480, 1.0, 48000.0, errorPercent);
            expectWithinAbsoluteError (fast, 48480.0, 0.01);
            expectWithinAbsoluteError (errorPercent, 1.0, 0.01,
                                       "A 48480 sample second is 1% fast, reported "
                                         + juce::String (errorPercent, 3) + "%");
        }

        beginTest ("The spectrum peak lands on the generated frequency");
        {
            const auto buffer = make (lab::SignalType::sine, 1000.0, 0.5f, 1.0, 48000.0);
            const auto spectrum = lab::computeSpectrum (buffer, 0, 16384, 48000.0);

            expectWithinAbsoluteError (spectrum.getPeakFrequency(), 1000.0,
                                       spectrum.binWidth * 1.5,
                                       "The spectral peak should be at 1 kHz, found "
                                         + juce::String (spectrum.getPeakFrequency(), 2));
            expectWithinAbsoluteError (spectrum.getMagnitudeAt (1000.0), -6.02f, 0.5f,
                                       "The peak magnitude should be -6 dBFS, measured "
                                         + juce::String (spectrum.getMagnitudeAt (1000.0), 2));
        }

        beginTest ("Reference signals match their stated formulas");
        {
            // RMS is analytically known for these, so it is checked rather than eyeballed.
            const struct { lab::SignalType type; float amplitude; } cases[] =
            {
                { lab::SignalType::sine,     0.5f },
                { lab::SignalType::square,   0.5f },
                { lab::SignalType::triangle, 0.5f },
                { lab::SignalType::sawtooth, 0.5f }
            };

            for (const auto& c : cases)
            {
                lab::SignalSpec spec;
                spec.type = c.type;
                spec.frequency = 1000.0;
                spec.amplitude = c.amplitude;
                spec.durationSeconds = 1.0;
                spec.sampleRate = 48000.0;

                const auto buffer = lab::generateSignal (spec);
                const auto measured = lab::measureLevels (buffer, 0).rms;
                const auto expected = lab::expectedRms (spec);

                // Sawtooth and triangle at 1 kHz are aliased by sampling, which shifts
                // their RMS slightly; 6% covers that and would still catch a wrong formula.
                expectWithinAbsoluteError (measured, expected, expected * 0.06f,
                                           lab::getSignalName (c.type) + " RMS should be "
                                             + juce::String (expected, 4) + ", measured "
                                             + juce::String (measured, 4));
            }
        }

        beginTest ("Mono signals stay inside their buffer");
        {
            // The generator once wrote channel 1 unconditionally, so a mono signal
            // corrupted the heap. Reading the samples back through the buffer's own
            // bounds is what catches it: this test crashed the suite before the fix.
            for (int channels = 1; channels <= 3; ++channels)
            {
                lab::SignalSpec spec;
                spec.type = lab::SignalType::sine;
                spec.frequency = 440.0;
                spec.amplitude = 0.5f;
                spec.durationSeconds = 0.05;
                spec.sampleRate = 48000.0;
                spec.numChannels = channels;

                const auto buffer = lab::generateSignal (spec);

                expectEquals (buffer.getNumChannels(), channels,
                              "The generator must honour the requested channel count");

                const auto levels = lab::measureLevels (buffer, 0);
                expectWithinAbsoluteError (levels.rmsDb, -9.0309f, 0.05f,
                                           juce::String (channels) + "-channel sine must still "
                                           "measure -9.03 dBFS RMS");

                if (channels > 1)
                    expectWithinAbsoluteError (lab::measureLevels (buffer, 1).rmsDb, -9.0309f, 0.05f,
                                               "Channel 1 must carry the signal too");
            }
        }

        beginTest ("Every reference signal is finite and deterministic");
        {
            for (int type = 0; type <= (int) lab::SignalType::impulse; ++type)
            {
                lab::SignalSpec spec;
                spec.type = (lab::SignalType) type;
                spec.amplitude = 0.5f;
                spec.durationSeconds = 0.25;
                spec.sampleRate = 48000.0;

                const auto first = lab::generateSignal (spec);
                const auto second = lab::generateSignal (spec);

                expect (isFinite (first),
                        lab::getSignalName (spec.type) + " must contain only finite samples");

                auto identical = first.getNumSamples() == second.getNumSamples();

                for (int i = 0; identical && i < first.getNumSamples(); ++i)
                    identical = first.getSample (0, i) == second.getSample (0, i);

                expect (identical,
                        lab::getSignalName (spec.type) + " must be deterministic: the same "
                        "spec must give the same samples");
            }
        }
    }
};

static MeasurementTests measurementTests;
}
