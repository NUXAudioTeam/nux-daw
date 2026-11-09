/* NUX DAW - the open project: state plus everything the UI needs to know about
   where it came from and whether it has unsaved work. */
#pragma once

#include <juce_events/juce_events.h>
#include "ProjectSerializer.h"
#include "UndoHistory.h"

namespace nux
{
class ProjectDocument final : public juce::ChangeBroadcaster
{
public:
    ProjectDocument();
    ~ProjectDocument() override = default;

    ProjectState& getState() noexcept { return state; }
    const ProjectState& getState() const noexcept { return state; }

    /** Creates a new empty song in memory. */
    void createNew();

    bool loadFromFile (const juce::File& file, juce::String& error);
    bool saveToFile (const juce::File& file, juce::String& error);
    bool saveToCurrentFile (juce::String& error);

    juce::File getCurrentFile() const { return currentFile; }
    juce::String getDisplayName() const;
    bool hasUnsavedChanges() const noexcept { return dirty; }
    void setHasUnsavedChanges (bool shouldBeDirty);

    juce::File getProjectFolder() const;
    juce::File getMediaFolder() const;

    /** Marks the project as changed and notifies listeners (coalesced). */
    void markDirty();

    //==============================================================================
    // Undo / redo

    /** Records the current project state before an edit is applied.

        Every mutation should be preceded by this. `key` groups repeated edits (a
        knob drag) into one undo step; leave it empty for a discrete step. */
    void beginEdit (const juce::String& key, const juce::String& description);

    /** Replaces the project state with the restored snapshot and tells listeners.
        Returns false when there was nothing to undo / redo. */
    bool undo();
    bool redo();

    bool canUndo() const noexcept { return history.canUndo(); }
    bool canRedo() const noexcept { return history.canRedo(); }
    juce::String getUndoDescription() const { return history.getUndoDescription(); }
    juce::String getRedoDescription() const { return history.getRedoDescription(); }
    int getUndoCount() const noexcept { return history.getUndoCount(); }
    int getHistorySteps() const noexcept { return history.getUndoCount(); }

    /** Where the most recent change message came from.

        `edit` is an ordinary change; `undo` and `redo` mean the whole project was replaced
        from a snapshot, so the host has to rebuild the audio graph rather than just repaint.
        This used to be a bool that was set by undo, never cleared, and also set by redo. */
    enum class ChangeOrigin { edit, undo, redo };

    ChangeOrigin getLastChangeOrigin() const noexcept { return lastChangeOrigin; }

    /** Kept for existing callers: true when the last change was a snapshot restore. */
    bool wasLastChangeAnUndo() const noexcept { return lastChangeOrigin != ChangeOrigin::edit; }

    /** Clears the history (a new or freshly opened project starts with none). */
    void clearHistory();

private:
    bool restoreFromJson (const juce::String& json, ChangeOrigin origin);

    /** True when the project differs from what is on disk. D3. */
    bool differsFromSavedState() const;

    ProjectState state;
    juce::File currentFile;
    bool dirty = false;
    ChangeOrigin lastChangeOrigin = ChangeOrigin::edit;

    /** The project as it was at the last save or load. Undo compares against this so a
        project can be clean again without being saved twice. */
    juce::String savedJson;
    UndoHistory history;
};
}
