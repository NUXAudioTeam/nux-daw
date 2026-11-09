# Phase 2.3 — Undo/Redo Reliability: approved and implemented

> **Status: approved by the user, and D1–D4 are implemented.** The work is on branch
> `feature/undo-reliability`, in PR #10. D1, D2, D3 and D4 are done; nothing outside the scope
> below was added. The measured results — the pre-fix (red) runs, the post-fix runs, the
> full-suite regression and the CI runs — are in `UNDO_REDO_TEST_REPORT.md`.
>
> The sections below are the original proposal, kept unchanged as the record of what was
> approved and why. Their "proposal / for approval" wording describes the document as it was
> written at audit time, not the current state of the code.

Audited on 2026-11-08 against `feature/recording-reliability` (`123793e`).
Evidence below is from the actual source: `src/nux_core/UndoHistory.cpp`,
`src/nux_core/ProjectDocument.cpp`, `src/nux_ui/MainComponent.cpp`.

---

## 1. The design being audited

Undo is snapshot-based: `beginEdit()` serialises the whole `ProjectState` to JSON *before* an
edit; `undo()` restores the previous snapshot into the same `state` object every view holds by
reference. Editing groups by an edit *key*: the same key within 450 ms coalesces into one step
(a knob drag is one undo, not two hundred). History is capped at 120 steps.

This is a sound design for this codebase. The defects below are all in the details.

---

## 2. D1 — a repeated `beginEdit()` creates an undo step that does nothing

```cpp
// UndoHistory::beginEdit
if (key.isNotEmpty() && key == lastEditKey && now - lastEditTimeMs <= coalesce...) { return; }
undoStack.push_back ({ serializer::toJson (state), description });   // no comparison with the state
```

An empty key forces a discrete step — as it must, because that is how a recording became its own
undo step in 2.1. But it also means that a caller which calls `beginEdit()` twice for one edit, or
calls it and then changes nothing, pushes a snapshot identical to the one below it. Pressing undo
then restores the current state: **the user presses Ctrl+Z and nothing happens**, presses again,
and now two edits disappear at once. "Undo is broken" is the bug report; the real fault is a
duplicate step.

**Proposal:** skip the push when the new snapshot is byte-identical to the top of the stack (still
updating the coalescing timer). Cheap: the JSON is already being produced. A second caller can no
longer poison the history, and it does not change any existing behaviour that was correct.

**Test:** place a take, call `beginEdit` twice with no change between, then undo once — the clip
must be gone in one press. Today it takes two.

---

## 3. D2 — the "this change came from undo" flag never goes back to false

```cpp
bool ProjectDocument::restoreFromJson (const juce::String& json, bool isUndo)
{
    ...
    lastChangeWasUndo = true;
    ...
    juce::ignoreUnused (isUndo);
}
```

Two problems in four lines:

- `isUndo` is ignored, so **redo is reported as an undo**.
- Nothing ever sets it back to `false`. `markDirty()` does not, so after the first undo the
  document claims every subsequent change is an undo for the rest of the session. Any code asking
  `wasLastChangeAnUndo()` to choose between "repaint" and "rebuild the world" takes the heavy path
  forever.

**Proposal:** replace the bool with the direction the history actually moved
(`None` / `Undo` / `Redo`), set it in `restoreFromJson`, and reset it to `None` in `markDirty()`
and after a save.

**Test:** undo → flag says Undo; redo → flag says Redo; then a normal edit → flag says None.

---

## 4. D3 — the modified marker survives a trip back to the saved state

```cpp
bool ProjectDocument::restoreFromJson (...)
{
    state = std::move (restored);
    dirty = true;          // always, whatever the snapshot contains
```

Save a project, move a clip, undo it, and the window title still shows `*`. The document is
identical to the file on disk and still claims to be modified. This is the smaller of the four,
but it is the one a user notices daily.

**Proposal:** remember the JSON of the state at the last save or load; after a restore, set the
modified flag to `restoredJson != savedJson`. Also routes through `setHasUnsavedChanges()` so
listeners hear about it.

**This changes visible behaviour**, which is why the proposal exists: after this, undoing back to
the saved state clears the `*`. Most DAWs do the same; some keep the flag. **Say if you want the
current behaviour kept** — it is a one-line difference either way.

