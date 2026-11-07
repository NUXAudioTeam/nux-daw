/* Take placement: the arithmetic behind "that recording you just made is now a clip".

   These tests exist because the logic used to live inside MainComponent and could only be
   checked by launching the application. Everything here is pure: a project state goes in, a
   project state comes out, and nothing needs a device, a window or a file on disk. */
#include <juce_core/juce_core.h>

#include "../src/nux_core/TakePlacement.h"
#include "../src/nux_core/ProjectDocument.h"
#include "../src/nux_core/Constants.h"

namespace
{
using namespace nux;

ProjectState makeProject (double tempo = 120.0)
{
    auto state = ProjectState::createDefault();
    state.tempo = tempo;
    return state;
}

TakePlacementRequest makeRequest (double startStep = 0.0, double lengthSeconds = 1.0)
{
    TakePlacementRequest request;
    request.clipName = "take";
    request.storedPath = "media/take.wav";
    request.startStep = startStep;
    request.lengthSeconds = lengthSeconds;
    return request;
}
}

class TakePlacementTests final : public juce::UnitTest
{
public:
    TakePlacementTests() : juce::UnitTest ("NUX take placement", "NUX") {}

    void runTest() override
    {
        beginTest ("A take's length in steps follows the tempo");
        {
            // One second at 120 BPM is two beats, which is eight steps at four per beat.
            expectEquals (takeLengthInSteps (1.0, 120.0, constants::stepsPerBeat), 8);
            expectEquals (takeLengthInSteps (1.0, 60.0,  constants::stepsPerBeat), 4);
            expectEquals (takeLengthInSteps (1.0, 240.0, constants::stepsPerBeat), 16);

            // Twice the length is twice the steps, within one step of rounding: ceil() does
            // not distribute over multiplication (at 140 BPM one second rounds 9.33 up to
            // 10, two seconds rounds 18.67 up to 19, not 20).
            for (const auto tempo : { 60.0, 90.0, 120.0, 140.0, 174.0 })
            {
                const auto oneStep = takeLengthInSteps (1.0, tempo, constants::stepsPerBeat);
                const auto twoSteps = takeLengthInSteps (2.0, tempo, constants::stepsPerBeat);

                expect (std::abs (twoSteps - 2 * oneStep) <= 1,
                        "doubling the take must roughly double the steps at "
                          + juce::String (tempo) + " BPM (got " + juce::String (oneStep)
                          + " and " + juce::String (twoSteps) + ")");
                expect (twoSteps > oneStep, "a longer take must never be shorter");
            }
        }

        beginTest ("A take is always at least one step long");
        {
            // A take shorter than a step still has to be visible and selectable.
            expectEquals (takeLengthInSteps (0.001, 120.0, constants::stepsPerBeat), 1);
            expectEquals (takeLengthInSteps (0.0,   120.0, constants::stepsPerBeat), 1);
            expectEquals (takeLengthInSteps (-5.0,  120.0, constants::stepsPerBeat), 1);

            const auto nan = std::numeric_limits<double>::quiet_NaN();
            expectEquals (takeLengthInSteps (nan, 120.0, constants::stepsPerBeat), 1,
                          "a NaN length must not produce a NaN clip");
            expect (takeLengthInSteps (1.0, 0.0, constants::stepsPerBeat) >= 1,
                    "a zero tempo must not divide by zero");
            expect (takeLengthInSteps (1.0, 120.0, 0) >= 1, "zero steps per beat is survivable");
        }

        beginTest ("The pattern grows in bars to hold the clip");
        {
            // 16 steps now; a clip ending at step 20 needs another bar (16 -> 32 -> capped).
            expectEquals (patternLengthForClipEnd (16, 20, 4, constants::maxNumSteps), 32);
            expectEquals (patternLengthForClipEnd (16, 17, 4, constants::maxNumSteps), 32);
            expectEquals (patternLengthForClipEnd (16, 16, 4, constants::maxNumSteps), 16,
                          "a clip that fits exactly must not grow the pattern");
            expectEquals (patternLengthForClipEnd (16, 4,  4, constants::maxNumSteps), 16,
                          "and a short clip must never shrink it");
        }

        beginTest ("Pattern growth stops at the maximum and never overflows");
        {
            expectEquals (patternLengthForClipEnd (16, 1000, 4, constants::maxNumSteps),
                          (int) constants::maxNumSteps);
            expectEquals (patternLengthForClipEnd (constants::maxNumSteps, 1000, 4,
                                                   constants::maxNumSteps),
                          (int) constants::maxNumSteps);
            expect (patternLengthForClipEnd (16, 20, 0, constants::maxNumSteps) > 16,
                    "a zero steps-per-beat must not loop forever");
        }

        beginTest ("Placing a take creates a clip at the right place");
        {
            auto state = makeProject (120.0);
            const auto result = placeTake (state, 0, makeRequest (4.0, 1.0));

            expect (result.placed, "a take must be placed: " + result.error);
            expectEquals (result.trackIndex, 0);
            expectEquals (result.clipIndex, 0);
            expectEquals (result.startStep, 4);
            expectEquals (result.lengthSteps, 8);
            expectEquals ((int) state.tracks[0].audioClips.size(), 1);

            const auto& clip = state.tracks[0].audioClips[0];
            expectEquals (clip.name, juce::String ("take"));
            expectEquals (clip.sourceFile, juce::String ("media/take.wav"));
            expectEquals (clip.startStep, 4);
            expectEquals (clip.lengthSteps, 8);
            expectEquals ((int) clip.sourceStartSample, 0,
                          "a recorded take starts at the beginning of its own file");
        }

        beginTest ("A fractional start step is rounded, not truncated");
        {
            // Recording starts on a step boundary "in principle"; in practice the playhead is
            // wherever the block landed. Truncating would move every take a little early.
            auto state = makeProject();
            const auto result = placeTake (state, 0, makeRequest (4.6, 0.5));
            expectEquals (result.startStep, 5, "4.6 steps rounds to 5, it does not truncate to 4");

            auto second = makeProject();
            expectEquals (placeTake (second, 0, makeRequest (4.4, 0.5)).startStep, 4);
        }

        beginTest ("The track becomes an audio track");
        {
            auto state = makeProject();
            expect (state.tracks[0].type != TrackType::audio, "the default track is a sampler");

            const auto result = placeTake (state, 0, makeRequest (0.0, 1.0));
            expect (result.placed, "placement should succeed");
            expect (result.trackBecameAudio, "and it should report that the type changed");
            expect (state.tracks[0].type == TrackType::audio, "the track is an audio track now");

            // A second take on the same track must not claim the conversion again.
            const auto again = placeTake (state, 0, makeRequest (8.0, 1.0));
            expect (! again.trackBecameAudio, "the second take finds an audio track already");
        }

        beginTest ("Repeated takes accumulate instead of replacing");
        {
            auto state = makeProject();

            for (int i = 0; i < 4; ++i)
            {
                const auto result = placeTake (state, 0, makeRequest (i * 8.0, 1.0));
                expect (result.placed, "take " + juce::String (i) + " should be placed");
                expectEquals (result.clipIndex, i);
            }

            const auto& clips = state.tracks[0].audioClips;
            expectEquals ((int) clips.size(), 4);

            for (int i = 0; i < 4; ++i)
                expectEquals (clips[(size_t) i].startStep, i * 8);
        }

        beginTest ("Recording a long take grows the pattern to contain it");
        {
            auto state = makeProject (120.0);
            expectEquals (state.patternLengthSteps, (int) constants::defaultNumSteps);

            // 4 seconds at 120 BPM is 32 steps, which does not fit in the default 16.
            const auto result = placeTake (state, 0, makeRequest (0.0, 4.0));
            expect (result.placed, "the take should be placed");
            expect (result.patternGrew, "and the pattern should have grown");
            expectEquals (result.lengthSteps, 32);
            expectEquals (result.patternLengthBefore, (int) constants::defaultNumSteps);
            expect (result.patternLengthAfter >= 32, "the pattern now holds the clip");
            expectEquals (state.patternLengthSteps, result.patternLengthAfter);
        }

        beginTest ("A short take leaves the pattern alone");
        {
            auto state = makeProject (120.0);
            const auto before = state.patternLengthSteps;

            const auto result = placeTake (state, 0, makeRequest (0.0, 0.5));
            expect (result.placed, "the take should be placed");
            expect (! result.patternGrew, "a half-second take fits in the default pattern");
            expectEquals (state.patternLengthSteps, before);
        }

        beginTest ("A take on a later track lands there and nowhere else");
        {
            auto state = makeProject();
            expect (state.tracks.size() >= 2, "the default project has at least two tracks");

            const auto untouchedBefore = (int) state.tracks[0].audioClips.size();
            const auto result = placeTake (state, 1, makeRequest (0.0, 1.0));

            expect (result.placed, "the take should be placed");
            expectEquals (result.trackIndex, 1);
            expectEquals ((int) state.tracks[1].audioClips.size(), 1);
            expectEquals ((int) state.tracks[0].audioClips.size(), untouchedBefore);
            expect (state.tracks[0].type != TrackType::audio,
                    "the other track must not be turned into an audio track");
        }

        beginTest ("An invalid track index falls back instead of losing the take");
        {
            // The index can be stale: a track deleted while recording, or an arm state that
            // changed. Losing a recording because of bookkeeping would be unforgivable.
            auto state = makeProject();
            const auto result = placeTake (state, 99, makeRequest (0.0, 1.0));

            expect (result.placed, "the take must still be placed");
            expectEquals (result.trackIndex, 0, "it lands on the first track");

            auto negative = makeProject();
            expectEquals (placeTake (negative, -3, makeRequest (0.0, 1.0)).trackIndex, 0);
        }

        beginTest ("An empty project is a real failure, and says so");
        {
            ProjectState empty;
            empty.tracks.clear();

            const auto result = placeTake (empty, 0, makeRequest (0.0, 1.0));

            expect (! result.placed, "there is nowhere to put the take");
            expect (result.error.isNotEmpty(), "and the caller gets a reason, not a silent return");
            expect (result.clipIndex < 0, "no clip index may be reported for a failed placement");
        }

        beginTest ("Placing a take is undoable and redoable");
        {
            // The same path the record button takes: begin the edit, place the take, undo.
            ProjectDocument document;
            auto& state = document.getState();
            state.tempo = 120.0;
            const auto clipsBefore = (int) state.tracks[0].audioClips.size();
            const auto patternBefore = state.patternLengthSteps;

            // Exactly what the record button does: an empty key, because a recording is a
            // discrete edit and not a continuation of the previous one.
            document.beginEdit ({}, "Record an audio clip");
            const auto result = placeTake (state, 0, makeRequest (0.0, 4.0));
            expect (result.placed, "the take should be placed");
            document.markDirty();

            expect ((int) state.tracks[0].audioClips.size() == clipsBefore + 1,
                    "the clip is in the project");
            expect (document.canUndo(), "recording is an undoable edit");

            expect (document.undo(), "undo should succeed");
            expectEquals ((int) document.getState().tracks[0].audioClips.size(), clipsBefore,
                          "undo must remove the take");
            expectEquals (document.getState().patternLengthSteps, patternBefore,
                          "and restore the pattern length it grew");
            expect (document.getState().tracks[0].type != TrackType::audio,
                    "and put the track type back");

            expect (document.redo(), "redo should succeed");
            expectEquals ((int) document.getState().tracks[0].audioClips.size(), clipsBefore + 1,
                          "redo must put the take back");
            expect (document.getState().patternLengthSteps >= 32,
                    "including the pattern growth it caused");
        }

        beginTest ("Two takes are two undo steps, not one");
        {
            ProjectDocument document;
            auto& state = document.getState();
            state.tempo = 120.0;

            for (int i = 0; i < 2; ++i)
            {
                document.beginEdit ({}, "Record an audio clip");
                expect (placeTake (state, 0, makeRequest (i * 4.0, 0.5)).placed);
                document.markDirty();
            }

            expectEquals ((int) state.tracks[0].audioClips.size(), 2);
            expect (document.undo(), "first undo");
            expectEquals ((int) document.getState().tracks[0].audioClips.size(), 1,
                          "one take left after undoing the second");
            expect (document.undo(), "second undo");
            expectEquals ((int) document.getState().tracks[0].audioClips.size(), 0,
                          "both takes undone, one step at a time");
        }

        beginTest ("Placement never disturbs the notes or inserts it did not touch");
        {
            auto state = makeProject();
            state.tracks[0].inserts[0].type = InsertType::eq;
            state.tracks[0].inserts[0].p1 = 6.0f;
            const auto noteCount = (int) state.tracks[0].clips.size();

            expect (placeTake (state, 0, makeRequest (0.0, 1.0)).placed);

            expect (state.tracks[0].inserts[0].type == InsertType::eq,
                    "the insert chain survives a recording");
            expect (juce::approximatelyEqual (state.tracks[0].inserts[0].p1, 6.0f));
            expectEquals ((int) state.tracks[0].clips.size(), noteCount,
                          "pattern clips are untouched");
        }
    }
};

static TakePlacementTests takePlacementTests;
