#include "Measurements.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace nux::lab
{
namespace
{
constexpr float minusInfinity = -200.0f;

float toDb (float linear)
{
    return linear > 0.0f ? juce::Decibels::gainToDecibels (linear, minusInfinity) : minusInfinity;
}

/** Rounds up to a power of two. */
int nextPowerOfTwo (int value)
{
    int result = 1;

    while (result < value)
        result <<= 1;

    return result;
}
}

//==============================================================================
LevelMeasurement measureLevels (const juce::AudioBuffer<float>& buffer, int channel)
{
    LevelMeasurement result;

    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() == 0)
        return result;

    const auto* data = buffer.getReadPointer (channel);
    const auto n = buffer.getNumSamples();

    result.peak = buffer.getMagnitude (channel, 0, n);
    result.rms = buffer.getRMSLevel (channel, 0, n);
    result.peakDb = toDb (result.peak);
    result.rmsDb = toDb (result.rms);
    result.crestFactorDb = result.peakDb - result.rmsDb;

    // AES17 counts a clip as three or more consecutive samples at full scale, which is
    // what distinguishes a real overload from a single full-scale sample in a sine.
    int consecutiveAtFullScale = 0;

    for (int i = 0; i < n; ++i)
    {
        if (std::abs (data[i]) >= 0.999f)
        {
            if (++consecutiveAtFullScale >= 3)
            {
                result.clipped = true;
                break;
            }
        }
        else
        {
            consecutiveAtFullScale = 0;
        }
    }

    return result;
}

//==============================================================================
int SpectrumResult::getPeakBin() const
{
    if (magnitudesDb.empty())
        return 0;

    return (int) std::distance (magnitudesDb.begin(),
                                std::max_element (magnitudesDb.begin(), magnitudesDb.end()));
}

double SpectrumResult::getPeakFrequency() const
{
    const auto bin = getPeakBin();

    if (bin < 0 || bin >= (int) frequencies.size())
        return 0.0;

    return frequencies[(size_t) bin];
}

float SpectrumResult::getMagnitudeAt (double frequencyHz) const
{
    if (binWidth <= 0.0 || magnitudesDb.size() < 3)
        return minusInfinity;

    const auto position = frequencyHz / binWidth;
    const auto nearest = (int) std::round (position);

    if (nearest < 1 || nearest >= (int) magnitudesDb.size() - 1)
    {
        if (nearest >= 0 && nearest < (int) magnitudesDb.size())
            return magnitudesDb[(size_t) nearest];

        return minusInfinity;
    }

    // Parabolic interpolation through the three bins around the requested frequency.
    // Without this a bin centre and a frequency between bins read up to 1.4 dB apart for
    // a Hann window, which would make every level check misleadingly strict.
    const auto l1 = magnitudesDb[(size_t) (nearest - 1)];
    const auto l2 = magnitudesDb[(size_t) nearest];
    const auto l3 = magnitudesDb[(size_t) (nearest + 1)];

    if (l2 <= minusInfinity + 1.0f)
        return l2;

    const auto alpha = l1, beta = l2, gamma = l3;
    const auto denominator = alpha - 2.0f * beta + gamma;

    if (std::abs (denominator) < 1.0e-9f)
        return beta;

    const auto p = 0.5f * (alpha - gamma) / denominator;   // -0.5 .. +0.5 from the peak

    return beta - 0.25f * (alpha - gamma) * p;
}

SpectrumResult computeSpectrum (const juce::AudioBuffer<float>& buffer, int channel,
                                int fftSize, double sampleRate)
{
    SpectrumResult result;
    result.fftSize = nextPowerOfTwo (juce::jmax (256, fftSize));
    result.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    result.binWidth = result.sampleRate / (double) result.fftSize;

    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() == 0)
        return result;

    const auto order = (int) std::log2 ((double) result.fftSize);

    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window ((size_t) result.fftSize,
                                               juce::dsp::WindowingFunction<float>::hann,
                                               false);

    // Zero-pad so a short block still fills the transform; that trades resolution for
    // bin interpolation rather than inventing data.
    std::vector<float> data ((size_t) result.fftSize * 2, 0.0f);

    const auto toCopy = juce::jmin (buffer.getNumSamples(), result.fftSize);
    std::copy (buffer.getReadPointer (channel),
               buffer.getReadPointer (channel) + toCopy,
               data.begin());

    window.multiplyWithWindowingTable (data.data(), (size_t) result.fftSize);
    fft.performFrequencyOnlyForwardTransform (data.data());

    // A Hann window halves the coherent gain: scaling by 2 / sum(window) restores the
    // amplitude so a full-scale sine reads 0 dB.
    const auto windowSum = (float) result.fftSize * 0.5f;
    const auto scale = 2.0f / windowSum;

    const auto numBins = result.fftSize / 2;
    result.magnitudesDb.resize ((size_t) numBins);
    result.frequencies.resize ((size_t) numBins);

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto linear = data[(size_t) bin] * scale;
        result.magnitudesDb[(size_t) bin] = toDb (linear);
        result.frequencies[(size_t) bin] = (double) bin * result.binWidth;
    }

    return result;
}

