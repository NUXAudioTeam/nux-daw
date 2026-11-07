# NUX DAW - Audio Test Report

**Date:** 2026-11-07
**Branch:** `feature/audio-testing`
**Build:** CMake + Ninja, Release, Linux x86_64
**Test suite:** `All NUX tests passed.` - **84 test groups, 0 failures**
(13 offline audio + 7 simulated recording + 6 test tone/metering + 6 device inventory
+ 52 pre-existing engine, project, plugin and UI groups)

This report separates what was actually verified from what was not. The machine that ran
these tests has **no audio hardware and no virtual audio cable installed**, so no sound
was ever produced or captured by a real device during this run.

---

## 1. Summary

| Category | Status |
|---|---|
| Offline (device independent) audio tests | ✅ **13 groups, all passed** |
| Measurement / instrumentation maths (step A) | ✅ **20 groups, all passed** |
| Software round trip + verification levels (step B) | ✅ **10 groups, all passed** |
| Windows CI: build Debug + Release | ✅ **builds, 15/15 ctest** (Debug leak check clean) |
| Simulated input recording | ✅ **7 groups, all passed** |
| Test tone / metering / device recovery | ✅ **6 groups, all passed** |
| Device inventory + virtual device detection | ✅ **6 groups, all passed** (logic only) |
| Physical hardware output | ❌ **NOT PERFORMED** - no device on this machine |
| Physical hardware input (microphone) | ❌ **NOT PERFORMED** - no device on this machine |
| Virtual audio device (VB-CABLE) | ❌ **NOT PERFORMED** - not installed |
| ASIO driver | ❌ **NOT PERFORMED** - not installed, Linux build |
| WASAPI loopback capture | ⚠️ **NOT SUPPORTED** - see section 7 |

---

## 2. Tests passed — offline, no hardware required

These were executed and passed. They exercise the real engine, not stubs.

### Signal correctness
- A 440 Hz tone is accurate at **44100, 48000, 88200 and 96000 Hz** (zero-crossing
  measurement, 0.5% tolerance).
- The tone level in dBFS matches the requested level (within 1 dB).
- The tone **never clips** at 25%, 50%, 75% or 100% level, and keeps headroom below
  0 dBFS at every level tested.
- Both channels carry an identical tone (largest difference < 0.0001).

### Ramps and click-free switching
- Turning the tone on and off produces **ramps, not steps**: the largest sample-to-sample
  difference stays well inside the natural slope of a 440 Hz sine.
- The tone reaches true silence after being switched off.

### Buffer processing
- The engine renders correctly at **every block size from 32 to 2048 samples**
  (32, 64, 128, 256, 512, 1024, 2048). No invalid samples, audible output at every size,
  no clipping.

### Sample rate handling
- Preparing at **44100, 48000, 96000 and 192000 Hz** produces valid audio on the first
  block and does not clip.

### Determinism
- Two identical renders of the same input produce **bit-identical output**
  (largest difference < 1e-6). This is what makes the rest of these tests meaningful.

### Mixing
- A note on the built-in synth produces audio and lands in the master mix.
- Mute silences the track; solo keeps the soloed track audible.
- The master fader scales program material.
- **The test tone deliberately ignores mutes and the master fader** - see section 6.

### WAV writing and reading
- A rendered take is written as a 24-bit WAV and reads back with the correct frequency
  (440 Hz ± 3 Hz) and the correct peak level (± 0.02), without clipping, with only finite
  samples.
- A malformed/aborted take deletes its file rather than leaving a broken WAV behind.

---

## 3. Tests passed — simulated input recording

No microphone was used. A known signal was pushed through the same ring buffer the audio
thread writes into, then drained to disk.

| Test | Result |
|---|---|
| A take is written to a readable WAV file | ✅ 24-bit, 2 channels, length accurate to 1500 samples over 1 second |
| The take remembers where it started on the timeline | ✅ start position preserved |
| Pushing audio while not recording writes nothing | ✅ no file created |
| A mono input is captured to a stereo take | ✅ both channels carry the signal |
| A silent input is reported as silent | ✅ level meter stays at zero |
| Aborting a take removes the file | ✅ no orphan file |
| Starting a second take while one is running is refused | ✅ refusal includes a message |

