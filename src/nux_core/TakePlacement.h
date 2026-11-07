/* NUX DAW - placing a finished take on the timeline.

   This used to live in MainComponent, which meant the most error-prone part of recording -
   "how long is this take in steps, where does it start, and does the pattern have to grow
   to hold it" - could only be checked by launching the application and reading a status
   line. It is arithmetic, and arithmetic belongs somewhere it can be tested.

   Nothing here knows about components, windows, the audio engine or files on disk. The
   caller decides what the file is called and how its path should be stored; this module
   decides what the project looks like afterwards. */
#pragma once

#include "TrackModel.h"

namespace nux
{
/** A take that has finished recording and wants to become a clip. */
struct TakePlacementRequest
{
    /** Clip name; usually the file name without its extension. */
    juce::String clipName;

    /** How the source file should be stored in the project: relative to the project when
        the take is inside it, absolute when it is not. The caller resolves this because it
        knows where the project lives. */
    juce::String storedPath;

    /** Where the take began, measured in steps. Fractional while recording. */
    double startStep = 0.0;

    /** How long the audio actually is. */
    double lengthSeconds = 0.0;
};

/** What happened, so the caller can rebuild the graph, refresh views and say something
    useful. */
struct TakePlacementResult
{
    bool placed = false;
    juce::String error;          // set when placed == false

    int trackIndex = -1;         // the track the clip really landed on
    int clipIndex = -1;          // index of the new clip inside that track
    int startStep = 0;
    int lengthSteps = 0;
    bool trackBecameAudio = false;

    int patternLengthBefore = 0;
    int patternLengthAfter = 0;
    bool patternGrew = false;
};

/** How many steps a take of this length occupies at this tempo.

    Rounded up, never below one: a take that is half a step long still has to occupy a step
    or it would be invisible and unselectable. */
int takeLengthInSteps (double lengthSeconds, double tempo, int stepsPerBeat);

/** The pattern length needed to contain a clip that ends at `clipEndStep`.

    Grows in bars, the way the pattern length control does, and stops at `maxSteps`. It never
    shrinks: recording a short take must not truncate the pattern someone just built. */
int patternLengthForClipEnd (int currentLength, int clipEndStep, int stepsPerBeat, int maxSteps);

/** Places the take on `trackIndex` and grows the pattern if it has to.

    An out-of-range track index falls back to the first track, which is what the record
    button has always done - a take must not be lost because the arm state changed while it
    was recording. An empty project is a real failure and says so. */
TakePlacementResult placeTake (ProjectState& state,
                               int trackIndex,
                               const TakePlacementRequest& request);
}
