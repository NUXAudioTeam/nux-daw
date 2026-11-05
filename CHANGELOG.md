# Changelog

All notable changes to NUX DAW are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Unit test suite (project model, `.nux` serialization, DSP, end-to-end render).
- GitHub Actions CI for Windows (MSVC) and Linux (GCC).

### Fixed
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