//==============================================================================
DistortionResult measureDistortion (const juce::AudioBuffer<float>& buffer, int channel,
                                    double fundamentalHz, double sampleRate,
                                    int fftSize, int maxHarmonics)
{
    DistortionResult result;
    result.fundamentalHz = fundamentalHz;

    const auto spectrum = computeSpectrum (buffer, channel, fftSize, sampleRate);

    if (spectrum.magnitudesDb.empty() || fundamentalHz <= 0.0)
        return result;

    const auto nyquist = sampleRate * 0.5;
    const auto fundamentalDb = spectrum.getMagnitudeAt (fundamentalHz);
    result.fundamentalDb = fundamentalDb;

    if (fundamentalDb <= minusInfinity + 1.0f)
        return result;   // nothing to measure against

    const auto fundamentalLinear = juce::Decibels::decibelsToGain (fundamentalDb);

    // Sum the power of every harmonic that actually fits below Nyquist.
    double harmonicPower = 0.0;
    int measured = 0;

    for (int h = 2; h <= maxHarmonics; ++h)
    {
        const auto harmonicHz = fundamentalHz * (double) h;

        if (harmonicHz >= nyquist * 0.98)   // stay clear of the last bins
            break;

        const auto harmonicDb = spectrum.getMagnitudeAt (harmonicHz);
        const auto harmonicLinear = juce::Decibels::decibelsToGain (harmonicDb);
        harmonicPower += (double) harmonicLinear * (double) harmonicLinear;
        ++measured;
    }

    result.harmonicsMeasured = measured;

    if (fundamentalLinear <= 0.0f)
        return result;

    const auto thdRatio = std::sqrt (harmonicPower) / (double) fundamentalLinear;
    result.thdPercent = (float) (thdRatio * 100.0);
    result.thdDb = toDb ((float) thdRatio);

    // THD+N needs the residual: everything except the fundamental and its own leakage.
    // The fundamental is excluded with a small window so Hann leakage does not count as
    // noise, which is the usual practical compromise and is documented as such.
    double totalPower = 0.0;
    double noisePower = 0.0;
    int noiseBins = 0;

    const auto fundamentalBin = (int) std::round (fundamentalHz / spectrum.binWidth);
    const auto exclude = juce::jmax (2, spectrum.fftSize / 4096);

    for (int bin = 1; bin < (int) spectrum.magnitudesDb.size(); ++bin)
    {
        const auto linear = juce::Decibels::decibelsToGain (spectrum.magnitudesDb[(size_t) bin]);
        const auto power = (double) linear * (double) linear;
        totalPower += power;

        if (std::abs (bin - fundamentalBin) > exclude)
        {
            noisePower += power;
            ++noiseBins;
        }
    }

    if (noiseBins > 0)
        result.noiseFloorDb = toDb ((float) std::sqrt (noisePower / (double) noiseBins));

    if (totalPower > 0.0)
    {
        const auto ratio = std::sqrt (noisePower / totalPower);
        result.thdPlusNoisePercent = (float) (ratio * 100.0);
        result.thdPlusNoiseDb = toDb ((float) ratio);
    }

    return result;
}

//==============================================================================
NoiseResult measureSnr (const juce::AudioBuffer<float>& signal,
                        const juce::AudioBuffer<float>& noise,
                        int channel)
{
    NoiseResult result;

    const auto signalLevels = measureLevels (signal, channel);
    const auto noiseLevels = measureLevels (noise, channel);

    result.signalRmsDb = signalLevels.rmsDb;
    result.noiseRmsDb = noiseLevels.rmsDb;
    result.noiseFloorDb = noiseLevels.peakDb;

    if (signalLevels.rms > 0.0f)
        result.snrDb = 20.0f * std::log10 (signalLevels.rms / juce::jmax (noiseLevels.rms, 1.0e-9f));
    else
        result.snrDb = minusInfinity;

    return result;
}

