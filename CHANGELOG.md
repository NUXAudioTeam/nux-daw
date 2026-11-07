## [Unreleased] - device selection and the device round trip

### Added

- **Device selection**: `DeviceSelection.{h,cpp}` lists the platform's devices, flags
  virtual cables, pairs the two halves of one, and refuses a pairing that cannot work
  (no input, missing device, half on another driver) with a reason that names the device.
- **`AudioEngine::applyDeviceChoice()`** opens exactly the chosen output/input pair instead
  of the system default, and returns the driver's own error text when it fails.
- **Virtual Audio Lab UI** in AUDIO SETTINGS: output/input choosers, `[virtual]` labels,
  OPEN PAIR and ROUND TRIP, with the verification level printed next to the measurement.
- **`VirtualLoopback.{h,cpp}`**: plays a known tone into a chosen output, captures the
  chosen input, trims the leading silence and measures what came back. A missing device
  produces `skipped` with a reason; it can never produce a pass.
- 13 test groups pinning the selection and honesty rules, registered with ctest.

### Fixed

- The suite reported one fewer group than it declared. `PluginHostTests` declares a group
  that only runs when the VST3 test plugins are missing, so the count now differs by one by
  design; documented in the test itself.

## [Unreleased] - Virtual Audio Lab, step A (measurement maths)

### Added
- **Measurement engine** (`src/nux_engine/Measurements.{h,cpp}`): levels and AES17 clip
  detection, Hann-FFT spectrum, AES17 THD / THD+N against a known fundamental, SNR,
  stereo correlation / DC / crosstalk, 4x-oversampled true peak, BS.1770-4 gated loudness
  with LRA, gain accuracy, sample-rate accuracy, and parabolic-interpolated frequency.
- **Reference signals** (`src/nux_engine/TestSignals.{h,cpp}`): sine, square, triangle,
  sawtooth, white and pink noise, DC, two tone, exponential sweep and impulse, each with
  an analytically known RMS and each deterministic for a given seed.
- **`tests/MeasurementTests.cpp`** (20 groups) of known-answer tests, so the maths is
  checked against formulas rather than against whatever the engine happened to produce.
- **`docs/AUDIO_MEASUREMENT_METHODS.md`** documenting every method, its standard, its
  known-answer checks, and exactly what has *not* been verified.

### Fixed
- **Loudness read every measurement 0.691 LU low.** Each 400 ms block already carried the
  BS.1770 `-0.691` offset; the averaging converted blocks back to power and then added it
  again. The four non-full-scale reference cases still passed a loose 0.2 LU tolerance,
  so only the full-scale case exposed it.
- **K-weighting was an approximation, not the standard.** It used a generic
  `makeHighShelf(1500 Hz, Q 0.7071, +4 dB)` and read a −20 dBFS stereo tone as
  −20.73 LUFS. Now designed from the published pre-filter parameters
  (`f0 = 1681.97 Hz`, `G = 3.9998 dB`, `Q = 0.70717`) and reads −20.00, which is what
  ffmpeg / libebur128 reports for the same signal.
- **`SpectrumResult::getMagnitudeAt()` did not interpolate** despite its header claiming
  it did; it rounded to the nearest bin, which misread levels by up to 1.4 dB depending
  on where a tone fell. Now uses parabolic interpolation.
- **`measureCrosstalkDb()` returned an absolute level instead of a ratio**, reporting a
  correct −60 dB leak as −69 dB. Now measured relative to the driven channel.
- **Mono reference signals corrupted the heap.** The generator wrote channel 1
  unconditionally, so any mono spec wrote past the end of the buffer. Silent in Release,
  and it took the whole test run down with a segfault as soon as a mono case was added.

### Changed
- `NUXTests` takes `--list` and `--only=<group>` so a crash can be bisected to one group,
  and writes unbuffered output so a crash leaves the running test's name in the CI log.
  Windows CI previously reported nothing but "SegFault".

## [Unreleased] - CI coverage

