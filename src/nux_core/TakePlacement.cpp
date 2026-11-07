#include "TakePlacement.h"
#include "Constants.h"

namespace nux
{
int takeLengthInSteps (double lengthSeconds, double tempo, int stepsPerBeat)
{
    if (tempo <= 0.0 || stepsPerBeat <= 0)
        return 1;

    // Guard the degenerate cases before the arithmetic rather than after it: a screen full
    // of NaN is a worse bug report than "at least one step".
    if (! (lengthSeconds > 0.0) || ! std::isfinite (lengthSeconds))
        return 1;

    const auto steps = lengthSeconds * tempo / 60.0 * (double) stepsPerBeat;

    if (! std::isfinite (steps))
        return stepsPerBeat;   // nonsense in, a sane bar out

    return juce::jmax (1, (int) std::ceil (steps));
}

int patternLengthForClipEnd (int currentLength, int clipEndStep, int stepsPerBeat, int maxSteps)
{
    auto length = juce::jmax (1, currentLength);

    if (clipEndStep <= length)
        return length;   // never shrink

    if (stepsPerBeat <= 0)
        stepsPerBeat = constants::stepsPerBeat;

    const auto barInSteps = juce::jmax (1, stepsPerBeat * 4);

    while (length < clipEndStep && length < maxSteps)
        length += barInSteps;

    return juce::jmin (length, maxSteps);
}

TakePlacementResult placeTake (ProjectState& state,
                               int trackIndex,
                               const TakePlacementRequest& request)
{
    TakePlacementResult result;

    if (state.tracks.empty())
    {
        result.error = "There are no tracks to record onto.";
        return result;
    }

    // A take that finished recording must not be thrown away because the track index it
    // started on is no longer valid (a track deleted while recording, or an arm state that
    // changed). The first track is the same fallback the record button always used.
    const auto target = juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size())
                          ? trackIndex
                          : 0;

    auto& track = state.tracks[(size_t) target];

    result.patternLengthBefore = state.patternLengthSteps;

    AudioClip clip;
    clip.name = request.clipName;
    clip.sourceFile = request.storedPath;
    clip.startStep = (int) std::round (request.startStep);
    clip.lengthSteps = takeLengthInSteps (request.lengthSeconds, state.tempo,
                                          constants::stepsPerBeat);
    clip.sourceStartSample = 0;

    // A recorded take turns the track into an audio track, exactly like an import.
    result.trackBecameAudio = track.type != TrackType::audio;
    track.type = TrackType::audio;

    result.clipIndex = (int) track.audioClips.size();
    track.audioClips.push_back (clip);

    const auto clipEnd = clip.startStep + clip.lengthSteps;
    const auto newLength = patternLengthForClipEnd (state.patternLengthSteps, clipEnd,
                                                    constants::stepsPerBeat,
                                                    constants::maxNumSteps);

    result.patternGrew = newLength > state.patternLengthSteps;
    state.patternLengthSteps = newLength;

    result.placed = true;
    result.trackIndex = target;
    result.startStep = clip.startStep;
    result.lengthSteps = clip.lengthSteps;
    result.patternLengthAfter = state.patternLengthSteps;
    return result;
}
}
