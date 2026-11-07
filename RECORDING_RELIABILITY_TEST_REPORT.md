# Recording reliability — test report

> **Branch:** `feature/recording-reliability` · **Base:** `feature/take-placement` (`32be195`)
> **Run:** 2026-11-07 · Linux Release + ThreadSanitizer (GCC 14)

Phase 2.2 asked for the whole path from pressing record to the take being safely in the
project to be audited, not extended. This report records what was found, what was changed,
and what is still unknown.

Everything below is **virtual**: the recorder is driven from a test thread standing in for
the audio callback. No audio interface, microphone or cable was involved at any point.

---

## 1. What the recording lifecycle actually is

| Stage | Thread | Code |
|---|---|---|
| Arm a track | message | `projectState->tracks[i].recordArmed` → `tracksRecordArmed` atomic (phase 5.1) |
| Start | message | `startInputRecording` → `InputRecorder::start` (creates the WAV writer) |
| Capture | audio | `audioDeviceIOCallbackWithContext` → `InputRecorder::pushInput` (ring buffer only) |
| Write | message (20 Hz timer) | `pumpInputRecorder` → `InputRecorder::drainToFile` |
| Stop | message | `stopInputRecording` → drain → finalise → read back |
| Place | message | `nux::placeTake` (phase 2.1) → undo edit → graph rebuild |

The split is right: the audio callback allocates nothing, locks nothing and never touches
the disk. The failures below are all in the handshake between those two halves, or in what
happens when something outside the process goes wrong.

---

## 2. Defects found, and the evidence

Each was reproduced with a test before it was fixed. The numbers below are real output from
running the new tests against the pre-fix implementation (one build, all reverted at once).

### 2.1 A full ring buffer overwrote audio silently

`pushInput` wrote `numSamples` samples regardless of how much room was left, wrapping over
unread audio, and `drainToFile` then computed the readable span from the index difference -
which had already been corrupted.

```
!!! Test 2 failed: an overrun must be reported, not hidden
!!! Test 4 failed: every pushed sample is either written or counted as dropped
    -- Expected value: 1024000, Actual value: 237568
```

**786,432 samples vanished and nothing said a word.** A stalled timer, a full disk or a
suspended laptop was enough to lose most of a take and still report success.

**Fixed:** the writer honours the free space, drops the newest samples when there is none,
and counts them in `droppedSamples`. The UI now says *"The take has gaps: N samples were
dropped while the disk fell behind"* instead of showing a clean take.

### 2.2 The last block of every take could be lost

`stop()` set the recording flag and drained once. A callback that had already passed the
flag check published its samples after the drain - into a file that was already closed.

```
!!! Test 3 failed: everything the callback captured is written or counted, not lost
    -- Expected value: 1313792, Actual value: 3072
```

**Fixed:** a `callbacksInFlight` counter. The audio thread increments it before touching the
buffer and decrements after; `stop()`, `start()` and `prepare()` wait for it to reach zero
(bounded, yielding, message thread only) before draining, resetting or resizing.

The same counter fixes a second, worse version of this bug: `prepare()` is called when the
audio device restarts, and it used to resize the ring buffer while the audio thread was
writing into it.

### 2.3 A device change mid-take rewrote the take's length

`getRecordedSeconds()` divided by the recorder's *current* sample rate, which `prepare()`
overwrites when the device reopens. At 44.1 kHz → 96 kHz:

```
!!! Test 2 failed: the take still knows its own sample rate
    -- Expected value within 0.001 of: 1.161, Actual value: 0.533333
```

A 10-second take reported itself as 5, and the clip was placed at half its real length -
silently, only when the interface reconnected during recording.

**Fixed:** the sample rate, channel count and track belong to the *take*, captured at
`start()`. `prepare()` during a take stores the new configuration and applies it to the next
take. The WAV header keeps the rate the audio was recorded at, because that is what the
samples are.

### 2.4 A failed write was reported as a saved take

`closeFile()` called `writer->flush()` and ignored the result; the WAV header is written by
the writer's destructor, which cannot report failure at all.

```
!!! Test 2 failed: a take with a failed write must not report success
!!! Test 3 failed: and must say why
```

**Fixed:** `stop()` returns a result, and `closeFile()` **reads the file back**: the header
must open, and `lengthInSamples` must equal the samples written. A take is only reported as
saved when that read-back matches. When it does not, `RecordedTake::saved == false`, the
file is left on disk (it may be repairable) and the UI shows the error instead of placing it.

### 2.5 The take landed on whichever track was armed when it *stopped*

`stopInputRecording()` read `getArmedTrack()` at the end. Arming another track - or
disarming - while recording moved the finished take elsewhere; disarming sent it to track
one through the invalid-index fallback.

**Fixed:** the track index is captured at `start()` and carried on the take
(`getStartTrackIndex()`).

### 2.6 A device that disappeared mid-take left the recorder "recording" forever

`audioDeviceStopped()` set its flags but never touched the recorder.

**Fixed:** the take is finalised and kept as an interrupted take; the timer collects it,
places it and says *"The input device was lost - the take up to that point was kept"*.

### 2.7 A crash left the audio on disk and unreachable

A WAV keeps its length in the header written at the end. A crash leaves the samples in place
with a header that says *zero samples*, so every reader - including NUX - opens silence.

The test states the old behaviour plainly:

```cpp
expectEquals (readBack (file), (juce::int64) 0,
              "a file whose header was never written reads as empty");
```

