/* NUX DAW - undo / redo tests.

   Undo is snapshot based: the whole project is serialized before an edit and
   restored on undo. These tests check the mechanics (stack order, coalescing,
   redo invalidation) and that a restored snapshot really does bring back the data,
   including the parts that are easy to forget like audio clips and plugin slots. */
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "../src/nux_core/ProjectDocument.h"
#include "../src/nux_core/UndoHistory.h"

namespace nux
{
class UndoTests final : public juce::UnitTest
{
public:
    UndoTests() : juce::UnitTest ("NUX undo history", "NUX") {}

    void runTest() override
    {
        beginTest ("An edit can be undone and redone");
        {
            ProjectDocument document;

            expect (! document.canUndo(), "A fresh project must have nothing to undo");

            document.beginEdit ("rename", "Rename a track");
            document.getState().tracks[0].name = "Kick";

            expect (document.canUndo());
            expectEquals (document.getUndoDescription(), juce::String ("Rename a track"));

            expect (document.undo());
            expectEquals (document.getState().tracks[0].name, juce::String ("Drums"));
            expect (document.canRedo());

            expect (document.redo());
            expectEquals (document.getState().tracks[0].name, juce::String ("Kick"));
        }

        beginTest ("Undo restores a value, not just the shape of the project");
        {
            ProjectDocument document;
            auto& state = document.getState();

            document.beginEdit ("volume", "Change the volume");
            state.tracks[0].volumeDb = -12.0f;
            state.tracks[1].pan = 0.5f;
            state.master.volumeDb = -3.0f;
            state.tempo = 140.0;
            state.patternLengthSteps = 32;

            expect (document.undo());

            expectWithinAbsoluteError (document.getState().tracks[0].volumeDb, 0.0f, 0.001f);
            expectWithinAbsoluteError (document.getState().tracks[1].pan, 0.0f, 0.001f);
            expectWithinAbsoluteError (document.getState().master.volumeDb, 0.0f, 0.001f);
            expectWithinAbsoluteError (document.getState().tempo, 120.0, 0.001);
            expectEquals (document.getState().patternLengthSteps, constants::defaultNumSteps);
        }

        beginTest ("A knob drag is one undo step, not hundreds");
        {
            ProjectDocument document;

            // Simulate a drag: the same key, many small changes, no delay between them.
            for (int i = 0; i < 50; ++i)
            {
                document.beginEdit ("fader", "Change the volume");
                document.getState().tracks[0].volumeDb = -0.5f * (float) i;
            }

            expectEquals (document.getHistorySteps(), 1);

            expect (document.undo());
            expectWithinAbsoluteError (document.getState().tracks[0].volumeDb, 0.0f, 0.001f);
        }

        beginTest ("Different edits are separate steps");
        {
            ProjectDocument document;

            document.beginEdit ("volume", "Change the volume");
            document.getState().tracks[0].volumeDb = -6.0f;

            document.beginEdit ("rename", "Rename a track");
            document.getState().tracks[0].name = "Snare";

            expectEquals (document.getHistorySteps(), 2);

            expect (document.undo());
            expectEquals (document.getState().tracks[0].name, juce::String ("Drums"));
            expectWithinAbsoluteError (document.getState().tracks[0].volumeDb, -6.0f, 0.001f);

            expect (document.undo());
            expectWithinAbsoluteError (document.getState().tracks[0].volumeDb, 0.0f, 0.001f);
        }

        beginTest ("A new edit clears the redo stack");
        {
            ProjectDocument document;

            document.beginEdit ("a", "First edit");
            document.getState().tracks[0].name = "One";

            expect (document.undo());
            expect (document.canRedo());

            document.beginEdit ("b", "Second edit");
            document.getState().tracks[0].name = "Two";

            expect (! document.canRedo(), "Edits after an undo must invalidate the redo path");
        }

        beginTest ("Audio clips and plugin slots survive a restore");
        {
            ProjectDocument document;
            auto& state = document.getState();

            document.beginEdit ("import", "Import an audio clip");

            AudioClip clip;
            clip.name = "Take 1";
            clip.sourceFile = "media/take1.wav";
            clip.lengthSteps = 16;
            state.tracks[2].audioClips.push_back (clip);

            state.tracks[0].inserts[0].type = InsertType::plugin;
            state.tracks[0].inserts[0].plugin.name = "Super Comp";
            state.tracks[0].inserts[0].plugin.descriptionXml = "<PLUGIN name=\"Super Comp\"/>";
            state.tracks[0].inserts[0].pluginStateBase64 = "c3RhdGU=";
            state.tracks[0].instrumentPlugin.name = "Tone Gen";
            state.tracks[0].instrumentPlugin.descriptionXml = "<PLUGIN name=\"Tone Gen\"/>";

            expect (document.undo());

            const auto& restored = document.getState();

            expectEquals ((int) restored.tracks[2].audioClips.size(), 0);
            expect (restored.tracks[0].inserts[0].type != InsertType::plugin,
                    "An undone plugin insert must be gone");
            expect (! restored.tracks[0].usesPluginInstrument(),
                    "An undone hosted instrument must be gone");

            // And redoing brings all of it back.
            expect (document.redo());
            const auto& again = document.getState();

            expectEquals ((int) again.tracks[2].audioClips.size(), 1);
            expectEquals (again.tracks[2].audioClips[0].name, juce::String ("Take 1"));
            expectEquals (again.tracks[0].inserts[0].plugin.name, juce::String ("Super Comp"));
            expectEquals (again.tracks[0].inserts[0].pluginStateBase64, juce::String ("c3RhdGU="));
            expect (again.tracks[0].usesPluginInstrument());
        }

        beginTest ("Undo marks the project as unsaved");
        {
            ProjectDocument document;
            expect (! document.hasUnsavedChanges());

            document.beginEdit ("x", "Change something");
            document.getState().tracks[0].name = "Changed";

            document.beginEdit ("y", "Change something else");
            document.getState().tracks[0].volumeDb = -6.0f;

            // One of the two edits undone: the project still differs from the saved state.
            expect (document.undo());
            expect (document.hasUnsavedChanges(),
                    "An undo that stops short of the saved state is still unsaved work");

            // Both undone: we are back where we started, so the marker goes (D3).
            expect (document.undo());
            expect (! document.hasUnsavedChanges(),
                    "Undoing all the way back to the saved state is not unsaved work");
        }

        beginTest ("An empty history does nothing and says so");
        {
            ProjectDocument document;

            expect (! document.undo());
            expect (! document.redo());
            expect (! document.canUndo());
            expect (! document.canRedo());
            expectEquals (document.getUndoCount(), 0);
        }

        beginTest ("Loading a project clears the history");
        {
            ProjectDocument document;

            document.beginEdit ("x", "Change something");
            document.getState().tracks[0].name = "Changed";

            expect (document.canUndo());

            document.clearHistory();

            expect (! document.canUndo());
            expect (! document.canRedo());
        }

        beginTest ("The history is bounded");
        {
            ProjectDocument document;

            for (int i = 0; i < UndoHistory::maximumSteps + 20; ++i)
            {
                // A distinct key each time, so nothing coalesces.
                document.beginEdit ("edit " + juce::String (i), "Edit " + juce::String (i));
                document.getState().tracks[0].volumeDb = (float) (i % 10);
            }

            expect (document.getUndoCount() <= UndoHistory::maximumSteps,
                    "The undo stack must not grow without limit");
        }
    }
};

static UndoTests undoTests;
}
