#include "ProjectSerializer.h"

namespace nux::serializer
{
namespace
{
const char* trackTypeToString (TrackType t) { return t == TrackType::audio ? "audio" : "sampler"; }
TrackType trackTypeFromString (const juce::String& s) { return s == "audio" ? TrackType::audio : TrackType::sampler; }

const char* insertTypeToString (InsertType t)
{
    switch (t)
    {
        case InsertType::eq:         return "eq";
        case InsertType::compressor: return "compressor";
        case InsertType::delay:      return "delay";
        case InsertType::reverb:     return "reverb";
        case InsertType::none:
        default:                     return "none";
    }
}

InsertType insertTypeFromString (const juce::String& s)
{
    if (s == "eq")         return InsertType::eq;
    if (s == "compressor") return InsertType::compressor;
    if (s == "delay")      return InsertType::delay;
    if (s == "reverb")     return InsertType::reverb;
    return InsertType::none;
}

juce::var insertToVar (const InsertSlot& slot)
{
    if (slot.type == InsertType::none)
        return {};

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("type", insertTypeToString (slot.type));
    obj->setProperty ("bypassed", slot.bypassed);
    obj->setProperty ("p1", slot.p1);
    obj->setProperty ("p2", slot.p2);
    obj->setProperty ("p3", slot.p3);
    return juce::var (obj);
}

InsertSlot insertFromVar (const juce::var& v)
{
    InsertSlot slot;
    if (auto* obj = v.getDynamicObject())
    {
        slot.type     = insertTypeFromString (obj->getProperty ("type").toString());
        slot.bypassed = (bool) obj->getProperty ("bypassed");
        slot.p1       = (float) (double) obj->getProperty ("p1");
        slot.p2       = (float) (double) obj->getProperty ("p2");
        slot.p3       = (float) (double) obj->getProperty ("p3");
    }
    return slot;
}

template <size_t N>
juce::var insertChainToVar (const std::array<InsertSlot, N>& chain)
{
    juce::Array<juce::var> arr;
    for (const auto& slot : chain)
        arr.add (insertToVar (slot));
    return arr;
}

template <size_t N>
void insertChainFromVar (const juce::var& v, std::array<InsertSlot, N>& chain)
{
    if (auto* arr = v.getArray())
        for (size_t i = 0; i < N && i < (size_t) arr->size(); ++i)
            chain[i] = insertFromVar ((*arr)[(int) i]);
}

double getDouble (const juce::var& v, double fallback)
{
    return v.isVoid() || v.isUndefined() ? fallback : (double) v;
}

int getInt (const juce::var& v, int fallback)
{
    return v.isVoid() || v.isUndefined() ? fallback : (int) v;
}

bool getBool (const juce::var& v, bool fallback)
{
    return v.isVoid() || v.isUndefined() ? fallback : (bool) v;
}

juce::var clipToVar (const Clip& clip)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", clip.name);
    obj->setProperty ("start", clip.startStep);
    obj->setProperty ("length", clip.lengthSteps);

    juce::Array<juce::var> notes;
    for (const auto& n : clip.notes)
    {
        auto* no = new juce::DynamicObject();
        no->setProperty ("step", n.step);
        no->setProperty ("note", n.midiNote);
        no->setProperty ("vel", n.velocity);
        notes.add (juce::var (no));
    }
    obj->setProperty ("notes", notes);
    return juce::var (obj);
}

Clip clipFromVar (const juce::var& v)
{
    Clip clip;
    if (auto* obj = v.getDynamicObject())
    {
        clip.name = obj->getProperty ("name").toString();
        if (clip.name.isEmpty()) clip.name = "Pattern";
        clip.startStep   = getInt (obj->getProperty ("start"), 0);
        clip.lengthSteps = juce::jmax (1, getInt (obj->getProperty ("length"), constants::defaultNumSteps));

        if (auto* notes = obj->getProperty ("notes").getArray())
        {
            for (const auto& nv : *notes)
            {
                if (auto* no = nv.getDynamicObject())
                {
                    Note n;
                    n.step     = juce::jlimit (0, clip.lengthSteps - 1, getInt (no->getProperty ("step"), 0));
                    n.midiNote = juce::jlimit (0, 127, getInt (no->getProperty ("note"), 60));
                    n.velocity = juce::jlimit (0.0f, 1.0f, (float) getDouble (no->getProperty ("vel"), 0.8));
                    clip.notes.push_back (n);
                }
            }
        }
    }
    return clip;
}

juce::var audioClipToVar (const AudioClip& clip)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", clip.name);
    obj->setProperty ("file", clip.sourceFile);
    obj->setProperty ("start", clip.startStep);
    obj->setProperty ("length", clip.lengthSteps);
    obj->setProperty ("sourceStart", (juce::int64) clip.sourceStartSample);
    return juce::var (obj);
}

