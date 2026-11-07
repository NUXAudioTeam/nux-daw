# NUX Audio Measurement Methods

This document explains how every number in the Virtual Audio Lab is produced, what it is
checked against, and where it is **not** trustworthy.

It exists so that no measurement result has to be taken on faith.

## What this document is not

NUX's measurements are **not certified, not calibrated, and not a substitute for lab
equipment.** They are software measurements against published standards. Nothing here has
been validated against an Audio Precision analyser, a real sound card's loopback, or a
reference microphone.

Specifically, the following have **never** been verified on real hardware:

- absolute level accuracy of any physical interface;
- whether a real device's clock is actually 48 kHz as it claims;
- THD produced by real converters, preamps or speakers;
- loudness of real programme material against a certified meter.

What *is* verified is the arithmetic: the maths is checked against analytically known
signals and, for loudness, against an independent implementation.

---

## 1. Reference signals — `TestSignals.{h,cpp}`

Every measurement starts from a signal whose correct value is known from its formula.

| Signal | Formula | RMS |
|---|---|---|
| sine | `A·sin(2πft)` | `A/√2` |
| square | `A·sign(sin(2πft))` | `A` |
| triangle | piecewise linear from phase | `A/√3` |
| sawtooth | piecewise linear from phase | `A/√3` |
| DC | constant | `|level|` |
| two tone | `A/2·(sin 2πf₁t + sin 2πf₂t)` | `A/(2√2)` |
| white noise | uniform, seeded | — |
| pink noise | Voss–McCartney, seeded | — |
| exponential sweep | integrated instantaneous phase | — |
| impulse | first sample only | `A/√N` |
| silence | zeros | 0 |

Noise generators are seeded, so a given seed always produces identical samples. The tests
assert determinism by generating each signal twice and comparing every sample.

The sweep integrates its instantaneous phase rather than multiplying time by frequency;
multiplying would make the frequency jump at every sample and destroy the sweep.

## 2. Levels — `measureLevels()`

Peak, RMS and crest factor in dBFS. Peak and RMS use `juce::AudioBuffer`'s own reduction
helpers.

- A **half-amplitude sine** must measure **−6.0206 dBFS peak** and **−9.0309 dBFS RMS**
  (RMS is peak − 3.0103 dB). Both are asserted to 0.02 dB.
- Silence reports at or below **−200 dBFS**, which this codebase treats as −∞. It is never
  reported as a small finite number, because a "-180 dB noise floor" is a fiction.
- **Clipping** uses the AES17 rule: **three consecutive** samples at or above full scale.
  A single full-scale sample is normal inside a loud sine and must not be reported as a
  clip. A test pins both halves of that rule.

## 3. Spectrum — `computeSpectrum()`

Hann window, real FFT via `juce::dsp::FFT`, magnitude scaled by `2 / Σwindow` so a
full-scale sine reads 0 dB at its bin.

**`getMagnitudeAt()` interpolates.** It fits a parabola through the three neighbouring
bins. This is not cosmetic: without it, the same sine reads up to ~1.4 dB differently
depending on where it falls relative to a bin centre, which would make every THD and level
check depend on the choice of FFT size. An earlier version of this code did a nearest-bin
lookup while its header claimed interpolation — the tests caught the lie.

A 1 kHz sine at 48 kHz with a 16384-point FFT must land within 1.5 bins of 1 kHz and read
**−6.02 ± 0.5 dBFS** at the peak.

## 4. Distortion — `measureDistortion()`

AES17 style, against a **known** fundamental frequency (not a guessed peak):

- `THD` = RMS sum of harmonics 2..N ÷ fundamental, as a percentage.
- `THD+N` = RMS of everything except the fundamental ÷ fundamental.
- `harmonicsMeasured` is reported, because "THD" over two harmonics is not THD.

Known-answer checks:

| Signal | Expected | Test |
|---|---|---|
| clean 1 kHz sine, 48 kHz | THD < 0.5 %, ≥ 9 harmonics | asserted |
| clean sine + −40 dB second harmonic | THD ≈ 1 % (0.3 % slack) | asserted |
| 200 Hz square | THD > 30 % (ideal ≈ 43 %) | asserted |

The "distortion added deliberately" test is the important one: it proves the measurement
rises when distortion actually exists, which a test that only measures clean sines cannot.

## 5. Noise and SNR — `measureSnr()`

SNR is signal RMS minus noise RMS, both in dB.

- A sine against digital silence must exceed **100 dB**.
- A sine against a **−60 dBFS** noise floor must land in **40–65 dB** (theory: ≈ 51 dB).
  The band is wide because uniform noise's RMS depends on the draw; the point is that the
  figure is in the right decade, not that it hits 51.0.

## 6. Stereo — `measureStereo()`, `measureCrosstalkDb()`

