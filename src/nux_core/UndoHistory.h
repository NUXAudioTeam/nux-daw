/* NUX DAW - undo / redo.

   How this works, and why it is built this way
   -------------------------------------------
   NUX edits its project through a plain ProjectState struct that every view holds
   by reference. Replacing that with a ValueTree would be a large, risky refactor
   of every panel, so undo is done the other way round: a snapshot of the whole
   project is captured before an edit and restored on undo.

   * A snapshot is the project serialized to JSON text. The serializer already
     round trips everything (tracks, clips, inserts, plugin references, master), so
     the snapshot is guaranteed to describe everything the project contains.
   * Edits are grouped by a *transaction key*: every change made against the same
     key within `coalesceMilliseconds` becomes one undo step. A knob drag is then
     one step in the history, not two hundred.
   * Restoring a snapshot is a whole-project operation, so the host has to rebuild
     the audio graph and the views afterwards. The document broadcasts a change
     message with a "restored" hint so callers know to do the heavy refresh.

   Snapshots cost a JSON write; NUX projects are small (kilobytes), and it happens
   only when the user actually edits something. */
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <vector>

#include "../nux_core/ProjectSerializer.h"

namespace nux
{
/** One point in the project's history. */
struct UndoSnapshot
{
    juce::String json;
    juce::String description;
};

class UndoHistory final
{
public:
    UndoHistory() = default;

    /** Called before an edit is applied: remembers the state the project had.

        `key` identifies the edit, so repeated changes with the same key (a knob
        drag, typing a name) collapse into a single history entry. Pass an empty key
        to force a discrete step. */
    void beginEdit (const ProjectState& state, const juce::String& key, const juce::String& description);

    /** True when there is something to undo / redo. */
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }

    /** Takes the state to restore and moves the history pointer. Returns an empty
        string when there was nothing to undo. */
    juce::String undo (const juce::String& currentJson);
    juce::String redo (const juce::String& currentJson);

    /** Description of the step undo/redo would take, for menu text. */
    juce::String getUndoDescription() const;
    juce::String getRedoDescription() const;

    /** Forgets everything (used when a different project is opened). */
    void clear();

    int getUndoCount() const noexcept { return (int) undoStack.size(); }
    int getRedoCount() const noexcept { return (int) redoStack.size(); }

    /** How long the same edit key keeps coalescing into one step. */
    static constexpr juce::int64 coalesceMilliseconds = 450;

    /** Caps memory: an old history is dropped rather than growing without limit. */
    static constexpr int maximumSteps = 120;

private:
    std::vector<UndoSnapshot> undoStack;
    std::vector<UndoSnapshot> redoStack;

    juce::String lastEditKey;
    juce::int64 lastEditTimeMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndoHistory)
};
}