AudioClip audioClipFromVar (const juce::var& v)
{
    AudioClip clip;
    if (auto* obj = v.getDynamicObject())
    {
        clip.name = obj->getProperty ("name").toString();
        if (clip.name.isEmpty()) clip.name = "Audio";
        clip.sourceFile         = obj->getProperty ("file").toString();
        clip.startStep          = getInt (obj->getProperty ("start"), 0);
        clip.lengthSteps        = juce::jmax (1, getInt (obj->getProperty ("length"), constants::defaultNumSteps));
        clip.sourceStartSample  = (juce::int64) (double) getDouble (obj->getProperty ("sourceStart"), 0.0);
    }
    return clip;
}

juce::var trackToVar (const TrackState& t)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("id", t.id);
    obj->setProperty ("name", t.name);
    obj->setProperty ("type", trackTypeToString (t.type));
    obj->setProperty ("colour", (juce::int64) (t.colourARGB & 0x00ffffffu));
    obj->setProperty ("volumeDb", t.volumeDb);
    obj->setProperty ("pan", t.pan);
    obj->setProperty ("muted", t.muted);
    obj->setProperty ("soloed", t.soloed);
    obj->setProperty ("recordArmed", t.recordArmed);
    obj->setProperty ("bus", t.busIndex);
    obj->setProperty ("rootNote", t.rootNote);
    obj->setProperty ("cutoff", t.cutoffHz);
    obj->setProperty ("resonance", t.resonance);
    obj->setProperty ("preset", t.instrumentPreset);
    obj->setProperty ("inserts", insertChainToVar (t.inserts));

    juce::Array<juce::var> steps;
    for (int i = 0; i < t.numSteps; ++i)
    {
        juce::Array<juce::var> stepArr;
        stepArr.add (t.steps[(size_t) i].enabled);
        stepArr.add (t.steps[(size_t) i].velocity);
        steps.add (stepArr);
    }
    obj->setProperty ("numSteps", t.numSteps);
    obj->setProperty ("steps", steps);

    juce::Array<juce::var> clips;
    for (const auto& c : t.clips) clips.add (clipToVar (c));
    obj->setProperty ("clips", clips);

    juce::Array<juce::var> audioClips;
    for (const auto& c : t.audioClips) audioClips.add (audioClipToVar (c));
    obj->setProperty ("audioClips", audioClips);

    return juce::var (obj);
}