//==============================================================================
StereoResult measureStereo (const juce::AudioBuffer<float>& buffer)
{
    StereoResult result;

    if (buffer.getNumChannels() < 1 || buffer.getNumSamples() == 0)
        return result;

    const auto* left = buffer.getReadPointer (0);
    const auto* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : left;
    const auto n = buffer.getNumSamples();

    double sumLeft = 0.0, sumRight = 0.0;

    for (int i = 0; i < n; ++i)
    {
        sumLeft += left[i];
        sumRight += right[i];
    }

    result.dcOffsetLeft = (float) (sumLeft / (double) n);
    result.dcOffsetRight = (float) (sumRight / (double) n);

    const auto rmsLeft = buffer.getRMSLevel (0, 0, n);
    const auto rmsRight = buffer.getNumChannels() > 1 ? buffer.getRMSLevel (1, 0, n) : rmsLeft;

    if (rmsLeft > 0.0f && rmsRight > 0.0f)
        result.balanceDb = 20.0f * std::log10 (rmsLeft / rmsRight);

    // Pearson correlation between the channels.
    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto l = (double) left[i] - (double) result.dcOffsetLeft;
        const auto r = (double) right[i] - (double) result.dcOffsetRight;
        sumLR += l * r;
        sumLL += l * l;
        sumRR += r * r;
    }

    if (sumLL > 0.0 && sumRR > 0.0)
        result.correlation = (float) (sumLR / std::sqrt (sumLL * sumRR));

    result.outOfPhase = result.correlation < -0.5f;

    // Side/mid energy ratio is a practical width measure.
    double midPower = 0.0, sidePower = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto mid = ((double) left[i] + (double) right[i]) * 0.5;
        const auto side = ((double) left[i] - (double) right[i]) * 0.5;
        midPower += mid * mid;
        sidePower += side * side;
    }

    if (sidePower > 0.0)
        result.sideToMidDb = toDb ((float) std::sqrt (sidePower / juce::jmax (midPower, 1.0e-12)));
    else
        result.sideToMidDb = minusInfinity;

    return result;
}

float measureCrosstalkDb (const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() < 2 || buffer.getNumSamples() == 0)
        return minusInfinity;

    const auto leftRms = buffer.getRMSLevel (0, 0, buffer.getNumSamples());
    const auto rightRms = buffer.getRMSLevel (1, 0, buffer.getNumSamples());

    const auto loud = juce::jmax (leftRms, rightRms);
    const auto quiet = juce::jmin (leftRms, rightRms);

    if (loud <= 0.0f)
        return minusInfinity;   // both channels silent: nothing to leak from

    // Crosstalk is a ratio: leakage in the quiet channel relative to the loud one.
    // Returning the absolute level of the quiet channel would report -69 dB for a
    // correctly isolated -60 dB leak, because the source itself sits at -9 dBFS.
    if (quiet <= 0.0f)
        return minusInfinity;

    return toDb (quiet / loud);
}

//==============================================================================
float measureTruePeakDb (const juce::AudioBuffer<float>& buffer, int channel,
                         double sampleRate)
{
    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() == 0)
        return minusInfinity;

    juce::ignoreUnused (sampleRate);

    // 4x oversampling through a low-passed interpolation, per BS.1770-4. The extra
    // samples catch inter-sample peaks that a plain sample peak misses.
    constexpr int oversampling = 4;

    juce::dsp::Oversampling<float> oversampler (1, 2,   // 2^2 = 4x
                                                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
    oversampler.initProcessing (512);

    const auto n = buffer.getNumSamples();
    const auto* data = buffer.getReadPointer (channel);

    auto truePeak = 0.0f;

    for (int start = 0; start < n; start += 512)
    {
        const auto blockLength = juce::jmin (512, n - start);

        juce::AudioBuffer<float> block (1, blockLength);
        block.copyFrom (0, 0, data + start, blockLength);

        juce::dsp::AudioBlock<float> audioBlock (block);
        auto oversampled = oversampler.processSamplesUp (audioBlock);

        for (size_t ch = 0; ch < oversampled.getNumChannels(); ++ch)
            for (size_t i = 0; i < oversampled.getNumSamples(); ++i)
                truePeak = juce::jmax (truePeak, std::abs (oversampled.getChannelPointer (ch)[i]));
    }

    oversampler.reset();

    return toDb (truePeak);
}

