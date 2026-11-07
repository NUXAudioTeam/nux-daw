/* NUX DAW - audio measurement primitives.

   The maths behind the Virtual Audio Lab. Everything here works on a block of samples
   and returns a number, so it can be pointed at a rendered block, an exported file or a
   captured take with no changes.

   Standards notes (documented properly in docs/AUDIO_MEASUREMENT_METHODS.md):
     * THD / THD+N follow the AES17 definitions (ratio of the distortion components to
       the fundamental, and the residual to the total).
     * Loudness follows the ITU-R BS.1770 / EBU R128 chain: K-weighting, mean square over
       gated 400 ms blocks, -70 LUFS absolute gate and -10 LU relative gate.
     * True peak follows the BS.1770-4 4x oversampling rule.

   IMPORTANT: implementing a measurement *procedure* is not certification. Nothing here
   has been checked against calibrated laboratory equipment, and the header says so at
   every entry point that could be mistaken for one. */
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace nux::lab
{
//==============================================================================
/** The basic level numbers for one channel. */
struct LevelMeasurement
{
    float peak = 0.0f;          // absolute sample peak, linear
    float rms = 0.0f;           // root mean square, linear
    float peakDb = -200.0f;     // dBFS
    float rmsDb = -200.0f;
    float crestFactorDb = 0.0f; // peak - rms
    bool clipped = false;       // 3 or more consecutive samples at full scale (AES17)
};

LevelMeasurement measureLevels (const juce::AudioBuffer<float>& buffer, int channel = 0);

//==============================================================================
/** Spectrum of a block, in dB relative to full scale. */
struct SpectrumResult
{
    std::vector<float> magnitudesDb;   // one entry per bin, 0 .. fftSize/2
    std::vector<float> frequencies;    // matching bin centre frequencies
    double binWidth = 0.0;             // Hz per bin
    int fftSize = 0;
    double sampleRate = 0.0;

    /** The bin with the largest magnitude. */
    int getPeakBin() const;

    /** The frequency of the largest bin. */
    double getPeakFrequency() const;

    /** The magnitude of an arbitrary frequency, in dBFS.

        Parabolic interpolation through the three neighbouring bins, so a frequency that
        falls between bin centres reads its true level rather than the nearest bin's.
        Without that, the same sine reads up to about 1.4 dB differently depending on
        where it lands relative to a bin, which would ruin a THD measurement. */
    float getMagnitudeAt (double frequencyHz) const;
};

/** Hann-windowed FFT. `fftSize` is rounded up to a power of two. */
SpectrumResult computeSpectrum (const juce::AudioBuffer<float>& buffer, int channel,
                                int fftSize, double sampleRate);

//==============================================================================
/** Harmonic distortion, measured against a known fundamental. */
struct DistortionResult
{
    double fundamentalHz = 0.0;
    float fundamentalDb = 0.0f;
    float thdPercent = 0.0f;        // sqrt(sum of harmonics^2) / fundamental * 100
    float thdDb = -200.0f;
    float thdPlusNoisePercent = 0.0f;
    float thdPlusNoiseDb = -200.0f;
    float noiseFloorDb = -200.0f;   // mean of the non-fundamental, non-harmonic bins
    int harmonicsMeasured = 0;
};

/** Measures THD and THD+N for a signal whose fundamental is at `fundamentalHz`.

    Only the first `maxHarmonics` harmonics that fit below Nyquist are included, which is
    what AES17 requires: harmonics above Nyquist are not measurable and must not be
    counted as distortion. */
DistortionResult measureDistortion (const juce::AudioBuffer<float>& buffer, int channel,
                                    double fundamentalHz, double sampleRate,
                                    int fftSize = 16384, int maxHarmonics = 10);

//==============================================================================
/** Signal to noise ratio and dynamic range. */
struct NoiseResult
{
    float signalRmsDb = -200.0f;
    float noiseRmsDb = -200.0f;
    float snrDb = 0.0f;
    float noiseFloorDb = -200.0f;
};

/** SNR from a signal buffer and a separate silent (or noise-only) buffer. */
NoiseResult measureSnr (const juce::AudioBuffer<float>& signal,
                        const juce::AudioBuffer<float>& noise,
                        int channel = 0);

//==============================================================================
/** DC offset and stereo behaviour. */
struct StereoResult
{
    float dcOffsetLeft = 0.0f;
    float dcOffsetRight = 0.0f;
    float balanceDb = 0.0f;          // left vs right RMS, positive means left is louder
    float correlation = 0.0f;        // -1 .. +1
    float sideToMidDb = -200.0f;     // stereo width
    bool outOfPhase = false;         // correlation < -0.5
};

StereoResult measureStereo (const juce::AudioBuffer<float>& buffer);

/** Left/right channel separation in dB (more negative is better).

    Returns the level of the quieter channel **relative to** the louder one, which is what
    crosstalk means. A perfect hard-left signal returns -inf; a signal with -60 dB of
    leakage returns about -60 dB. It does not report the absolute level of the quiet
    channel, which would misreport a healthy -60 dB leak as -69 dB.

    Two channels carrying similar levels (a finished stereo mix) are not a crosstalk
    measurement: use measureStereo() correlation for that, and note this function's value
    is only meaningful when one channel is deliberately driven and the other should be
    silent. */
float measureCrosstalkDb (const juce::AudioBuffer<float>& buffer);

//==============================================================================
/** True peak, following the BS.1770-4 rule of 4x oversampling. */
float measureTruePeakDb (const juce::AudioBuffer<float>& buffer, int channel,
                         double sampleRate);

//==============================================================================
/** Loudness, gated as BS.1770-4 / EBU R128 specifies, plus the un-gated values so a
    caller can see the difference. */
struct LoudnessResult
{
    float integratedLufs = -200.0f;      // gated
    float ungatedLufs = -200.0f;
    float shortTermMaxLufs = -200.0f;    // 3 s window, maximum
    float momentaryMaxLufs = -200.0f;    // 400 ms window, maximum
    float loudnessRangeLu = 0.0f;        // LRA: 10th to 95th percentile
    bool gated = true;
};

LoudnessResult measureLoudness (const juce::AudioBuffer<float>& buffer, double sampleRate);

//==============================================================================
/** Gain accuracy: renders the difference between input and output as a gain in dB. */
float measureGainAccuracyDb (const juce::AudioBuffer<float>& input,
                             const juce::AudioBuffer<float>& output);

/** The measured sample rate, from the number of samples and the elapsed time. */
double measureSampleRateAccuracy (juce::int64 numSamples, double elapsedSeconds,
                                  double expectedRate, double& errorPercent);

/** Frequency, accurate to a fraction of a bin, by parabolic interpolation around the
    spectral peak. Far more accurate than zero-crossing counting. */
double measureFrequencyAccurate (const juce::AudioBuffer<float>& buffer, int channel,
                                 double sampleRate, int fftSize = 32768);
}