TrackState trackFromVar (const juce::var& v)
{
    TrackState t;
    if (auto* obj = v.getDynamicObject())
    {
        t.id   = obj->getProperty ("id").toString();
        t.name = obj->getProperty ("name").toString();
        if (t.name.isEmpty()) t.name = "Track";
        t.type = trackTypeFromString (obj->getProperty ("type").toString());

        const auto colour = (juce::uint32) (juce::int64) (double) getDouble (obj->getProperty ("colour"), 0x2ee6c4);
        t.colourARGB = 0xff000000u | (colour & 0x00ffffffu);

        t.volumeDb    = juce::jlimit (-60.0f, 6.0f, (float) getDouble (obj->getProperty ("volumeDb"), 0.0));
        t.pan         = juce::jlimit (-1.0f, 1.0f, (float) getDouble (obj->getProperty ("pan"), 0.0));
        t.muted       = getBool (obj->getProperty ("muted"), false);
        t.soloed      = getBool (obj->getProperty ("soloed"), false);
        t.recordArmed = getBool (obj->getProperty ("recordArmed"), false);
        t.busIndex    = juce::jlimit (0, constants::numMixerBuses - 1, getInt (obj->getProperty ("bus"), 0));
        t.rootNote    = juce::jlimit (0, 127, getInt (obj->getProperty ("rootNote"), 36));
        t.cutoffHz    = juce::jlimit (60.0f, 20000.0f, (float) getDouble (obj->getProperty ("cutoff"), 12000.0));
        t.resonance   = juce::jlimit (0.1f, 8.0f, (float) getDouble (obj->getProperty ("resonance"), 0.7));
        t.instrumentPreset = juce::jlimit (0, 7, getInt (obj->getProperty ("preset"), 0));

        insertChainFromVar (obj->getProperty ("inserts"), t.inserts);

        t.numSteps = juce::jlimit (constants::minNumSteps, constants::maxNumSteps,
                                   getInt (obj->getProperty ("numSteps"), constants::defaultNumSteps));

        if (auto* steps = obj->getProperty ("steps").getArray())
        {
            for (int i = 0; i < juce::jmin (t.numSteps, steps->size()); ++i)
            {
                if (auto* pair = (*steps)[i].getArray())
                {
                    Step s;
                    s.enabled  = (*pair)[0];
                    s.velocity = juce::jlimit (0.05f, 1.0f, (float) (double) (*pair)[1]);
                    t.steps[(size_t) i] = s;
                }
            }
        }

        if (auto* clips = obj->getProperty ("clips").getArray())
            for (const auto& c : *clips)
                t.clips.push_back (clipFromVar (c));

        if (auto* audioClips = obj->getProperty ("audioClips").getArray())
            for (const auto& c : *audioClips)
                t.audioClips.push_back (audioClipFromVar (c));
    }
    return t;
}
} // namespace

int currentSchemaVersion() { return constants::projectFormatVersion; }

juce::String toJson (const ProjectState& state)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "nux-project");
    root->setProperty ("schema", currentSchemaVersion());
    root->setProperty ("app", constants::appName);
    root->setProperty ("appVersion", constants::versionString);
    root->setProperty ("name", state.name);
    root->setProperty ("tempo", state.tempo);
    root->setProperty ("patternLength", state.patternLengthSteps);
    root->setProperty ("loop", state.loopEnabled);
    root->setProperty ("timeSigNum", state.timeSignatureNumerator);

    auto* master = new juce::DynamicObject();
    master->setProperty ("volumeDb", state.master.volumeDb);
    master->setProperty ("limiter", state.master.limiterEnabled);
    master->setProperty ("inserts", insertChainToVar (state.master.inserts));
    root->setProperty ("master", juce::var (master));

    juce::Array<juce::var> buses;
    for (const auto& b : state.buses)
    {
        auto* bo = new juce::DynamicObject();
        bo->setProperty ("name", b.name);
        bo->setProperty ("volumeDb", b.volumeDb);
        bo->setProperty ("pan", b.pan);
        bo->setProperty ("muted", b.muted);
        bo->setProperty ("inserts", insertChainToVar (b.inserts));
        buses.add (juce::var (bo));
    }
    root->setProperty ("buses", buses);

    juce::Array<juce::var> tracks;
    for (const auto& t : state.tracks) tracks.add (trackToVar (t));
    root->setProperty ("tracks", tracks);

    return juce::JSON::toString (juce::var (root), false);
}

