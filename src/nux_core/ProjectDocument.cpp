#include "ProjectDocument.h"

namespace nux
{
ProjectDocument::ProjectDocument()
{
    state = ProjectState::createDefault();

    // A brand-new document is clean: the default state is its own reference point.
    savedJson = serializer::toJson (state);
}

void ProjectDocument::createNew()
{
    state = ProjectState::createDefault();
    currentFile = juce::File();
    history.clear();
    lastChangeOrigin = ChangeOrigin::edit;
    savedJson = serializer::toJson (state);
    setHasUnsavedChanges (false);
    sendChangeMessage();
}

bool ProjectDocument::loadFromFile (const juce::File& file, juce::String& error)
{
    ProjectState loaded;

    if (! serializer::loadFromFile (file, loaded, error))
        return false;

    state = std::move (loaded);
    currentFile = file;
    history.clear();
    lastChangeOrigin = ChangeOrigin::edit;
    savedJson = serializer::toJson (state);
    setHasUnsavedChanges (false);
    sendChangeMessage();
    return true;
}

bool ProjectDocument::saveToFile (const juce::File& file, juce::String& error)
{
    auto target = file;

    if (! target.hasFileExtension (constants::projectFileExtension))
        target = target.withFileExtension (constants::projectFileExtension);

    if (! serializer::saveToFile (state, target, error))
        return false;

    currentFile = target;
    savedJson = serializer::toJson (state);
    setHasUnsavedChanges (false);
    sendChangeMessage();
    return true;
}

bool ProjectDocument::saveToCurrentFile (juce::String& error)
{
    if (! currentFile.existsAsFile() && currentFile.getFullPathName().isEmpty())
    {
        error = "Choose a file name to save this project.";
        return false;
    }

    return saveToFile (currentFile, error);
}

juce::String ProjectDocument::getDisplayName() const
{
    if (currentFile == juce::File())
        return state.name;

    return currentFile.getFileNameWithoutExtension();
}

void ProjectDocument::setHasUnsavedChanges (bool shouldBeDirty)
{
    if (dirty != shouldBeDirty)
    {
        dirty = shouldBeDirty;
        sendChangeMessage();
    }
}

juce::File ProjectDocument::getProjectFolder() const
{
    if (currentFile == juce::File())
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("NUX Projects");

    return currentFile.getParentDirectory();
}

juce::File ProjectDocument::getMediaFolder() const
{
    return getProjectFolder().getChildFile ("media");
}

void ProjectDocument::markDirty()
{
    // An ordinary edit. Before this, the flag set by an undo stayed set for the rest of the
    // session, so every later change claimed to be an undo. D2.
    lastChangeOrigin = ChangeOrigin::edit;
    setHasUnsavedChanges (true);
}

//==============================================================================
void ProjectDocument::beginEdit (const juce::String& key, const juce::String& description)
{
    history.beginEdit (state, key, description);
}

bool ProjectDocument::restoreFromJson (const juce::String& json, ChangeOrigin origin)
{
    if (json.isEmpty())
        return false;

    ProjectState restored;
    juce::String error;

    if (! serializer::fromJson (json, restored, error))
        return false;

    state = std::move (restored);

    // D2: say which way the history actually moved. Redo used to be reported as an undo.
    lastChangeOrigin = origin;

    // D3: the project is dirty only if it differs from what was last saved or loaded.
    // Undoing back to the saved state used to leave the modified marker on forever.
    setHasUnsavedChanges (differsFromSavedState());

    sendChangeMessage();
    return true;
}

bool ProjectDocument::differsFromSavedState() const
{
    const auto current = serializer::toJson (state);

    // A document that has never been saved has no reference point, so any state counts as
    // unsaved - except the untouched default, which is what createNew() records.
    return savedJson.isEmpty() || current != savedJson;
}

bool ProjectDocument::undo()
{
    const auto target = history.undo (serializer::toJson (state));

    if (target.isEmpty())
        return false;

    return restoreFromJson (target, ChangeOrigin::undo);
}

bool ProjectDocument::redo()
{
    const auto target = history.redo (serializer::toJson (state));

    if (target.isEmpty())
        return false;

    return restoreFromJson (target, ChangeOrigin::redo);
}

void ProjectDocument::clearHistory()
{
    history.clear();
}
}
