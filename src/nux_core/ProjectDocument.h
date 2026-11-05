/* NUX DAW - the open project: state plus everything the UI needs to know about
   where it came from and whether it has unsaved work. */
#pragma once

#include <juce_events/juce_events.h>
#include "ProjectSerializer.h"

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

private:
    ProjectState state;
    juce::File currentFile;
    bool dirty = false;
};
}