**Fixed:** `nux_core/RecordingRecovery.{h,cpp}` parses the real chunk structure, works out
the sample count from the file size and rewrites the two length fields; a startup scan in
`MainComponent` repairs `take-*.wav` files and reports how many it recovered. It does not
grow a file, does not touch a file that is already consistent, and refuses to invent a
recording out of a header with no audio after it.

---

## 3. Test coverage — 18 new groups

**`NUX recording reliability`** (12)

| Group | What it pins |
|---|---|
| A full ring buffer drops samples and counts them instead of overwriting | written + dropped == pushed; nothing vanishes silently |
| The last block of a take is not lost when recording stops | a live callback thread during `stop()`; every sample written or counted |
| A device change mid-take does not rewrite the take's length | 44.1 kHz take, `prepare(96000)` midway, length and header rate unchanged |
| Preparing a new device while a take runs keeps the buffered audio | undrained audio survives the reconfiguration; the next take uses the new rate |
| A take remembers the track it started on | track index and start step survive to `stop()` |
| A take that could not be finalised is not reported as saved | `stop()` false, an error message, unread audio still in the ring |
| A finished take reads back exactly the samples that were written | header sample count == samples written |
| Empty takes are refused and leave no file behind | zero-length take leaves no file after abort |
| Rapid start and stop cycles leave the recorder usable | 12 cycles; counters reset, each file holds its own block |
| A minute of audio is written without drift or gaps | 60 s at an odd block size (441); exact sample count, zero drops |
| Pushing while stopping neither loses nor duplicates audio | 3 threaded attempts against the file |
| Aborting leaves nothing behind and stops the take | no file, counters zeroed |

**`NUX recording recovery`** (6)

| Group | What it pins |
|---|---|
| A finished file is reported as consistent and is left alone | no repair, no byte changed |
| A take that lost its header is repaired to its real length | reads as 0 samples before, 5000 after, same file size |
| A truncated file is repaired to the samples that survived | header corrected downwards, no data invented |
| A header with no audio after it is not invented into a recording | refused, with a reason |
| Only unfinalised takes are listed as recoverable | one broken take found, a finished take and a non-take ignored |
| A file that is not a WAV is refused | no parse, no repair |

---

## 4. The race ThreadSanitizer found — in my own fix

The first TSan run of the new stress test reported a race:

```
WARNING: ThreadSanitizer: data race
  Read of size 1 by main thread:
    #0 juce::AudioBuffer<float>::copyFrom(...)
    #1 nux::InputRecorder::drainToFile() InputRecorder.cpp:310
  Previous write of size 1 by thread T2:
    #0 juce::AudioBuffer<float>::getWritePointer(int)
    #1 nux::InputRecorder::pushInput(...) InputRecorder.cpp:264
```

The samples were not racing - they are handed over through the indices. The one-byte field
that raced is `AudioBuffer`'s internal "is this buffer cleared" flag, which
`getWritePointer()` writes and `copyFrom()` reads. It was harmless in practice and fatal in
CI, because the job greps for that exact warning.

**Fixed** by keeping raw channel pointers (`cacheRingPointers()`) so the audio thread never
goes through an `AudioBuffer` accessor, and by copying out of the ring with `memcpy`. This
is also better real-time practice: no JUCE accessor, no assertions, no flag writes on the
callback.

That is the point worth keeping: **this race existed in the shipping engine before this
phase**, on every recording. It took a threaded test plus TSan to see it.

---

## 5. Verification — actual results

| Check | Result |
|---|---|
| Focused: `--only="NUX recording"` | 18 groups, all passed |
| Full suite, Linux Release | **165 groups, `All NUX tests passed.`** |
| `ctest` | **19/19 passed** |
| Full suite under ThreadSanitizer | **165 groups, 18 suites, 0 races, exit 0** |
| Pre-fix demonstration (section 2) | 5 of the new groups failed, numbers recorded above |
| **CI run `42386678210`** · Linux Release | ✅ `100% tests passed out of 19` |
| **CI** · Linux ThreadSanitizer | ✅ `Ran 18 group(s)`, `All NUX tests passed.`, **0 races** |
| **CI** · Windows Debug + leak detector | ✅ success |
| **CI** · Windows Release | ✅ success |

The only `WARNING: ThreadSanitizer` string in the TSan log is the workflow echoing its own
`grep`; no sanitizer report was produced.

A clean TSan run means no race was **observed** on the paths these tests exercise. It is
not proof that the recorder is race-free, and it says nothing about a real driver's callback
behaviour.

---

## 6. Not covered — stated, not hidden

| Gap | Why |
|---|---|
| A physical input device: real capture, unplug/replug, driver reset | no hardware in this environment. Everything above is simulated |
| Buffer under/overrun from a real device | the ring overrun is tested synthetically; a real driver's timing is not |
| Disk-full and permission failures at the OS level | the write-failure path is exercised through a test hook (`setWriteFailureForTests`), not a real full disk |
| Recording several tracks at once | **not supported**: there is one `InputRecorder`. This is a limitation, not a bug, and no test pretends otherwise |
| Pause/resume of a recording | **not supported**: the transport has no record-pause. Stop is the only way to end a take |
| Input latency compensation | needs a measured round trip |
| Actual crash recovery during a live session | the recovery module is tested against files that are byte-for-byte what a crash leaves; the process itself was not killed mid-take |