### Added
- **Windows Debug CI job** with a leak-detector gate. Release-only CI could never have
  caught the `PluginRetirementQueue` leak, because JUCE only reports leaked objects in
  Debug builds.
- **ASIO compile check in Windows CI.** `JUCE_ASIO` defaults to `0`; a Linux build cannot
  prove the flag reached the compiler, but the Windows runner can. The step fails loudly
  if ASIO is not compiled in.
- `--device-report` command line flag: prints driver types, devices, which are virtual
  cables, whether ASIO is compiled in and usable, and a plain-English summary. The most
  useful thing to ask for in a bug report.
- CI now publishes test output and `AUDIO_TEST_REPORT.md` as artifacts, and prints the
  device inventory on both Windows and Linux runners.

### Fixed
- `recover.sh` now installs and starts Xvfb. A missing display makes the GUI self-tests
  segfault, which looks exactly like a code regression and cost real debugging time.

## [Unreleased] - audio testing

### Added
- **Offline audio test suite** (`tests/OfflineAudioTests.cpp`, 13 groups) that needs no
  sound card: 440 Hz accuracy at 44.1/48/88.2/96 kHz, level in dBFS, clipping at every
  level, click-free ramps, identical stereo channels, every block size from 32 to 2048,
  sample rates up to 192 kHz, bit-identical determinism, mute/solo/master fader
  behaviour, and a full WAV write/read round trip.
- `tests/SignalAnalysis.h` - frequency, peak, RMS, clipping, step-discontinuity and
  channel-difference measurement used by the tests.
- **Device inventory** (`src/nux_engine/AudioDeviceProbe.{h,cpp}`): lists driver types and
  devices, recognises virtual cables (VB-CABLE, Voicemeeter, loopback adapters) by name,
  reports ASIO only when a driver is really installed, and always produces a message
  explaining what was checked.
- `tests/DeviceProbeTests.cpp` (6 groups) covering the detection logic and the real
  machine's inventory.
- **`AUDIO_TEST_REPORT.md`** separating passed, simulated, virtual-device, physical and
  ASIO tests, with the limitations spelled out.
- `docs/hardware-checklist.md` - the manual checks that still need real hardware.

### Fixed
- **ASIO was never actually compiled in.** `JUCE_ASIO` defaults to `0` and was absent from
  `CMakeLists.txt`, so the README's "ASIO where installed" was wrong. WASAPI, WASAPI
  exclusive, DirectSound and (on Windows) ASIO are now set explicitly.
- `probeAudioDevices()` reported 2 output channels for a driver type with **zero** devices,
  because `getDefaultDeviceIndex()` returns an index even for an empty list. Channel
  counts now come from the device list.
- A device manager that had never been initialised reported no driver types at all,
  which read as "no drivers exist". The probe now registers the platform types first.
- The test tone's independence from mutes and the master fader is now deliberate and
  pinned by a test, instead of being an accident of where it is summed in.

## [Unreleased]

### Added
- **Test tone.** A 440 Hz sine that proves audio reaches the selected output device.
  It is generated on the audio thread with a 5 ms per-sample ramp (no click), added
  after the master chain so it is audible even with every track muted, capped well
  below 0 dBFS, and stopped by panic.
- **Input and output level meters** in the audio settings panel, fed by atomics the
  engine publishes each block. An input that never moves says "no input signal"
  instead of looking like a working meter.
- **Device-loss recovery.** `audioDeviceStopped()` / `audioDeviceError()` now mark the
  engine honestly, the status line says "Device disconnected - reopening...", and
  `tryReopenDevice()` reopens and re-prepares the graph from the panel's timer.
- `--audio-settings [--test-tone]` headless hook, plus the panel screenshot.
- `tests/AudioDeviceTests.cpp` (6 groups): tone frequency, no clipping, click-free
  ramp, tone in the mix, meter behaviour, device-loss reporting.

### Fixed
- The audio settings panel still asked for **0 input channels**, so the selector could
  never offer an input to record from.
- `setTestToneEnabled()` clamped the level *up* to 0.05 and then decided it was already
  audible, so the tone came out at -35 dB instead of the intended level.

