#include "ProjectDocument.h"

namespace nux
{
ProjectDocument::ProjectDocument()
{
    state = ProjectState::createDefault();
}

void ProjectDocument::createNew()
{
    state = ProjectState::createDefault();
    currentFile = juce::File();
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
    setHasUnsavedChanges (true);
}
}