**This proves the recorder logic, buffer handling, file writing and file integrity. It
does not prove that a microphone is captured.**

---

## 4. Tests passed — test tone, metering, device recovery

| Test | Result |
|---|---|
| The test tone is a 440 Hz sine on both channels | ✅ |
| The test tone does not clip at full level | ✅ |
| The tone ramps in and out instead of clicking | ✅ |
| A stopped test tone contributes nothing to the mix | ✅ |
| The output meter follows what was actually sent | ✅ including panic clearing the meters |
| The engine reports a lost device instead of pretending to play | ✅ |

---

## 5. Tests passed — device inventory and virtual device detection (logic only)

These verify the *detection logic*, not any driver:

- Real hardware names ("Speakers (Realtek(R) Audio)", "Headphone USB Audio Device",
  "Focusrite USB ASIO", ...) are **not** misidentified as virtual cables.
- VB-CABLE names ("CABLE Input (VB-Audio Virtual Cable)", "Voicemeeter Input",
  "Virtual Audio Cable 1", "Hi-Fi Cable Input", "Loopback Adapter") **are** recognised.
- An inventory containing only a virtual cable is reported as a virtual device, and the
  user-facing message warns that it does not verify real hardware.
- An empty inventory produces a message naming what was checked and suggesting a way
  forward (install a device, or install VB-CABLE for routing tests without hardware).
- ASIO is reported only when a driver is *really installed*: an ASIO driver type with no
  devices is not counted as usable ASIO.

**What this run actually saw on this machine** (recorded by the test itself):

```
NO AUDIO HARDWARE on this machine - No audio output device was found.
Checked: ALSA (This driver reported no devices.).
Install or enable a sound device, or install a virtual cable such as VB-CABLE
to route audio without hardware.
Physical and virtual device tests were therefore NOT performed.
```

---

## 6. Design rules pinned by tests

Two behaviours are deliberate, and are covered by tests so a future change cannot
"fix" them by accident:

1. **The test tone bypasses the master chain.** It is a *device* test. A master fader at
   -inf, a muted project or an empty arrangement must not be able to make a working sound
   card look broken.
2. **Preview notes are consumed, not pre-cleared.** `previewMidi` used to be cleared at
   the top of every block, which silently discarded every piano-roll and MIDI-keyboard
   note. A test now pins the behaviour.

---

## 6b. Step A — measurement maths, cross-checked against an independent implementation

The tests before this point proved the engine produced *something* sane. They could not
prove the *numbers attached to it* were right, and a meter that lies is worse than no
meter. So the measurement code was checked against an outside reference rather than
against itself.

Implemented (`src/nux_engine/Measurements.cpp`): peak / RMS / true peak (4x oversampled),
ITU-R BS.1770-4 loudness and LRA, THD and THD+N, SNR, stereo width, DC offset, channel
correlation, crosstalk, and a Hann-windowed FFT with parabolic interpolation.

Loudness was validated against **ffmpeg's `ebur128` filter**, which is libebur128 — an
independent BS.1770 implementation, not our own code:

| Signal (1 kHz, 48 kHz) | libebur128 | NUX |
|---|---|---|
| 0 dBFS stereo | 0.0 LUFS | +0.0067 |
| 0 dBFS mono | -3.0 LUFS | -3.0 |
| -6 dBFS stereo | -6.0 LUFS | -6.0 |
| -20 dBFS stereo | -20.0 LUFS | -20.0 |
| -20 dBFS mono | -23.0 LUFS | -23.0 |

Five real bugs were found and fixed by this work, none of which the old tests could have
caught:

1. The `-0.691` offset was applied twice (it is already inside the block loudness formula).
2. K-weighting was approximated rather than implemented; error was 0.73 LU (-20.73 vs -20.00).
3. `getMagnitudeAt` snapped to the nearest FFT bin — about 1.4 dB of error on a pure tone.
4. Crosstalk was reported as an absolute level instead of a ratio.
5. The mono signal generator wrote past the end of the buffer (heap overflow).