# Changelog

All notable changes to NUX DAW are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- **Edit effect parameters.** Double-click an insert in the mixer to open a panel
  with the slot's real parameters (EQ low/mid/high, compressor threshold/ratio/
  attack, delay time/feedback/mix, reverb size/damping/mix), with units, correct
  ranges, logarithmic frequency and a reset-to-defaults button. The descriptors
  live in `nux_core/InsertParameters` and are shared by the editor, the insert menu
  and the tests, so nothing can drift out of step with the DSP.
- **Undo / redo.** `Ctrl+Z` and `Ctrl+Y` (or `Ctrl+Shift+Z`) undo and redo every
  edit: steps, notes, clips, faders, pans, mutes, inserts, plugin loads, tempo,
  pattern length, track add/duplicate/delete and renames. Repeated edits of the same
  thing (dragging a knob) coalesce into one step. Undo restores the whole project,
  then rebuilds the audio graph, the clips and the plugin instances to match.
- Headless self-test for undo/redo (`--undo-self-test`) that drives the real
  commands and prints what each step did.
- **VST3 plugins now run in the audio path.** An insert slot can hold a hosted
  plugin instead of a built-in effect, and a track can be played by a hosted
  instrument instead of the sampler.
  - `PluginSlot` owns a live instance, prepares it for the engine's sample rate
    and block size, and processes it inside the channel strip.
  - Instruments make the strip generate its own input, so the dry track signal is
    never mixed in underneath them.
  - Plugin state (the full chunk) is captured into the project on save and
    restored on load, so a project reopens with the same plugin settings.
  - Mixer insert menu: *Plugin...* opens a searchable chooser, *Open plugin
    window* shows the plugin's own editor.
  - A plugin that cannot be found reports a message and leaves the rest of the
    project playing rather than breaking the session.
- `PluginManager::scanNow()` and explicit extra search folders, so a plugin can be
  scanned synchronously (used by the chooser's Rescan button and by the tests).
- Two real VST3 test plugins (`NUX Test Gain`, `NUX Test Tone`) built from JUCE and
  used by the test suite to verify hosting against actual plugin binaries.
- **Audio clip playback.** Audio clips are decoded into cached stereo sample
  buffers and streamed in sync with the transport, including looped retriggering.
- **Audio import**: drag and drop a WAV/AIFF/FLAC/Ogg/MP3 file onto a track lane,
  or use *Import audio file...* from the track menu. Files are copied into the
  project's `media/` folder so projects stay portable.
- Clip lane drawing with a waveform preview, plus a clip context menu
  (duplicate, move to bar 1, remove).
- Headless smoke-test mode: `NUX DAW --demo-clip <file> --screenshot <png>`.
- Tests for the clip store, clip player (position, looping, stereo, failures) and
  an engine test that renders an imported clip into the mixdown.
- Unit test suite (project model, `.nux` serialization, DSP, end-to-end render).
- GitHub Actions CI for Windows (MSVC) and Linux (GCC).

### Fixed
- Plugin instances are never destroyed while an audio block could still be using
  them: they are swapped out with the device stopped and then freed through a
  short grace-period retirement queue.
- WAV export no longer applies live looping, so the render tail is silent instead
  of repeating the pattern.
- Loop retriggering of audio clips: a clip that starts at step 0 now plays again
  on every pass of the loop.
- `juce::File` objects are never built from a relative path any more (this was an
  assertion on debug builds), and quoted command-line paths are unquoted.
- Keyboard focus was grabbed before the main window was on screen, which tripped
  a JUCE assertion on start-up.
- The sequencer grid painted over the track header column, hiding track names.
- The transport bar reserved no space for the output scope and master meter, so
  both collapsed to nothing on window widths below ~1300 px.
- The wordmark overlapped the transport buttons at its previous width.
- The Master panel laid its meter and info readout outside the card.
- The piano roll velocity knob overlapped its own readout in a 38 px toolbar.
- Replaced the deprecated modal `AlertWindow::showYesNoCancelBox` with the
  asynchronous `MessageBoxOptions` API, including correct quit-after-save
  handling.
- Float comparisons in the data model now use `juce::exactlyEqual` to satisfy
  `-Wfloat-equal`.

## [0.3.0] - 2026-11-05

First working foundation. **Not a finished DAW** - see `TODO-LIST.md` for the
honest list of what is still missing.

### Added
- **Edit effect parameters.** Double-click an insert in the mixer to open a panel
  with the slot's real parameters (EQ low/mid/high, compressor threshold/ratio/
  attack, delay time/feedback/mix, reverb size/damping/mix), with units, correct
  ranges, logarithmic frequency and a reset-to-defaults button. The descriptors
  live in `nux_core/InsertParameters` and are shared by the editor, the insert menu
  and the tests, so nothing can drift out of step with the DSP.