---

## 5. D4 — nothing proves a snapshot restores *everything*

Undo is only as complete as the serializer. If `toJson`/`fromJson` drops a single field — a
`recordArmed` flag, an insert parameter, a clip's `sourceStartSample`, the pattern length — undo
silently leaves that field at its new value. No crash, no warning, and no existing test looks for it.

**Proposal:** a test that mutates a field in every part of `ProjectState` (tempo, name, pattern
length, per-track type/name/volume/pan/mute/solo/arm, inserts with all parameters, pattern clips,
audio clips with every member, master settings), serialises, deserialises, and compares **field by
field** against the original. If it finds a gap, fixing the serializer is part of 2.3; if it does
not, the coverage becomes a permanent guard.

This is the test I would write first, because it decides whether D1–D3 are the whole story.

---

## 6. Boundary coverage worth having once the four are fixed

| Test | Why |
|---|---|
| Coalescing matrix: same key inside the window = one step; same key outside = two; different keys = two; empty key = always a discrete step | the rule that made 2.1's bug possible |
| `maximumSteps` (120): 200 edits, then undo 120 times, then a 121st undo returns nothing | the cap is real but never demonstrated |
| Memory stays bounded across 500 edits | snapshots are whole-project JSON |
| `createNew()` and `loadFromFile()` clear the history | otherwise undo can restore the *previous project* into the current one — worth proving it cannot |
| Undo after a recording is placed removes the clip, the audio stays on disk | 2.1 covers the clip; the file deliberately is not deleted, and that deserves a test that says so |
| Redo stack is dropped by a new edit | classic, easy to break |

---

## 7. What 2.3 will not do

- It will not move the state model to `ValueTree` or any other refactor. The snapshot design stays.
- It will not add a visible undo history panel.
- It will not touch the audio engine, the recorder or take placement except where a test needs to
  place a take.
- It will not merge anything or change `main`.

---

## 8. Estimated shape of the change

| File | Change |
|---|---|
| `src/nux_core/UndoHistory.{h,cpp}` | D1 duplicate-step guard; keep the coalescing rule as is |
| `src/nux_core/ProjectDocument.{h,cpp}` | D2 direction enum, D3 saved-state comparison |
| `tests/UndoTests.cpp` + new `tests/UndoReliabilityTests.cpp` | D1–D4 and the boundary table |
| `src/nux_ui/MainComponent.cpp` | only if D2's accessor name changes |
| `UNDO_REDO_TEST_REPORT.md` | new, with actual results |

Roughly 200 lines of source, 400 of tests. The work is verification-heavy by design: four of the
six defects are about what the code *claims*, not about what it computes.

---

## 9. Implementation status

| Item | Status | Where |
|---|---|---|
| **D1** — a repeated `beginEdit()` must not push a dead step | **implemented** | `src/nux_core/UndoHistory.cpp` |
| **D2** — the document must say which way the history moved (redo was reported as undo) | **implemented** | `src/nux_core/ProjectDocument.{h,cpp}` |
| **D3** — undoing back to the saved state must clear `*`; redo must re-dirty it | **implemented**, decision as approved | `src/nux_core/ProjectDocument.{h,cpp}` |
| **D4** — the undo round trip must be proven field by field | **implemented** | `tests/UndoReliabilityTests.cpp` |
| Boundary table (coalescing, 120-step cap, redo stack, project switch, recorded take) | **covered** | `tests/UndoReliabilityTests.cpp` |
| `UNDO_REDO_TEST_REPORT.md` | **written**, with actual results | repository root |

No item outside D1–D4 was added to the scope.

The estimate in §8 held: the source change is small, and the tests are the bulk of the work.

Verification summary, all of it measured:

* every new group was run against the pre-fix code first and **failed** there;
* after the fix: `NUXTests` **19/19 groups pass** locally (Linux Release, g++ 14.2, JUCE 9.0.5);
* CI **4/4 jobs success** on both the push and the pull-request run (Linux Release, Linux TSan,
  Windows Debug + leak detector, Windows Release);
* the recorded take undo keeps the recorded file on disk;
* **not** verified: no GUI click-through, no physical hardware, no macOS build.