bool fromJson (const juce::String& json, ProjectState& destination, juce::String& error)
{
    juce::var parsed;
    const auto parseResult = juce::JSON::parse (json, parsed);

    if (parseResult.failed())
    {
        error = "Not a valid JSON document: " + parseResult.getErrorMessage();
        return false;
    }

    auto* root = parsed.getDynamicObject();

    if (root == nullptr)
    {
        error = "The project root must be a JSON object.";
        return false;
    }

    const auto format = root->getProperty ("format").toString();

    if (format.isNotEmpty() && format != "nux-project")
    {
        error = "Unsupported project format: " + format;
        return false;
    }

    const auto schema = getInt (root->getProperty ("schema"), currentSchemaVersion());

    if (schema > currentSchemaVersion())
    {
        error = "This project was written by a newer NUX version (schema "
                + juce::String (schema) + "). Update NUX to open it.";
        return false;
    }

    ProjectState state;
    state.name = root->getProperty ("name").toString();
    if (state.name.isEmpty()) state.name = "Untitled";
    state.tempo = juce::jlimit (constants::minTempo, constants::maxTempo, getDouble (root->getProperty ("tempo"), 120.0));
    state.patternLengthSteps = juce::jlimit (constants::minNumSteps, constants::maxNumSteps,
                                             getInt (root->getProperty ("patternLength"), constants::defaultNumSteps));
    state.loopEnabled = getBool (root->getProperty ("loop"), true);
    state.timeSignatureNumerator = juce::jlimit (1, 16, getInt (root->getProperty ("timeSigNum"), 4));

    if (auto* master = root->getProperty ("master").getDynamicObject())
    {
        state.master.volumeDb = juce::jlimit (-60.0f, 6.0f, (float) getDouble (master->getProperty ("volumeDb"), 0.0));
        state.master.limiterEnabled = getBool (master->getProperty ("limiter"), true);
        insertChainFromVar (master->getProperty ("inserts"), state.master.inserts);
    }

    for (auto& b : state.buses) b.name = "Bus";

    if (auto* buses = root->getProperty ("buses").getArray())
    {
        for (int i = 0; i < juce::jmin (constants::numMixerBuses, buses->size()); ++i)
        {
            if (auto* bo = (*buses)[i].getDynamicObject())
            {
                auto& b = state.buses[(size_t) i];
                b.name     = bo->getProperty ("name").toString();
                if (b.name.isEmpty()) b.name = "Bus " + juce::String (i + 1);
                b.volumeDb = juce::jlimit (-60.0f, 6.0f, (float) getDouble (bo->getProperty ("volumeDb"), 0.0));
                b.pan      = juce::jlimit (-1.0f, 1.0f, (float) getDouble (bo->getProperty ("pan"), 0.0));
                b.muted    = getBool (bo->getProperty ("muted"), false);
                insertChainFromVar (bo->getProperty ("inserts"), b.inserts);
            }
        }
    }

    if (auto* tracks = root->getProperty ("tracks").getArray())
    {
        for (const auto& tv : *tracks)
        {
            if (state.tracks.size() >= (size_t) constants::maxTracks)
                break;
            if (tv.getDynamicObject() != nullptr)
                state.tracks.push_back (trackFromVar (tv));
        }
    }

    // Guarantee at least one track so the UI always has something to show.
    if (state.tracks.empty())
        state.tracks.push_back (trackFromVar (juce::var (new juce::DynamicObject())));

    destination = std::move (state);
    error.clear();
    return true;
}

bool saveToFile (const ProjectState& state, const juce::File& file, juce::String& error)
{
    const auto parent = file.getParentDirectory();

    if (parent != juce::File() && ! parent.isDirectory() && ! parent.createDirectory().wasOk())
    {
        error = "Could not create the folder: " + parent.getFullPathName();
        return false;
    }

    juce::TemporaryFile temp (file);

    if (! temp.getFile().replaceWithText (toJson (state), false, false, "\n"))
    {
        error = "Could not write to: " + file.getFullPathName();
        return false;
    }

    if (! temp.overwriteTargetFileWithTemporary())
    {
        error = "Could not replace the existing project file: " + file.getFullPathName();
        return false;
    }

    return true;
}

bool loadFromFile (const juce::File& file, ProjectState& destination, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "Project file not found: " + file.getFullPathName();
        return false;
    }

    return fromJson (file.loadFileAsString(), destination, error);
}
}