//==============================================================================
LoudnessResult measureLoudness (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    LoudnessResult result;

    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0 || sampleRate <= 0.0)
        return result;

    // BS.1770-4 K-weighting: a high-shelf pre-filter followed by a high-pass, applied
    // per channel.
    //
    // These biquads are designed from the pre-filter parameters the standard publishes for
    // 48 kHz and are re-derived for whatever rate is running. Approximating them with a
    // generic shelf and high-pass is not good enough: using JUCE's makeHighShelf at
    // 1500 Hz read a -20 dBFS stereo tone as -20.73 LUFS where the reference
    // implementation (ffmpeg / libebur128) reads exactly -20.00. One third of a unit
    // sounds like nothing until it is the number reported to a mastering engineer.
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

        void reset() { x1 = x2 = y1 = y2 = 0.0; }

        double process (double x) noexcept
        {
            const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;

            if (! std::isfinite (y))
            {
                reset();
                return 0.0;
            }

            return y;
        }
    };

    const auto designShelf = [] (Biquad& f, double fs)
    {
        // Stage 1: high shelf, +4 dB above about 1.68 kHz.
        constexpr double f0 = 1681.974450955533;
        constexpr double gainDb = 3.999843853973347;
        constexpr double q = 0.7071752369554196;

        const auto k = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const auto vh = std::pow (10.0, gainDb / 20.0);
        const auto vb = std::pow (vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;

        f.b0 = (vh + vb * k / q + k * k) / a0;
        f.b1 = 2.0 * (k * k - vh) / a0;
        f.b2 = (vh - vb * k / q + k * k) / a0;
        f.a1 = 2.0 * (k * k - 1.0) / a0;
        f.a2 = (1.0 - k / q + k * k) / a0;
        f.reset();
    };

    const auto designHighPass = [] (Biquad& f, double fs)
    {
        // Stage 2: high pass at about 38 Hz.
        constexpr double f0 = 38.13547087602444;
        constexpr double q = 0.5003270373238773;

        const auto k = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const auto a0 = 1.0 + k / q + k * k;

        f.b0 = 1.0;
        f.b1 = -2.0;
        f.b2 = 1.0;
        f.a1 = 2.0 * (k * k - 1.0) / a0;
        f.a2 = (1.0 - k / q + k * k) / a0;
        f.reset();
    };

    const auto numChannels = juce::jmin (2, buffer.getNumChannels());
    const auto n = buffer.getNumSamples();

    std::vector<double> weighted ((size_t) n, 0.0);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        Biquad shelf, highPass;
        designShelf (shelf, sampleRate);
        designHighPass (highPass, sampleRate);

        for (int i = 0; i < n; ++i)
        {
            auto sample = highPass.process (shelf.process ((double) buffer.getSample (ch, i)));

            // Channel weighting is 1.0 for left and right.
            weighted[(size_t) i] += sample * sample;
        }
    }

    // 400 ms blocks with 75% overlap, per the standard.
    const auto blockSamples = juce::jmax (1, (int) std::round (sampleRate * 0.4));
    const auto hopSamples  = juce::jmax (1, blockSamples / 4);

    std::vector<double> blockLoudness;

    for (int start = 0; start + blockSamples <= n; start += hopSamples)
    {
        double sum = 0.0;

        for (int i = start; i < start + blockSamples; ++i)
            sum += weighted[(size_t) i];

        const auto meanSquare = sum / (double) blockSamples;

        if (meanSquare <= 0.0)
        {
            blockLoudness.push_back (-200.0);
            continue;
        }

        // LUFS = -0.691 + 10 log10(sum of weighted mean squares)
        blockLoudness.push_back (-0.691 + 10.0 * std::log10 (meanSquare));
    }

    if (blockLoudness.empty())
        return result;

    // Ungated mean.
    {
        double sum = 0.0;
        int counted = 0;

        for (const auto value : blockLoudness)
            if (value > -200.0)
            {
                sum += std::pow (10.0, value / 10.0);
                ++counted;
            }

        if (counted > 0)
            // blockLoudness values are already in LUFS, so the -0.691 constant is baked in.
            // Adding it again here would subtract it twice and read every measurement
            // 0.691 LU low.
            result.ungatedLufs = (float) (10.0 * std::log10 (sum / counted));
    }

    // Gate 1: absolute -70 LUFS. Gate 2: relative -10 LU below the gated mean.
    std::vector<double> afterAbsoluteGate;

    for (const auto value : blockLoudness)
        if (value > -70.0)
            afterAbsoluteGate.push_back (value);

    result.gated = ! afterAbsoluteGate.empty();

    if (afterAbsoluteGate.empty())
        return result;

    double sum = 0.0;

    for (const auto value : afterAbsoluteGate)
        sum += std::pow (10.0, value / 10.0);

    const auto relativeThreshold = 10.0 * std::log10 (sum / (double) afterAbsoluteGate.size()) - 10.0;

    std::vector<double> gated;

    for (const auto value : afterAbsoluteGate)
        if (value > relativeThreshold)
            gated.push_back (value);

    if (gated.empty())
        return result;

    sum = 0.0;

    for (const auto value : gated)
        sum += std::pow (10.0, value / 10.0);

    result.integratedLufs = (float) (10.0 * std::log10 (sum / (double) gated.size()));

    // Momentary (400 ms) and short-term (3 s) maxima, straight from the block values.
    result.momentaryMaxLufs = (float) *std::max_element (blockLoudness.begin(), blockLoudness.end());

    const auto shortTermBlocks = juce::jmax (1, (int) (3.0 / 0.1));
    double shortTermMax = -200.0;

    for (size_t i = 0; i + (size_t) shortTermBlocks <= blockLoudness.size(); ++i)
    {
        double acc = 0.0;
        int counted = 0;

        for (size_t j = i; j < i + (size_t) shortTermBlocks; ++j)
            if (blockLoudness[j] > -200.0)
            {
                acc += std::pow (10.0, blockLoudness[j] / 10.0);
                ++counted;
            }

        if (counted > 0)
            shortTermMax = juce::jmax (shortTermMax, 10.0 * std::log10 (acc / counted));
    }

    result.shortTermMaxLufs = (float) shortTermMax;

    // Loudness range: 10th to 95th percentile of the gated blocks.
    auto sorted = gated;
    std::sort (sorted.begin(), sorted.end());

    const auto lowIndex  = (size_t) (0.10 * (double) (sorted.size() - 1));
    const auto highIndex = (size_t) (0.95 * (double) (sorted.size() - 1));
    result.loudnessRangeLu = (float) (sorted[highIndex] - sorted[lowIndex]);

    return result;
}

