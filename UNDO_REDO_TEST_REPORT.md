# Phase 2.3 — Undo/Redo Reliability — Test Report

Scope: **D1–D4 only**, exactly as approved in `PHASE_2_3_PROPOSAL.md`. Nothing outside that
scope was added.

Branch `feature/undo-reliability`, PR **#10**, commit **`a25adca`**.

Every number in this report was produced by a command that actually ran. Where something was
**not** verified it is listed under [What is not verified](#what-is-not-verified).

---

## 1. What was fixed, and how it is proven

| ID | Defect | Fix | Proof |
|---|---|---|---|
| **D1** | `beginEdit()` pushed a snapshot identical to the top of the stack, so one change could leave a dead step; the first Ctrl+Z then looked like a no-op | `UndoHistory::beginEdit()` compares the new snapshot with `undoStack.back().json` and skips the push (coalescing timer still updated) | `D1: an edit that changes nothing does not push an undo step`, `D1: a genuine second edit still gets its own step` |
| **D2** | A redo reported itself as an undo, so "was the last change an undo?" was wrong after a redo | `ProjectDocument::ChangeOrigin { edit, undo, redo }`; `getLastChangeOrigin()`; `wasLastChangeAnUndo()` is `origin != edit`; `markDirty()` resets to `edit` | `D2: undo, redo and an ordinary edit each say what they are` |
| **D3** | The `*` marker stayed on forever after undoing back to the saved state | The document keeps the JSON it was last saved/loaded from (`savedJson`, set in the constructor, `createNew`, `loadFromFile`, `saveToFile`); `restoreFromJson()` sets `hasUnsavedChanges(differsFromSavedState())` | `D3:` × 3 groups — see the criterion table in §3 |
| **D4** | Undo round trip was only ever proven for a knob value | Full project round trip through the history: every field of a non-trivial state, and undo of a recorded take (clip must go, recorded file must stay) | `D4: every field of the project survives a snapshot round trip`, `D4: undo restores a field that a knob-only test would miss`, `Undo of a recorded take removes the clip and keeps the file` |

---

## 2. TDD — the tests were run against the pre-fix code first

The three fixes were temporarily reverted **in the source only** (tests untouched), the project
rebuilt, and the suite run. It failed, exactly where it should:

```
NUX undo reliability / D1: an edit that changes nothing does not push an undo step
!!! Test 1 failed: three calls for one change are one step, not three -- Expected value: 1, Actual value: 3
FAILED!!  1 test failed, out of a total of 3

NUX undo reliability / D2: undo, redo and an ordinary edit each say what they are
!!! Test 7 failed: the next ordinary edit clears it
FAILED!!  1 test failed, out of a total of 7

NUX undo reliability / D3: undoing back to the saved state clears the modified marker
!!! Test 5 failed: undo back to the saved state must clear the marker
!!! Test 9 failed: and undo clears it again
FAILED!!  2 tests failed, out of a total of 9

NUX undo reliability / D3: a project that has never been saved stays dirty after an undo
!!! Test 3 failed: but undoing the only edit returns to the untouched default, which is clean
FAILED!!  1 test failed, out of a total of 3

NUX undo reliability / D3: the marker is right after several edits and a partial undo
!!! Test 10 failed: and only when the saved state is reached does the marker go
FAILED!!  1 test failed, out of a total of 10
```

2 of 2 undo groups reported `FAILED!!`. The fixes were then restored, rebuilt, and re-run:

```
$ ./NUXTests --only="NUX undo"
Random seed: 0x482f01
Ran 2 group(s).
All NUX tests passed.
```

Honest note on the first attempt: my first D1 test **passed against the broken code**, because
the scenario I wrote was not the defect (I had changed the state between the two `beginEdit()`
calls). The test was rewritten to the real scenario — repeated `beginEdit()` with no change in
between — and that version fails pre-fix (`Expected 1, Actual 3`) and passes post-fix. The D3
revert also silently missed on the first attempt (the anchor text had drifted), which made D3
look green; it was re-applied properly and re-run before the results above were accepted.

---

## 3. D3 acceptance criteria, one by one

The five criteria the decision was approved on, and where each is asserted:

| # | Criterion | Test group | Result |
|---|---|---|---|
| 1 | A change from the saved state shows `*` | `D3: undoing back to the saved state clears the modified marker` | pass |
| 2 | Undo back to the saved state removes `*` | same group (`Test 5`) | pass |
| 3 | Redo that re-creates the change shows `*` again | same group (`Test 9`) | pass |
| 4 | After Save `*` is removed | same group + `D3: the marker is right after several edits and a partial undo` | pass |
| 5 | The saved state is identified correctly **even when other entries remain in the history** | `D3: the marker is right after several edits and a partial undo` (two edits, save, edit, partial undo → still dirty; undo to the saved point → clean) | pass |

`D3: a project that has never been saved stays dirty after an undo` covers the boundary where
there is no saved JSON at all: `differsFromSavedState()` returns `true` because `savedJson` is
empty, so a never-saved project can never be reported clean.

---

## 4. Regression — the rest of the suite

Local, Linux Release (`g++ 14.2.0`, JUCE 9.0.5, CMake 4.4.4), whole suite in one process:

```
Ran 19 group(s).
All NUX tests passed.
```

`ctest`, undo entries only:

```
1/2 Test  #8: NUX_undo_history ........ Passed
2/2 Test  #9: NUX_undo_reliability .... Passed
100% tests passed out of 2
```

One existing expectation **had** to change, and this is deliberate rather than a weakened test:
`tests/UndoTests.cpp` ("Undo marks the project as unsaved") asserted that undoing back to the
saved state leaves the project dirty — the behaviour D3 was approved to change. It now asserts
both halves: a partial undo stays dirty, and undoing all the way back to the saved state is clean.

---

## 5. CI — actual results

Both workflows triggered by `a25adca` completed successfully.

| Workflow | Run | Result |
|---|---|---|
| Build and test (push to branch) | `42474152655` | **4/4 jobs success** |
| Build and test (pull request) | `42474150422` | **4/4 jobs success** |
| Formatting and housekeeping | `42474152589` | success |

Per job, run `42474152655`:

| Job | Result | Duration |
|---|---|---|
| Linux (GCC, Release) | success | 11:27 |
| Linux (GCC, ThreadSanitizer) | success | 5:24 |
| Windows (MSVC, Debug + leak detector) | success | 8:56 |
| Windows (MSVC, Release) | success | 14:42 |

The report commit on top (`fc6e955`) was verified the same way: run `42475447302` (push) and
`42475452528` (pull request), **4/4 jobs success each**.

From the Linux Release job log of run `42474152655`:

```
 8/20 Test  #8: NUX_undo_history .................   Passed    0.02 sec
 9/20 Test  #9: NUX_undo_reliability .............   Passed    0.04 sec
100% tests passed out of 20
Total Test time (real) =   2.17 sec
```

---

## 6. Test inventory

`tests/UndoReliabilityTests.cpp` — suite **`NUX undo reliability`**, 13 groups, 113 assertions,
registered in `tests/CMakeLists.txt` both as a source and as the ctest group name `NUX_undo_reliability`:

1. `D1: an edit that changes nothing does not push an undo step`
2. `D1: a genuine second edit still gets its own step`
3. `D2: undo, redo and an ordinary edit each say what they are`
4. `D3: undoing back to the saved state clears the modified marker`
5. `D3: a project that has never been saved stays dirty after an undo`
6. `D3: the marker is right after several edits and a partial undo`
7. `D4: every field of the project survives a snapshot round trip`
8. `D4: undo restores a field that a knob-only test would miss`
9. `Coalescing matrix: key, timing and identity`
10. `The history stays bounded and undo stops at the oldest kept step` (120-step cap)
11. `A new edit after an undo throws away the redo stack`
12. `Opening another project cannot be undone into the previous one`
13. `Undo of a recorded take removes the clip and keeps the file`

---

## 7. What is not verified

* **No GUI click-through was performed.** These tests drive `ProjectDocument` and `UndoHistory`
  directly. That the title bar actually repaints `*` after `Cmd+Z` was not exercised here; it
  rests on `MainComponent` reading `hasUnsavedChanges()`, which is unchanged.
* **No physical hardware was involved** in this phase, and nothing in this report claims any
  device-level behaviour.
* **macOS** is not built by CI and was not built locally; only Linux and Windows were compiled.
* The TSan job passed, which means no race was reported by that run — it does not prove the
  absence of races.

---

## 8. Files touched

| File | Lines changed (approx.) |
|---|---|
| `src/nux_core/UndoHistory.cpp` | +8 / −1 (duplicate-snapshot guard) |
| `src/nux_core/ProjectDocument.h` | +30 / −3 (origin enum + accessors, `savedJson`, `differsFromSavedState`) |
| `src/nux_core/ProjectDocument.cpp` | +40 / −8 (saved JSON capture, dirty computation, origin plumbing) |
| `tests/UndoReliabilityTests.cpp` | new, ~470 lines |
| `tests/UndoTests.cpp` | one group's expectation updated to the approved D3 behaviour |
| `tests/CMakeLists.txt` | +1 source, +1 ctest group |
| `PHASE_2_3_PROPOSAL.md` | the approved proposal, committed with the change |
