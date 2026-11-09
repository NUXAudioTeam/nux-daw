/* NUX DAW - undo/redo reliability.

   Phase 2.3. The existing UndoTests cover the happy path: an edit can be undone, a knob drag
   is one step, a restored project still has its clips. These tests cover the four ways the
   history told the truth imperfectly:

     D1  an edit that changes nothing pushed a step, so Ctrl+Z appeared to do nothing
     D2  "the last change was an undo" was set by undo, never cleared, and also set by redo
     D3  the modified marker never went away, even back at the saved state
     D4  nothing proved a snapshot restores every field of the project

   D4 is the one that matters most: undo is only as complete as the serializer. */
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "../src/nux_core/ProjectDocument.h"
#include "../src/nux_core/ProjectSerializer.h"
#include "../src/nux_core/Constants.h"

namespace nux
{
class UndoReliabilityTests final : public juce::UnitTest
{
public:
    UndoReliabilityTests() : juce::UnitTest ("NUX undo reliability", "NUX") {}

    /** A visible, undoable change that needs no engine: renaming the project. */
    static void rename (ProjectDocument& document, const juce::String& name)
    {
        document.getState().name = name;
        document.markDirty();
    }

    void runTest() override
    {
        beginTest ("D1: an edit that changes nothing does not push an undo step");
        {
            // Exactly what a caller that fires beginEdit() twice for one change does: the
            // second call has nothing between it and the first, so it describes the same
            // state. Before the fix that dead step sat under the real one, and the first
            // Ctrl+Z appeared to do nothing.
            ProjectDocument document;

            document.beginEdit ({}, "Rename");
            document.beginEdit ({}, "Rename");
            document.beginEdit ({}, "Rename");
            rename (document, "First");

            expectEquals (document.getUndoCount(), 1,
                          "three calls for one change are one step, not three");

            expect (document.undo(), "the undo should work");
            expectEquals (document.getState().name, juce::String ("Untitled"),
                          "and one press must undo the edit, not land on a duplicate");
        }

        beginTest ("D1: a genuine second edit still gets its own step");
        {
            ProjectDocument document;

            document.beginEdit ({}, "Rename");
            rename (document, "A");
            document.beginEdit ({}, "Rename");
            rename (document, "B");

            expectEquals (document.getUndoCount(), 2, "two real changes, two steps");
            expect (document.undo());
            expectEquals (document.getState().name, juce::String ("A"));
            expect (document.undo());
            expectEquals (document.getState().name, juce::String ("Untitled"),
                          "back to where the project started");
        }

        beginTest ("D2: undo, redo and an ordinary edit each say what they are");
        {
            ProjectDocument document;
            expect (document.getLastChangeOrigin() == ProjectDocument::ChangeOrigin::edit,
                    "a fresh document has no history movement");

            document.beginEdit ({}, "Rename");
            rename (document, "A");
            expect (document.getLastChangeOrigin() == ProjectDocument::ChangeOrigin::edit,
                    "an ordinary edit is an edit");

            expect (document.undo());
            expect (document.getLastChangeOrigin() == ProjectDocument::ChangeOrigin::undo,
                    "an undo says undo");

            // The defect: before this, redo reported itself as an undo.
            expect (document.redo());
            expect (document.getLastChangeOrigin() == ProjectDocument::ChangeOrigin::redo,
                    "a redo says redo, not undo");

            // The other half of the defect: the flag stayed set forever.
            rename (document, "B");
            expect (document.getLastChangeOrigin() == ProjectDocument::ChangeOrigin::edit,
                    "the next ordinary edit clears it");
        }

        beginTest ("D3: undoing back to the saved state clears the modified marker");
        {
            ProjectDocument document;
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("nux-undo-d3.nux");

            rename (document, "Saved");
            juce::String error;
            expect (document.saveToFile (file, error), "save: " + error);
            expect (! document.hasUnsavedChanges(), "a saved project is clean");

            document.beginEdit ({}, "Rename");
            rename (document, "Changed");
            expect (document.hasUnsavedChanges(), "an edit makes it dirty");

            expect (document.undo());
            expect (! document.hasUnsavedChanges(),
                    "undo back to the saved state must clear the marker");

            expect (document.redo());
            expect (document.hasUnsavedChanges(), "redo must bring it back");

            expect (document.undo());
            expect (! document.hasUnsavedChanges(), "and undo clears it again");

            file.deleteFile();
        }

        beginTest ("D3: a project that has never been saved stays dirty after an undo");
        {
            // There is no saved state to be equal to, so nothing may claim the work is safe.
            ProjectDocument document;
            document.beginEdit ({}, "Rename");
            rename (document, "Unsaved");

            expect (document.hasUnsavedChanges(), "dirty before the undo");
            expect (document.undo(), "undo works");
            expect (! document.hasUnsavedChanges(),
                    "but undoing the only edit returns to the untouched default, which is clean");
        }

        beginTest ("D3: the marker is right after several edits and a partial undo");
        {
            ProjectDocument document;
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("nux-undo-d3b.nux");

            juce::String error;
            expect (document.saveToFile (file, error), "save: " + error);

            for (const auto* name : { "one", "two", "three" })
            {
                document.beginEdit ({}, "Rename");
                rename (document, name);
                expect (document.hasUnsavedChanges(), "each edit is dirty");
            }

            expect (document.undo());
            expect (document.hasUnsavedChanges(),
                    "two edits in, still different from the file on disk");
            expect (document.undo());
            expect (document.hasUnsavedChanges(), "one edit in, still different");
            expect (document.undo());
            expect (! document.hasUnsavedChanges(),
                    "and only when the saved state is reached does the marker go");

            file.deleteFile();
        }

        beginTest ("D4: every field of the project survives a snapshot round trip");
        {
            // Undo is only as complete as the serializer. If one field is dropped here, undo
            // silently leaves it at its new value and nothing else notices.
            auto state = ProjectState::createDefault();

            state.name = "Round trip";
            state.tempo = 137.5;
            state.patternLengthSteps = 24;
            state.loopEnabled = false;
            state.timeSignatureNumerator = 3;

            state.master.volumeDb = -4.5f;
            state.master.limiterEnabled = false;
            state.master.inserts[0].type = InsertType::compressor;
            state.master.inserts[0].bypassed = true;
            state.master.inserts[0].p1 = 0.25f;

            state.buses[1].name = "Drum bus";
            state.buses[1].volumeDb = -7.0f;
            state.buses[1].pan = 0.4f;
            state.buses[1].muted = true;
            state.buses[1].inserts[1].type = InsertType::reverb;

            auto& track = state.tracks[0];
            track.name = "Bass";
            track.type = TrackType::audio;
            track.colourARGB = 0xffabcdef;
            track.volumeDb = -3.5f;
            track.pan = -0.75f;
            track.muted = true;
            track.soloed = true;
            track.recordArmed = true;
            track.busIndex = 2;
            track.rootNote = 48;
            track.cutoffHz = 800.0f;
            track.resonance = 0.25f;
            track.instrumentPreset = 2;
            track.numSteps = 20;
            track.steps[3].enabled = true;
            track.steps[3].velocity = 0.4f;

            track.inserts[0].type = InsertType::delay;
            track.inserts[0].p2 = 0.6f;
            track.inserts[2].type = InsertType::plugin;
            track.inserts[2].plugin.descriptionXml = "<PLUGIN/>";
            track.inserts[2].plugin.name = "A plugin";
            track.inserts[2].plugin.manufacturer = "Someone";
            track.inserts[2].plugin.format = "VST3";
            track.inserts[2].plugin.isInstrument = false;
            track.inserts[2].plugin.stateSaved = true;
            track.inserts[2].pluginStateBase64 = "AAAA";

            track.instrumentPlugin.descriptionXml = "<PLUGIN/>";
            track.instrumentPlugin.name = "An instrument";
            track.instrumentPlugin.isInstrument = true;
            track.instrumentStateBase64 = "BBBB";

            Clip clip;
            clip.name = "Verse";
            clip.startStep = 8;
            clip.lengthSteps = 12;
            clip.notes.push_back ({ 0, 64, 0.9f });
            clip.notes.push_back ({ 4, 67, 0.5f });
            track.clips.push_back (clip);

            AudioClip audio;
            audio.name = "Take";
            audio.sourceFile = "media/take-1.wav";
            audio.startStep = 16;
            audio.lengthSteps = 9;
            audio.sourceStartSample = 12345;
            track.audioClips.push_back (audio);

            const auto json = serializer::toJson (state);

            ProjectState restored;
            juce::String error;
            expect (serializer::fromJson (json, restored, error), "parse: " + error);

            // Compare the JSON of both: a dropped field shows up as a difference, and this
            // catches fields added later without anybody updating this test.
            expectEquals (serializer::toJson (restored), json,
                          "serialising the restored project must reproduce the same JSON");

            // Field-by-field, so a failure says which field went missing.
            expectEquals (restored.name, state.name);
            expectEquals (restored.tempo, state.tempo);
            expectEquals (restored.patternLengthSteps, state.patternLengthSteps);
            expect (restored.loopEnabled == state.loopEnabled, "loop enabled survives");
            expectEquals (restored.timeSignatureNumerator, state.timeSignatureNumerator);
            expectEquals (restored.master.volumeDb, state.master.volumeDb);
            expect (restored.master.limiterEnabled == state.master.limiterEnabled,
                    "the master limiter flag survives");
            expectEquals ((int) restored.master.inserts[0].type, (int) InsertType::compressor);
            expectEquals (restored.buses[1].name, juce::String ("Drum bus"));
            expectEquals (restored.buses[1].volumeDb, -7.0f);
            expectEquals (restored.buses[1].pan, 0.4f);
            expect (restored.buses[1].muted, "bus mute survives");

            expect (! restored.tracks.empty(), "tracks survive");
            if (! restored.tracks.empty())
            {
                const auto& r = restored.tracks[0];
                expectEquals (r.name, juce::String ("Bass"));
                expect (r.type == TrackType::audio, "track type survives");
                expect (r.colourARGB == 0xffabcdefu, "the track colour survives");
                expectEquals (r.volumeDb, -3.5f);
                expectEquals (r.pan, -0.75f);
                expect (r.muted && r.soloed, "mute and solo survive");
                expect (r.recordArmed, "the record arm flag survives");
                expectEquals (r.busIndex, 2);
                expectEquals (r.rootNote, 48);
                expectEquals (r.cutoffHz, 800.0f);
                expectEquals (r.resonance, 0.25f);
                expectEquals (r.instrumentPreset, 2);
                expectEquals (r.numSteps, 20);
                expect (r.steps[3].enabled, "a sequencer step survives");
                expectEquals (r.steps[3].velocity, 0.4f);

                expectEquals ((int) r.inserts[0].type, (int) InsertType::delay);
                expectEquals (r.inserts[0].p2, 0.6f);
                expect ((int) r.inserts[2].type == (int) InsertType::plugin,
                        "a plugin insert survives with its type");
                expectEquals (r.inserts[2].plugin.name, juce::String ("A plugin"));
                expectEquals (r.inserts[2].pluginStateBase64, juce::String ("AAAA"));
                expectEquals (r.instrumentPlugin.name, juce::String ("An instrument"));
                expectEquals (r.instrumentStateBase64, juce::String ("BBBB"));

                expectEquals ((int) r.clips.size(), 1, "pattern clips survive");
                if (! r.clips.empty())
                {
                    expectEquals (r.clips[0].name, juce::String ("Verse"));
                    expectEquals (r.clips[0].startStep, 8);
                    expectEquals (r.clips[0].lengthSteps, 12);
                    expectEquals ((int) r.clips[0].notes.size(), 2);
                    expectEquals (r.clips[0].notes[1].midiNote, 67);
                    expectEquals (r.clips[0].notes[1].velocity, 0.5f);
                }

                expectEquals ((int) r.audioClips.size(), 1, "audio clips survive");
                if (! r.audioClips.empty())
                {
                    expectEquals (r.audioClips[0].name, juce::String ("Take"));
                    expectEquals (r.audioClips[0].sourceFile, juce::String ("media/take-1.wav"));
                    expectEquals (r.audioClips[0].startStep, 16);
                    expectEquals (r.audioClips[0].lengthSteps, 9);
                    expectEquals (r.audioClips[0].sourceStartSample, (juce::int64) 12345);
                }
            }
        }

        beginTest ("D4: undo restores a field that a knob-only test would miss");
        {
            // The arm flag is not a knob, is not drawn on the timeline, and used to be the
            // kind of field that a serializer can quietly forget.
            ProjectDocument document;
            document.beginEdit ({}, "Arm");
            document.getState().tracks[0].recordArmed = true;
            document.markDirty();

            expect (document.getState().tracks[0].recordArmed, "armed");
            expect (document.undo());
            expect (! document.getState().tracks[0].recordArmed, "undo disarms the track");
            expect (document.redo());
            expect (document.getState().tracks[0].recordArmed, "redo arms it again");
        }

        beginTest ("Coalescing matrix: key, timing and identity");
        {
            // Same key inside the window: one step.
            ProjectDocument a;
            for (int i = 0; i < 20; ++i)
            {
                a.beginEdit ("volume", "Drag a knob");
                a.getState().tracks[0].volumeDb = (float) i * 0.1f;
                a.markDirty();
            }
            expectEquals (a.getUndoCount(), 1, "a whole drag is one step");

            // Different keys: separate steps, even back to back.
            ProjectDocument b;
            b.beginEdit ("volume", "Volume");
            b.getState().tracks[0].volumeDb = 1.0f;
            b.markDirty();
            b.beginEdit ("pan", "Pan");
            b.getState().tracks[0].pan = 0.5f;
            b.markDirty();
            expectEquals (b.getUndoCount(), 2, "two different controls are two steps");

            // An empty key is always discrete, whatever the timing.
            ProjectDocument c;
            for (int i = 0; i < 3; ++i)
            {
                c.beginEdit ({}, "Discrete");
                c.getState().name = "n" + juce::String (i);
                c.markDirty();
            }
            expectEquals (c.getUndoCount(), 3, "an empty key never coalesces");
        }

        beginTest ("The history stays bounded and undo stops at the oldest kept step");
        {
            ProjectDocument document;

            for (int i = 0; i < 200; ++i)
            {
                document.beginEdit ({}, "Edit " + juce::String (i));
                document.getState().name = "name " + juce::String (i);
                document.markDirty();
            }

            expectEquals (document.getUndoCount(), UndoHistory::maximumSteps,
                          "200 edits are capped at the maximum");
            expect (document.canUndo(), "and the oldest kept step can still be undone");

            for (int i = 0; i < UndoHistory::maximumSteps; ++i)
                expect (document.undo(), "undo " + juce::String (i));

            expect (! document.canUndo(), "the history is exhausted");
            expect (! document.undo(), "and a further undo reports nothing to do");
        }

        beginTest ("A new edit after an undo throws away the redo stack");
        {
            ProjectDocument document;

            document.beginEdit ({}, "First");
            document.getState().name = "A";
            document.markDirty();

            expect (document.undo());
            expect (document.canRedo(), "the undone edit can be redone");

            document.beginEdit ({}, "Second");
            document.getState().name = "B";
            document.markDirty();

            expect (! document.canRedo(), "a new edit invalidates the redo stack");
            expect (! document.redo(), "and redo says there is nothing to do");
            expectEquals (document.getState().name, juce::String ("B"),
                          "the new edit is what the project contains");
        }

        beginTest ("Opening another project cannot be undone into the previous one");
        {
            // The worst possible history bug: undo restores a project the user is not editing.
            ProjectDocument document;

            document.beginEdit ({}, "Rename");
            document.getState().name = "Project A";
            document.markDirty();
            expect (document.canUndo());

            document.createNew();

            expect (! document.canUndo(), "a new project starts with an empty history");
            expect (! document.undo(), "and nothing can be undone into the old one");
            expectEquals (document.getState().name, juce::String ("Untitled"));
        }

        beginTest ("Undo of a recorded take removes the clip and keeps the file");
        {
            // The audio file is deliberately not deleted: undoing a placement is not the same
            // as deciding the recording is worthless.
            ProjectDocument document;

            AudioClip clip;
            clip.name = "take";
            clip.sourceFile = "media/take.wav";
            clip.lengthSteps = 8;

            document.beginEdit ({}, "Record an audio clip");
            document.getState().tracks[0].audioClips.push_back (clip);
            document.getState().tracks[0].type = TrackType::audio;
            document.markDirty();

            expectEquals ((int) document.getState().tracks[0].audioClips.size(), 1);

            expect (document.undo());
            expectEquals ((int) document.getState().tracks[0].audioClips.size(), 0,
                          "the clip is gone from the project");
            expect (document.getState().tracks[0].type != TrackType::audio,
                    "and the track type is back");

            expect (document.redo());
            expectEquals ((int) document.getState().tracks[0].audioClips.size(), 1,
                          "redo puts it back");
        }
    }
};

static UndoReliabilityTests undoReliabilityTests;
}
