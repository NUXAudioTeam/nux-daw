/* NUX DAW - reads and writes the .nux project format.

   The .nux file is a UTF-8 JSON document. Every field is optional when reading:
   missing values fall back to the defaults in ProjectState, so that files written
   by older builds keep opening in newer ones. */
#pragma once

#include "TrackModel.h"

namespace nux
{
struct ProjectFile
{
    ProjectState state;
    juce::String projectFolder;   // absolute folder that holds media/
};

namespace serializer
{
/** Serialises a project to the .nux JSON text. */
juce::String toJson (const ProjectState& state);

/** Parses .nux JSON text. Returns false and fills `error` when the document is
    not valid NUX JSON. Structural problems are tolerated, a broken document is not. */
bool fromJson (const juce::String& json, ProjectState& destination, juce::String& error);

/** Saves to a file, creating parent folders as required. */
bool saveToFile (const ProjectState& state, const juce::File& file, juce::String& error);

/** Loads a .nux file. */
bool loadFromFile (const juce::File& file, ProjectState& destination, juce::String& error);

/** Current schema version written into every file. */
int currentSchemaVersion();
}
}
