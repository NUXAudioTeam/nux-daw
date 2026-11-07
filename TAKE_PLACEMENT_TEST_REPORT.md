# Take placement — test report

> **Branch:** `feature/take-placement` · **Base:** `feature/tsan-ci` (`dd4035f`)
> **Run:** 2026-11-07 · Linux Release, GCC 14 · ThreadSanitizer build (GCC 14, `-fsanitize=thread -g -O1`)

This is the report for phase 2.1: taking the arithmetic behind "the take you just recorded
is now a clip" out of the UI and putting it where it can be tested.

---

## 1. What moved, and what did not

| Before | After |
|---|---|
| `MainComponent::placeRecordedTake()` decided the clip length, its position, whether the pattern grows, and whether the track becomes an audio track | `nux::placeTake()` in `src/nux_core/TakePlacement.{h,cpp}` decides all of it |
| Testable only by launching the application with `--record-self-test` | 17 test groups, plus a ctest entry |
| Depended on `ProjectDocument`, the audio engine and the window title | Depends on `ProjectState` and constants. Nothing else |

The UI keeps the jobs that genuinely belong to the UI: where the project folder is, whether
a take should be stored relative or absolute, the undo edit, rebuilding the audio graph, and
the status line.

**Existing behaviour preserved.** The one intentional change is a bug fix, described in
section 4.

---

## 2. Test coverage — 17 groups

| Group | What it pins |
|---|---|
| A take's length in steps follows the tempo | 1 s = 8 steps at 120 BPM, 4 at 60, 16 at 240; doubling a take roughly doubles its steps at 60/90/120/140/174 BPM |
| A take is always at least one step long | 0.001 s, 0 s, −5 s and NaN all produce one step, never a zero-length or NaN clip |
| The pattern grows in bars to hold the clip | 16 → 32 when a clip ends at step 17 or 20; no growth when it fits exactly; no shrinking |
| Pattern growth stops at the maximum | caps at `maxNumSteps`; a zero `stepsPerBeat` cannot loop forever |
| Placing a take creates a clip at the right place | name, stored path, start step, length, `sourceStartSample = 0`, exactly one clip |
| A fractional start step is rounded, not truncated | 4.6 → 5, 4.4 → 4 |
| The track becomes an audio track | type changes, reported once, not re-reported on the second take |
| Repeated takes accumulate instead of replacing | four takes, four clips, four reported indices, correct start steps |
| Recording a long take grows the pattern to contain it | 4 s at 120 BPM = 32 steps, pattern grows from 16, before/after reported |
| A short take leaves the pattern alone | 0.5 s take does not change the pattern length |
| A take on a later track lands there and nowhere else | other tracks untouched, not converted to audio |
| An invalid track index falls back instead of losing the take | 99 and −3 both land on track 0 rather than discarding the recording |
| An empty project is a real failure, and says so | `placed == false`, a non-empty error, no clip index |
| Placing a take is undoable and redoable | undo removes the clip, restores the pattern length and the track type; redo puts them back |
| Two takes are two undo steps, not one | **this one found a bug — see section 4** |
| Placement never disturbs the notes or inserts it did not touch | insert type and values survive, pattern clips unchanged |

**Time signatures:** the model has no per-project time signature yet (4/4 is fixed in
`constants::stepsPerBeat`), so "different time signatures" is covered as far as the code
allows: the length maths takes `stepsPerBeat` as a parameter and is tested at other values
through the pattern-growth tests. A real test needs the 3.6 work (time signature support),
which does not exist. Saying otherwise would be inventing coverage.

---

## 3. How this was verified

| Check | Result |
|---|---|
| Focused group: `NUXTests --only="NUX take placement"` | **17 groups, all passed** |
| Full suite (Linux Release) | **147 groups, `All NUX tests passed.`** |
| `ctest` | **17/17 passed** (16 previous entries + `NUX_take_placement`) |
| Full suite under ThreadSanitizer | **147 groups, 15 suites, 0 races, exit 0** |
| Take placement under TSan specifically | 0 races |
| Windows Debug / Release CI | see section 5 |

The extracted code is pure: it takes a `ProjectState&` and a request struct and mutates the
state. It runs on the message thread only, holds no locks, starts no threads, and is not
called from an audio callback — so it cannot introduce a race, and TSan agrees. The
`AtomicInsertState` publish/acquire work from phase 5.1 is untouched.

---

## 4. The bug this found

The UI called:

```cpp
document.beginEdit ("record audio", "Record an audio clip");
```

`beginEdit` takes a **key**, and a repeated key means "this is a continuation of the same
edit" — that is how a knob drag becomes one undo step instead of four hundred. Passing the
same key for two separate recordings therefore merged them: **one undo deleted both takes**,
and one redo brought both back.

The test failed the first time it ran:

```
!!! Test 5 failed: one take left after undoing the second -- Expected value: 1, Actual value: 0
!!! Test 6 failed: second undo
```

Fixed by passing an empty key, which means "discrete step", in
`MainComponent::placeRecordedTake`. The test now records the intent explicitly: a recording
is a discrete edit, never a continuation.

A second, smaller finding from the same run: my first test asserted that doubling a take
exactly doubles its step count. It does not — `ceil()` does not distribute over
multiplication (at 140 BPM one second rounds 9.33 up to 10, two seconds rounds 18.67 up to
19). The assertion was wrong, not the code; it now checks "within one step" and that a longer
take is never shorter.

---

## 5. CI

Windows Debug, Windows Release and Linux Release run the same suite as before, plus the new
`NUX_take_placement` ctest entry. The Linux ThreadSanitizer job covers the new groups too.
Exact results are recorded in the pull request once the runs finish — not predicted here.

---

## 6. What is still not tested

| Gap | Why |
|---|---|
| Real capture into a real file | needs a device; `RecordingTests` covers the recorder with simulated input |
| Latency compensation | needs a device to measure the round trip |
| Per-project time signature | the feature does not exist yet (TODO 3.6) |
| The status text and window title updates | UI strings, covered by the headless self-test, not by unit tests |
