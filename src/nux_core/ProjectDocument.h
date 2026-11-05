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

    /** True when the most recent change message came from an undo or redo, so the
        host knows to rebuild the audio graph rather than just repaint. */
    bool wasLastChangeAnUndo() const noexcept { return lastChangeWasUndo; }

    /** Clears the history (a new or freshly opened project starts with none). */
    void clearHistory();

private:
    bool restoreFromJson (const juce::String& json, bool isUndo);

    ProjectState state;
    juce::File currentFile;
    bool dirty = false;
    bool lastChangeWasUndo = false;
    UndoHistory history;
};
}