The methods and their tolerances are written up in `docs/AUDIO_MEASUREMENT_METHODS.md`.
These measurements are implemented, not calibrated — see section 10.

---

## 6c. Step B — round trip, and a rule about honesty (10 groups)

Range tests prove a processor does something; a round trip proves a **signal survives the
journey**. `src/nux_engine/RoundTrip.cpp` plays a known tone and measures what comes back.

The important part is not the DSP, it is the reporting. Every result carries a
`Verification` level, and the levels are ordered so that a weak claim can never be
mistaken for a strong one:

```
notPerformed  <  skipped  <  simulated  <  virtualDevice  <  physicalDevice
```

Three rules are pinned by tests, because they are exactly the rules it is tempting to
break when you are impatient for a green tick:

- **A simulated round trip never reports itself as hardware.** Feeding a generated buffer
  straight back into a generated buffer is a software test. It is labelled `simulated`.
- **A skipped round trip is never judged a pass.** No device means the verdict is
  `skipped`, not "everything is fine".
- **A tone that comes back the wrong frequency, or at the wrong level beyond tolerance,
  fails** — the judge compares against the injected frequency, level and the level of the
  returned signal, with a tolerance object rather than a magic number.

Covered: all 11 signal types, frequency preservation, mono and stereo, gain accuracy,
level tolerance, and the verification ladder itself. The device round trip currently
returns `skipped` on this machine, which is the honest answer when no capture device is
driven — see step C.

---

## 7. WASAPI loopback capture — investigated, not supported

The prompt asked to investigate Windows WASAPI loopback capture and implement it if
supported. **It is not available in this build, and it was not implemented.**

Findings:

- JUCE does **not** expose loopback capture: `AUDCLNT_STREAMFLAGS_LOOPBACK` appears
  **0 times** in `external/JUCE/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp`.
  There is no loopback option on `AudioIODeviceType` or `AudioIODevice`.
- Implementing it would mean bypassing JUCE's device layer and writing a custom
  WASAPI *endpoint* capturer against the raw Windows APIs, including the IAudioClient /
  IAudioCaptureClient plumbing and format negotiation. That is a substantial,
  Windows-only, hardware-dependent component.

**Decision:** not implemented in this phase. The right way to get the same verification
today is a virtual cable: send NUX's output to VB-CABLE, record from VB-CABLE's input and
compare. That is listed in the next section as a manual check.

---

## 8. ASIO status

**Build configuration changed:** ASIO was **not** actually compiled in before this branch.
`JUCE_ASIO` defaults to `0` and was never set in `CMakeLists.txt`, so the README line
"ASIO where installed" was inaccurate. It is now set on Windows only:

```cmake
JUCE_WASAPI=1
$<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>
$<$<PLATFORM_ID:Windows>:JUCE_WASAPI_EXCLUSIVE=1>
$<$<PLATFORM_ID:Windows>:JUCE_DIRECTSOUND=1>
```

| Item | Status |
|---|---|
| ASIO compiled in on Windows | ✅ enabled in CMake (needs a Windows build to confirm) |
| ASIO driver present on this machine | ❌ none (Linux build machine) |
| ASIO initialisation tested | ❌ **NOT PERFORMED** |
| ASIO verified working | ❌ **NOT VERIFIED** |

Installing an ASIO driver is not the same as verifying ASIO. It is not marked verified
here and must not be until a real driver has been opened and audio has passed through it.

---

## 9. Tests NOT performed — hardware required

Everything in this section needs a machine with a sound card. The checklist is in
[`docs/hardware-checklist.md`](docs/hardware-checklist.md).

| Item | Why it was not performed |
|---|---|
| Real speaker output | No output device on the build machine |
| Real microphone recording | No input device on the build machine |
| Virtual audio device (VB-CABLE) | Not installed on the build machine |
| ASIO driver initialisation | No ASIO driver, non-Windows host |
| Device disconnection at the OS level | Requires a device to disconnect |
| Audio latency measurement | Requires a real driver and loopback timing |
| Sample-rate switching on a real driver | Requires a real driver |
| Multi-hour playback stability | Requires a real driver |

---

## 10. Known limitations

