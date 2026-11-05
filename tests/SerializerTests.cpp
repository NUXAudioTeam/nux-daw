#include <juce_core/juce_core.h>
#include "../src/nux_core/ProjectSerializer.h"

namespace nux
{
class SerializerTests final : public juce::UnitTest
{
public:
    SerializerTests() : juce::UnitTest ("NUX project serialization", "NUX") {}

    void runTest() override
    {
        beginTest ("Round trip preserves the project");
        {
            auto original = ProjectState::createDefault();
            original.name = "Round Trip";
            original.tempo = 137.5;
            original.patternLengthSteps = 32;
            original.loopEnabled = false;

            original.tracks[0].name = "Kick";
            original.tracks[0].volumeDb = -4.5f;
            original.tracks[0].pan = -0.35f;
            original.tracks[0].muted = true;
            original.tracks[0].soloed = true;
            original.tracks[0].busIndex = 3;
            original.tracks[0].instrumentPreset = 2;   // pluck
            original.tracks[0].inserts[0] = { InsertType::compressor, false, -22.0f, 4.0f, 8.0f };
            original.tracks[0].inserts[1] = { InsertType::delay, true, 420.0f, 0.4f, 0.3f };
            original.tracks[0].steps[0] = { true, 1.0f };
            original.tracks[0].steps[1] = { false, 0.25f };

            Clip clip;
            clip.name = "Hook";
            clip.startStep = 8;
            clip.lengthSteps = 16;
            clip.notes.push_back ({ 0, 60, 0.9f });
            clip.notes.push_back ({ 4, 64, 0.55f });
            original.tracks[0].clips.push_back (clip);

            AudioClip audioClip;
            audioClip.name = "Vocal";
            audioClip.sourceFile = "media/vocal.wav";
            audioClip.startStep = 16;
            audioClip.lengthSteps = 8;
            audioClip.sourceStartSample = 44100;
            original.tracks[2].audioClips.push_back (audioClip);

            original.master.volumeDb = -2.0f;
            original.master.inserts[0] = { InsertType::eq, false, 3.0f, 2500.0f, -2.0f };
            original.buses[2].name = "Drum Bus";
            original.buses[2].volumeDb = -1.5f;

            const auto json = serializer::toJson (original);
            expect (json.isNotEmpty());

            ProjectState loaded;
            juce::String error;
            expect (serializer::fromJson (json, loaded, error), "Parsing should succeed: " + error);

            expectEquals (loaded.name, original.name);
            expectWithinAbsoluteError (loaded.tempo, original.tempo, 1.0e-9);
            expectEquals (loaded.patternLengthSteps, original.patternLengthSteps);
            expect (! loaded.loopEnabled);
            expectEquals ((int) loaded.tracks.size(), (int) original.tracks.size());

            const auto& t = loaded.tracks[0];
            expectEquals (t.name, juce::String ("Kick"));
            expectWithinAbsoluteError (t.volumeDb, -4.5f, 0.001f);
            expectWithinAbsoluteError (t.pan, -0.35f, 0.001f);
            expect (t.muted);
            expect (t.soloed);
            expectEquals (t.busIndex, 3);
            expectEquals (t.instrumentPreset, 2);

            expectEquals ((int) t.inserts[0].type, (int) InsertType::compressor);
            expectWithinAbsoluteError (t.inserts[0].p1, -22.0f, 0.001f);
            expectEquals ((int) t.inserts[1].type, (int) InsertType::delay);
            expect (t.inserts[1].bypassed);

            expect (t.steps[0].enabled);
            expect (! t.steps[1].enabled);
            expectWithinAbsoluteError (t.steps[1].velocity, 0.25f, 0.01f);

            expectEquals ((int) t.clips.size(), 1);
            expectEquals ((int) t.clips[0].notes.size(), 2);
            expectEquals (t.clips[0].notes[1].midiNote, 64);

            expectEquals ((int) loaded.tracks[2].audioClips.size(), 1);
            expectEquals (loaded.tracks[2].audioClips[0].sourceFile, juce::String ("media/vocal.wav"));

            expectWithinAbsoluteError (loaded.master.volumeDb, -2.0f, 0.001f);
            expectEquals ((int) loaded.master.inserts[0].type, (int) InsertType::eq);
            expectEquals (loaded.buses[2].name, juce::String ("Drum Bus"));
        }

        beginTest ("Broken JSON is rejected with an error");
        {
            ProjectState loaded;
            juce::String error;
            expect (! serializer::fromJson ("{ this is not json", loaded, error));
            expect (error.isNotEmpty());
        }

        beginTest ("Non-object documents are rejected");
        {
            ProjectState loaded;
            juce::String error;
            expect (! serializer::fromJson ("[1, 2, 3]", loaded, error));
            expect (error.isNotEmpty());
        }

        beginTest ("A newer schema is refused");
        {
            ProjectState loaded;
            juce::String error;
            const auto json = juce::String ("{\"format\":\"nux-project\",\"schema\":9999}");
            expect (! serializer::fromJson (json, loaded, error));
            expect (error.containsIgnoreCase ("newer"));
        }

        beginTest ("Missing optional fields fall back to defaults");
        {
            ProjectState loaded;
            juce::String error;
            expect (serializer::fromJson ("{}", loaded, error), error);
            expectWithinAbsoluteError (loaded.tempo, 120.0, 1.0e-9);
            expectEquals (loaded.patternLengthSteps, constants::defaultNumSteps);
            expect (loaded.tracks.size() >= 1, "At least one track should be created");
        }

        beginTest ("Out of range values are clamped, not rejected");
        {
            ProjectState loaded;
            juce::String error;
            const auto json = juce::String ("{\"tempo\": 9000, \"patternLength\": 400, \"tracks\": ["
                                            "{\"name\":\"X\",\"volumeDb\": 60, \"pan\": -9, \"numSteps\": 999}]}");
            expect (serializer::fromJson (json, loaded, error), error);

            expect (loaded.tempo <= constants::maxTempo + 0.001);
            expectEquals (loaded.patternLengthSteps, constants::maxNumSteps);
            expectWithinAbsoluteError (loaded.tracks[0].volumeDb, 6.0f, 0.001f);
            expectWithinAbsoluteError (loaded.tracks[0].pan, -1.0f, 0.001f);
            expectEquals (loaded.tracks[0].numSteps, constants::maxNumSteps);
        }

        beginTest ("Saving and loading a file round trips");
        {
            auto temporary = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("nux-test-project")
                                 .getChildFile ("roundtrip.nux");
            temporary.deleteFile();

            auto project = ProjectState::createDefault();
            project.name = "File Test";

            juce::String error;
            expect (serializer::saveToFile (project, temporary, error), error);
            expect (temporary.existsAsFile());

            ProjectState loaded;
            expect (serializer::loadFromFile (temporary, loaded, error), error);
            expectEquals (loaded.name, juce::String ("File Test"));

            // Loading a missing file must fail cleanly.
            ProjectState missing;
            expect (! serializer::loadFromFile (temporary.getSiblingFile ("nope.nux"), missing, error));
            expect (error.containsIgnoreCase ("not found"));

            temporary.deleteFile();
            temporary.getParentDirectory().deleteRecursively();
        }
        beginTest ("A plugin insert and hosted instrument survive a save and load");
        {
            auto project = ProjectState::createDefault();
            project.tracks[0].inserts[1].type = InsertType::plugin;
            project.tracks[0].inserts[1].bypassed = false;
            project.tracks[0].inserts[1].plugin.name = "Super Comp";
            project.tracks[0].inserts[1].plugin.manufacturer = "NUX Labs";
            project.tracks[0].inserts[1].plugin.format = "VST3";
            project.tracks[0].inserts[1].plugin.descriptionXml = "<PLUGIN name=\"Super Comp\" uniqueId=\"42\"/>";
            project.tracks[0].inserts[1].plugin.stateSaved = true;
            project.tracks[0].inserts[1].pluginStateBase64 = "c3RhdGU=";

            // Track 1 is a sampler track, which is where a hosted instrument belongs.
            project.tracks[1].instrumentPlugin.name = "Tone Generator";
            project.tracks[1].instrumentPlugin.format = "VST3";
            project.tracks[1].instrumentPlugin.isInstrument = true;
            project.tracks[1].instrumentPlugin.descriptionXml = "<PLUGIN name=\"Tone Generator\" isInstrument=\"1\"/>";
            project.tracks[1].instrumentStateBase64 = "aW5zdA==";

            project.master.inserts[3].type = InsertType::plugin;
            project.master.inserts[3].plugin.name = "Limiter XL";
            project.master.inserts[3].plugin.descriptionXml = "<PLUGIN name=\"Limiter XL\"/>";

            const auto text = serializer::toJson (project);

            ProjectState loaded;
            juce::String error;
            expect (serializer::fromJson (text, loaded, error), "The project failed to load: " + error);

            expect (loaded.tracks[0].inserts[1].type == InsertType::plugin,
                    "A plugin insert must keep its type");
            expectEquals (loaded.tracks[0].inserts[1].plugin.name, juce::String ("Super Comp"));
            expectEquals (loaded.tracks[0].inserts[1].plugin.format, juce::String ("VST3"));
            expectEquals (loaded.tracks[0].inserts[1].plugin.descriptionXml,
                          juce::String ("<PLUGIN name=\"Super Comp\" uniqueId=\"42\"/>"));
            expect (loaded.tracks[0].inserts[1].plugin.stateSaved, "The plugin state flag must survive");
            expectEquals (loaded.tracks[0].inserts[1].pluginStateBase64, juce::String ("c3RhdGU="));

            expect (loaded.tracks[1].usesPluginInstrument(), "A hosted instrument must survive a load");
            expectEquals (loaded.tracks[1].instrumentPlugin.name, juce::String ("Tone Generator"));
            expectEquals (loaded.tracks[1].instrumentStateBase64, juce::String ("aW5zdA=="));

            // An audio track is played by its clips, never by a VST3 instrument.
            expect (! loaded.tracks[2].usesPluginInstrument(),
                    "An audio track must not claim a hosted instrument");

            expect (loaded.master.inserts[3].type == InsertType::plugin, "A master plugin insert must survive");
            expectEquals (loaded.master.inserts[3].plugin.name, juce::String ("Limiter XL"));

            // Track 0 has no plugin anywhere: its inserts and instrument stay built-in.
            expect (loaded.tracks[0].inserts[0].type != InsertType::plugin,
                    "A track with built-in inserts must not gain a plugin");
            expect (! loaded.tracks[0].usesPluginInstrument(),
                    "A track without a hosted instrument must not report one");
        }
    }
};

static SerializerTests serializerTests;
}
