# NUX DAW - manual hardware checklist

The automated tests cover everything that can be checked without a sound card. These
items can only be verified on a real Windows machine with real hardware, so they are
listed here rather than claimed as tested.

Tick each item and note the device used. **Do not mark an item done because the code
looks right** - plug the hardware in and listen.

## Audio output

- [ ] The AUDIO settings window lists the expected WASAPI output devices.
- [ ] Selecting an output device produces a status line with the device name, sample
      rate and buffer size.
- [ ] **TEST TONE** produces a steady 440 Hz tone on both speakers.
- [ ] Changing LEVEL changes the loudness smoothly, with no click at either end.
- [ ] **STOP TONE** silences it without a click.
- [ ] The OUTPUT meter moves while the tone plays and returns to -inf when it stops.
- [ ] Changing the sample rate to 44100 / 48000 / 96000 works and the tone still sounds
      like 440 Hz.
- [ ] Changing the buffer size (128 / 256 / 512 / 1024) does not break playback.

## Audio input

- [ ] Plugging in a microphone or interface makes it selectable as an INPUT.
- [ ] The INPUT meter moves when you speak or play into it.
- [ ] The INPUT meter reads -inf and shows "no input signal" when nothing is connected.
- [ ] Arming a track (the **R** button) and pressing **record** creates a take.
- [ ] The finished take appears on the armed track as an audio clip with a waveform.
- [ ] The recorded audio actually contains what you played - not silence, and not a copy
      of the backing track.

## Device handling

- [ ] Unplugging the interface while NUX is running shows "Device disconnected" rather
      than silently going quiet.
- [ ] Plugging it back in reopens the device (the status line returns to normal).
- [ ] Starting NUX with no audio device shows a clear message and the app stays usable.
- [ ] ASIO drivers appear in the device type list when an ASIO driver is installed
      (ASIO has **not** been tested by the automated suite).

## Performance

- [ ] A 32-track project with a few plugins plays without dropouts at 256 samples.
- [ ] The CPU use stays reasonable while the test tone and playback run together.

## Reporting

When something fails, capture: the device make and model, the driver type (WASAPI /
ASIO), the sample rate and buffer size, and what the AUDIO panel status line says.