//==============================================================================
float measureGainAccuracyDb (const juce::AudioBuffer<float>& input,
                             const juce::AudioBuffer<float>& output)
{
    const auto inRms = input.getRMSLevel (0, 0, input.getNumSamples());
    const auto outRms = output.getRMSLevel (0, 0, output.getNumSamples());

    if (inRms <= 0.0f || outRms <= 0.0f)
        return 0.0f;

    return 20.0f * std::log10 (outRms / inRms);
}

double measureSampleRateAccuracy (juce::int64 numSamples, double elapsedSeconds,
                                  double expectedRate, double& errorPercent)
{
    errorPercent = 0.0;

    if (elapsedSeconds <= 0.0 || expectedRate <= 0.0)
        return 0.0;

    const auto measured = (double) numSamples / elapsedSeconds;
    errorPercent = (measured - expectedRate) / expectedRate * 100.0;

    return measured;
}

double measureFrequencyAccurate (const juce::AudioBuffer<float>& buffer, int channel,
                                 double sampleRate, int fftSize)
{
    const auto spectrum = computeSpectrum (buffer, channel, fftSize, sampleRate);

    if (spectrum.magnitudesDb.size() < 3)
        return 0.0;

    const auto bin = spectrum.getPeakBin();

    if (bin <= 0 || bin >= (int) spectrum.magnitudesDb.size() - 1)
        return spectrum.getPeakFrequency();

    // Parabolic interpolation over the three bins around the peak: the true peak of a
    // windowed sine sits between bins, and this recovers a fraction of the bin width.
    const auto left   = spectrum.magnitudesDb[(size_t) bin - 1];
    const auto centre = spectrum.magnitudesDb[(size_t) bin];
    const auto right  = spectrum.magnitudesDb[(size_t) bin + 1];

    const auto denominator = (left - 2.0f * centre + right);
    const auto offset = std::abs (denominator) > 1.0e-9f
                          ? 0.5f * (left - right) / denominator
                          : 0.0f;

    const auto refinedBin = (double) bin + (double) juce::jlimit (-0.5f, 0.5f, offset);

    return refinedBin * spectrum.binWidth;
}
}
