/* NUX DAW - core constants shared by every layer. */
#pragma once

#include <juce_core/juce_core.h>

namespace nux::constants
{
inline constexpr int maxTracks        = 32;
inline constexpr int numMixerBuses    = 8;
inline constexpr int maxTrackInserts  = 4;
inline constexpr int maxBusInserts    = 2;
inline constexpr int maxClipsPerTrack = 32;
inline constexpr int maxNotesPerClip  = 128;
inline constexpr int maxAudioClipsPerTrack = 8;
inline constexpr int defaultNumSteps  = 16;
inline constexpr int maxNumSteps      = 32;
inline constexpr int minNumSteps      = 4;

inline constexpr double minTempo = 20.0;
inline constexpr double maxTempo = 300.0;

/*  NUX works on a 16th-note grid: one "step" is a 16th note, so a 16 step
    pattern is one bar of 4/4. All musical positions in the engine are in steps. */
inline constexpr int stepsPerBeat = 4;

inline constexpr const char* appName     = "NUX DAW";
inline constexpr const char* companyName = "NUX Audio Works";
inline constexpr const char* versionString = "0.3.0";
inline constexpr const char* projectFileExtension = ".nux";
inline constexpr int projectFormatVersion = 1;
}
