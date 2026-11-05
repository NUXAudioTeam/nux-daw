#include "TrackModel.h"

namespace nux
{
juce::Colour TrackState::getColour() const
{
    return juce::Colour (colourARGB | 0xff000000u);
}

namespace
{
TrackState makeTrack (const juce::String& id, const juce::String& name,
                      TrackType type, juce::uint32 colour, int busIndex, int preset)
{
    TrackState t;
    t.id = id;
    t.name = name;
    t.type = type;
    t.colourARGB = colour;
    t.busIndex = busIndex;
    t.instrumentPreset = preset;
    return t;
}
}

ProjectState ProjectState::createDefault()
{
    ProjectState p;
    p.name = "Untitled";
    p.tempo = 120.0;

    for (int i = 0; i < constants::numMixerBuses; ++i)
        p.buses[(size_t) i].name = "Bus " + juce::String (i + 1);

    auto drums = makeTrack ("trk-drums", "Drums", TrackType::sampler, 0xff2ee6c4u, 0, 0);
    auto bass  = makeTrack ("trk-bass",  "Bass",  TrackType::sampler, 0xff7c9cffu, 0, 1);
    auto audio = makeTrack ("trk-audio", "Audio 1", TrackType::audio, 0xffffb454u, 0, 0);

    // A starter groove so that the very first launch makes sound.
    for (int s = 0; s < constants::defaultNumSteps; s += 4)
        drums.steps[(size_t) s] = { true, 1.0f };
    for (int s = 2; s < constants::defaultNumSteps; s += 4)
        drums.steps[(size_t) s] = { true, 0.7f };

    p.tracks.push_back (std::move (drums));
    p.tracks.push_back (std::move (bass));
    p.tracks.push_back (std::move (audio));

    return p;
}
}
