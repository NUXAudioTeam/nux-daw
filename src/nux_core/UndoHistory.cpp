#include "UndoHistory.h"

namespace nux
{
void UndoHistory::beginEdit (const ProjectState& state, const juce::String& key,
                             const juce::String& description)
{
    const auto now = juce::Time::currentTimeMillis();

    // Coalesce: the same edit repeated within the window is one history step, so a
    // knob drag or a held key does not fill the stack.
    if (key.isNotEmpty()
        && key == lastEditKey
        && now - lastEditTimeMs <= coalesceMilliseconds
        && ! undoStack.empty())
    {
        lastEditTimeMs = now;
        return;
    }

    undoStack.push_back ({ serializer::toJson (state), description });

    if ((int) undoStack.size() > maximumSteps)
        undoStack.erase (undoStack.begin());

    // A new edit invalidates anything that was undone.
    redoStack.clear();

    lastEditKey = key;
    lastEditTimeMs = now;
}

juce::String UndoHistory::undo (const juce::String& currentJson)
{
    if (undoStack.empty())
        return {};

    // The state as it is right now becomes the redo target.
    auto step = undoStack.back();
    undoStack.pop_back();

    redoStack.push_back ({ currentJson, step.description });

    // The coalescing window must not swallow the next edit after a jump.
    lastEditKey.clear();
    lastEditTimeMs = 0;

    return step.json;
}

juce::String UndoHistory::redo (const juce::String& currentJson)
{
    if (redoStack.empty())
        return {};

    auto step = redoStack.back();
    redoStack.pop_back();

    undoStack.push_back ({ currentJson, step.description });

    lastEditKey.clear();
    lastEditTimeMs = 0;

    return step.json;
}

juce::String UndoHistory::getUndoDescription() const
{
    return undoStack.empty() ? juce::String() : undoStack.back().description;
}

juce::String UndoHistory::getRedoDescription() const
{
    return redoStack.empty() ? juce::String() : redoStack.back().description;
}

void UndoHistory::clear()
{
    undoStack.clear();
    redoStack.clear();
    lastEditKey.clear();
    lastEditTimeMs = 0;
}
}