1. **No hardware has been tested.** Every "passed" line in this report is either DSP maths
   or file I/O. Nothing here proves that NUX makes a sound.
2. **Frequency measurement is zero-crossing based.** It is accurate to well under 1% for
   a clean sine, but it is not a spectral analysis; for a complex or noisy signal it would
   under-report.
3. **Buffer sizes are tested offline**, by pulling blocks by hand. The real device callback
   can deliver variable block sizes and XRUNs; that path is only exercised on hardware.
4. **Sample-rate "conversion" is not implemented.** The engine prepares at the device rate
   and expects the project to match it. There is no resampler, so importing a 48 kHz clip
   into a 44.1 kHz project is not sample-rate converted.
5. **The virtual-device detection is name-based.** An unusually named real device that
   contains the word "virtual" would be flagged, and a virtual cable with a completely
   custom name would not be. This is a heuristic for user messaging, not a routing check.
6. **ASIO is compiled in but unverified.**
7. **Loopback capture is not implemented** (section 7).

---

## 10b. Two Windows faults that had been hiding each other

This is recorded in full because the way it failed is more instructive than the fix.

For some time the Windows CI job reported a passing test step while running **no tests at
all**. `enable_testing()` was missing from the CMake files, so `ctest` found zero tests and
returned success. That silence then concealed a second, unrelated fault: once ctest was
made to actually run the tests, Windows segfaulted before the first test could even print
its name.

The crash affected exactly the groups that construct an `AudioEngine`:

```
NUX_engine_rendering   ***Exception: SegFault  0.03 sec
NUX_audio_device       ***Exception: SegFault  0.03 sec
NUX_offline_audio      ***Exception: SegFault  0.03 sec
NUXTests (full)        ***Exception: SegFault
```

The cause was not logic. It was object size:

```
sizeof(AudioEngine) = 434,576 bytes (Linux) / 447,584 bytes (Windows)  =  ~424-437 KB
default stack:       Windows 1 MB   |   Linux 8 MB
```

Every test declared `AudioEngine engine;` **on the stack**. On Windows that single object
took 43% of the stack and the first engine test overflowed it immediately; on Linux there
was 8 MB of room, so 104 groups passed locally for days while Windows never got past the
starting line.

Fixed by allocating all 22 test engines on the heap, plus a test that asserts
`sizeof(AudioEngine)` stays under half a Windows stack, so the next person to grow the
engine sees the number instead of rediscovering the crash.

Result: Windows Debug **15/15 ctest pass**, JUCE leak detector clean; Linux 114 groups,
15/15. The lesson generalises past this bug - *a test suite that reports success without
running anything will hide the next fault too*, which is why the CI step now throws unless
the output actually contains `All NUX tests passed`.

---

## 11. How to reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_BUILD_PARALLEL_LEVEL=1
cmake --build build --target NUXTests -j1
ctest --test-dir build --output-on-failure
```

`ctest` runs 15 entries: 14 per-group tests and the full suite. A single group can be run
directly with `NUXTests --only="round trip"`, and `--list` shows the groups.

The suite prints `All NUX tests passed.` on success. The device inventory line in
section 5 is printed by the `Probing the real machine` test and will differ per machine -
that is the point of it.

---

## 12. What to do next

The Virtual Audio Lab is agreed in four stages, in this order, because each one can only
be trusted if the stage before it is already trustworthy:

| Stage | Status |
|---|---|
| Offline tests (no device) | ✅ done |
| Step A - measurement maths | ✅ done, cross-checked against libebur128 |
| Step B - software round trip + verification levels | ✅ done |
| Step C - real speaker and microphone on a Windows PC | ⏳ needs a physical machine |
| Step D - ASIO driver | ⏳ last, and only with a real driver |

1. Run the suite on a Windows machine with a real sound card (step C). Compare the device
   inventory line against what Windows shows.
2. Work through `docs/hardware-checklist.md` with speakers and a microphone.
3. Install VB-CABLE and repeat the loopback comparison described in section 7.
4. Only after that, install an ASIO driver (ASIO4ALL or the interface's own) and test it.
5. Keep developing the engine, mixer, MIDI and VST3 features in the meantime - none of
   them need hardware to be built or tested offline.