- Correlation: **+1.0** for identical channels, **−1.0** for inverted ones (both to 0.01);
  below −0.5 the result is flagged `outOfPhase`.
- DC offset is measured per channel and asserted to ±0.001.
- **Crosstalk is a ratio**: the quieter channel relative to the louder one. An earlier
  version returned the quiet channel's *absolute* level, which reported a healthy −60 dB
  leak as −69 dB because the source itself sat at −9 dBFS. Fixed, and pinned by a test that
  injects exactly −60 dB of leakage and requires −60 ± 1 dB within.
- A perfect hard-panned signal returns −∞ (< −200 dB).

## 7. True peak — `measureTruePeakDb()`

4× oversampling with `juce::dsp::Oversampling` (polyphase IIR, per BS.1770-4's
4× requirement), maximum of the interpolated samples.

Tested invariants: true peak is never more than 0.2 dB *below* the sample peak, and stays
within 6 dB of it for a 12 kHz sine at 48 kHz.

Limitation: the oversampling filter is not the specified 48-tap polyphase FIR, so an
absolute reading may differ from a certified meter by a few tenths of a dB. It is
trustworthy for *comparisons* (before/after, clip detection) and not certified for
absolute compliance reporting.

## 8. Loudness — `measureLoudness()`

ITU-R BS.1770-4 / EBU R128: K-weighting, 400 ms blocks at 75 % overlap, absolute gate at
−70 LUFS, relative gate at −10 LU below the gated mean, plus LRA from the 10th and 95th
percentiles of the gated blocks.

### K-weighting

The two biquads are **designed from the standard's pre-filter parameters**, not approximated
with generic shelves:

- stage 1, high shelf: `f0 = 1681.974450955533 Hz`, `G = 3.999843853973347 dB`,
  `Q = 0.7071752369554196`, with `Vb = Vh^0.4996667741545416`
- stage 2, high pass: `f0 = 38.13547087602444 Hz`, `Q = 0.5003270373238773`

This matters more than it sounds. The first implementation approximated stage 1 with
JUCE's `makeHighShelf(1500 Hz, Q 0.7071, +4 dB)`, which read a −20 dBFS stereo tone as
**−20.73 LUFS**. The correct design reads **−20.00**.

### Reference values, and how they were obtained

These are measured from **ffmpeg's `ebur128` filter (libebur128)**, an independent
implementation of the same standard, on identical 6-second signals:

| Signal | Reference | NUX | Status |
|---|---|---|---|
| full-scale 1 kHz, stereo | 0.0 LUFS | +0.007 | pass |
| full-scale 1 kHz, mono | −3.0 LUFS | −2.99 | pass |
| −20 dBFS 1 kHz, stereo | −20.0 LUFS | −20.00 | pass |
| −20 dBFS 1 kHz, mono | −23.0 LUFS | −23.00 | pass |
| −6 dBFS 1 kHz, stereo | −6.0 LUFS | −6.00 | pass |

To reproduce:

```bash
ffmpeg -nostats -i tone.wav -filter_complex ebur128 -f null -
```

Note the mono/stereo 3 dB difference. A full-scale 1 kHz **mono** tone reads about
**−3 LUFS**; the widely quoted "−3.01 LUFS for a full-scale sine" is the mono figure, and
it is easy to misremember as the stereo one. This document said so the hard way.

The full-scale case reads +0.0067 rather than a flat 0.0 because K-weighting genuinely has
about +0.0067 dB of gain at 1 kHz (the +4 dB shelf minus the high pass). ffmpeg prints
0.0 at one decimal place. The test allows ±0.05 LU.

### A bug this caught

Each block's loudness already included the standard's `−0.691` offset. The averaging code
converted blocks back to power with `10^(L/10)` and then added `−0.691` **again**,
subtracting the constant twice and reading every result **0.691 LU low**. The four
non-full-scale cases still landed inside a loose 0.2 LU tolerance, so only the full-scale
case exposed it — which is why the tolerance is now 0.05 LU.

## 9. Gain and sample-rate accuracy

- `measureGainAccuracyDb()`: RMS ratio in dB. A −6 dB gain must read −6.000 ± 0.05.
- `measureSampleRateAccuracy(actual, seconds, expected, errorOut)`: returns the implied
  rate and its percentage error. 48480 samples in one second against 48000 must report
  1.000 % ± 0.01.

## 10. What is still not verified

- No measurement here has been compared against a calibrated meter or analyser.
- No reading has been taken from physical hardware. Every result so far comes from
  generated signals inside the process.
- True peak uses an IIR oversampler, not the specified FIR (§7).
- Loudness uses two channels with weight 1.0; surround layouts are not handled because NUX
  is a stereo engine today.
- Rectangular-window and swept-sine frequency response are not implemented yet; `sineSweep`
  exists as a signal but is not yet analysed.

Anything that says "verified" in NUX documentation means "checked against a known answer in
software". It never means "checked against a real device".
