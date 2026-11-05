/* NUX DAW - plain data model for a project.
   This header deliberately depends only on juce_core so that the model can be
   unit tested without any GUI, audio device or plugin scanner running. */
#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <vector>

#include "Constants.h"

namespace nux
{
enum class TrackType
{
    sampler,   // step-sequenced virtual instrument track
    audio      // imported / recorded audio clip track
};

enum class InsertType
{
    none,
    eq,
    compressor,
    delay,
    reverb
};

/** One insert slot on a channel strip. Parameters are generic so that the
    serializer, the UI and the DSP layer can share a single slot description. */
struct InsertSlot
{
    InsertType type = InsertType::none;
    bool bypassed   = false;
    float p1 = 0.0f;
    float p2 = 0.0f;
    float p3 = 0.0f;

    bool isActive() const noexcept { return type != InsertType::none && ! bypassed; }
    bool operator== (const InsertSlot& o) const noexcept
    {
        return type == o.type && bypassed == o.bypassed
            && juce::exactlyEqual (p1, o.p1)
            && juce::exactlyEqual (p2, o.p2)
            && juce::exactlyEqual (p3, o.p3);
    }
    bool operator!= (const InsertSlot& o) const noexcept { return ! (*this == o); }
};

/** A note inside a pattern clip. `step` is relative to the clip start. */
struct Note
{
    int step      = 0;
    int midiNote  = 60;
    float velocity = 0.8f;

    bool operator== (const Note& o) const noexcept
    {
        return step == o.step && midiNote == o.midiNote && juce::exactlyEqual (velocity, o.velocity);
    }
};

/** A pattern clip: a chunk of sequenced notes placed on the timeline. */
struct Clip
{
    juce::String name { "Pattern" };
    int startStep   = 0;
    int lengthSteps = constants::defaultNumSteps;
    std::vector<Note> notes;

    int getEndStep() const noexcept { return startStep + lengthSteps; }

    bool operator== (const Clip& o) const noexcept
    {
        return startStep == o.startStep && lengthSteps == o.lengthSteps && notes == o.notes;
    }
};

/** A placed audio clip. `sourceFile` is relative to the media folder. */
struct AudioClip
{
    juce::String name { "Audio" };
    juce::String sourceFile;
    int startStep   = 0;
    int lengthSteps = constants::defaultNumSteps;
    juce::int64 sourceStartSample = 0;

    bool operator== (const AudioClip& o) const noexcept
    {
        return sourceFile == o.sourceFile && startStep == o.startStep
            && lengthSteps == o.lengthSteps && sourceStartSample == o.sourceStartSample;
    }
};

/** One 16th-note step of the drum sequencer. */
struct Step
{
    bool enabled   = false;
    float velocity = 0.85f;
};

struct TrackState
{
    juce::String id;
    juce::String name { "Track" };
    TrackType type = TrackType::sampler;

    juce::uint32 colourARGB = 0xff2ee6c4u;

    float volumeDb = 0.0f;   // -60 .. +6
    float pan      = 0.0f;   // -1 .. +1
    bool muted     = false;
    bool soloed    = false;
    bool recordArmed = false;
    int busIndex   = 0;      // index into the mixer buses

    /** Sampler voice parameters. */
    int rootNote    = 36;
    float cutoffHz  = 12000.0f;
    float resonance = 0.7f;
    int instrumentPreset = 0; // 0 = Analog Kit, 1 = Sub Bass, 2 = Pluck ...

    std::array<InsertSlot, constants::maxTrackInserts> inserts {};

    int numSteps = constants::defaultNumSteps;
    std::array<Step, constants::maxNumSteps> steps {};

    std::vector<Clip> clips;
    std::vector<AudioClip> audioClips;

    juce::Colour getColour() const;
    void setColour (juce::Colour c) noexcept { colourARGB = (juce::uint32) c.getARGB(); }
};

struct BusState
{
    juce::String name { "Bus" };
    float volumeDb = 0.0f;
    float pan      = 0.0f;
    bool muted     = false;
    std::array<InsertSlot, constants::maxBusInserts> inserts {};
};

struct MasterState
{
    float volumeDb = 0.0f;
    bool limiterEnabled = true;
    std::array<InsertSlot, constants::maxTrackInserts> inserts {};
};

struct ProjectState
{
    juce::String name { "Untitled" };
    double tempo          = 120.0;
    int patternLengthSteps = constants::defaultNumSteps;
    bool loopEnabled       = true;
    int timeSignatureNumerator = 4;

    MasterState master;
    std::array<BusState, constants::numMixerBuses> buses {};
    std::vector<TrackState> tracks;

    /** Creates the empty song used when no project is loaded. */
    static ProjectState createDefault();
};

/** dB -> linear gain, with -60 dB and below treated as silence. */
inline float dbToGain (float db) noexcept
{
    return db <= -59.9f ? 0.0f : (float) std::pow (10.0, (double) db / 20.0);
}

inline float gainToDb (float gain) noexcept
{
    return gain <= 0.0001f ? -60.0f : (float) (20.0 * std::log10 ((double) gain));
}
}