- **Undo / redo.** `Ctrl+Z` and `Ctrl+Y` (or `Ctrl+Shift+Z`) undo and redo every
  edit: steps, notes, clips, faders, pans, mutes, inserts, plugin loads, tempo,
  pattern length, track add/duplicate/delete and renames. Repeated edits of the same
  thing (dragging a knob) coalesce into one step. Undo restores the whole project,
  then rebuilds the audio graph, the clips and the plugin instances to match.
- Headless self-test for undo/redo (`--undo-self-test`) that drives the real
  commands and prints what each step did.
- **VST3 plugins now run in the audio path.** An insert slot can hold a hosted
  plugin instead of a built-in effect, and a track can be played by a hosted
  instrument instead of the sampler.
  - `PluginSlot` owns a live instance, prepares it for the engine's sample rate
    and block size, and processes it inside the channel strip.
  - Instruments make the strip generate its own input, so the dry track signal is
    never mixed in underneath them.
  - Plugin state (the full chunk) is captured into the project on save and
    restored on load, so a project reopens with the same plugin settings.
  - Mixer insert menu: *Plugin...* opens a searchable chooser, *Open plugin
    window* shows the plugin's own editor.
  - A plugin that cannot be found reports a message and leaves the rest of the
    project playing rather than breaking the session.
- `PluginManager::scanNow()` and explicit extra search folders, so a plugin can be
  scanned synchronously (used by the chooser's Rescan button and by the tests).
- Two real VST3 test plugins (`NUX Test Gain`, `NUX Test Tone`) built from JUCE and
  used by the test suite to verify hosting against actual plugin binaries.
- JUCE 9.0.5 (pinned submodule) + CMake build targeting Visual Studio 2022.
- Audio engine: device management (WASAPI / DirectSound / ASIO where installed),
  stereo output, MIDI input, block-based rendering with per-track MIDI.
- 16-voice built-in polyphonic synth with five presets (Analog Kit, Sub Bass,
  Pluck, Warm Pad, Electric Keys) and a drum map.
- Channel strip per track: IIR low-shelf / peak / high-shelf EQ, feed-forward
  compressor, feedback delay, FDN reverb, smoothed fader, constant-power pan,
  mute, peak metering.
- Mixer: up to 32 tracks, 8 buses with tempo-synced delay and reverb sends,
  and a master bus with a soft-knee limiter at -0.3 dBFS.
- Step sequencer (16th-note grid, 4-32 steps) with transport, tempo, loop and
  play/stop/record controls.
- Workspaces: Main (track + step grid), Mixer, Piano Roll, Plugins, Master.
- VST3 plugin scanning with a cached plugin list and plugin editor hosting.
- `.nux` project format (JSON, versioned and forward-checked) with save/load.
- Offline 24-bit WAV export of the pattern with a tail fade.
- Dark studio theme with teal accents and a custom JUCE LookAndFeel.

### Known limitations
- Audio clips are stored in the project but **not yet played back**.
- VST3 plugins are scanned and their editors open, but plugin audio is **not yet
  inserted into the signal chain**.
- No undo/redo, no automation, no waveform display.
- Windows/MSVC build is verified by CI only at this stage; the reference build
  was made with GCC 14 on Linux.
