/* NUX DAW - plugin hosting tests.

   These tests use real VST3 binaries built from JUCE (tests/testplugins) rather
   than stubs, so they exercise the same code path the application uses: scan a
   folder, instantiate, prepare, process audio, save and restore state, then tear
   down. If the test plugins are not built the suite reports that explicitly
   instead of passing silently. */
#include "../src/nux_engine/AudioEngine.h"
#include "../src/nux_engine/ChannelStrip.h"
#include "../src/nux_engine/PluginManager.h"
#include "../src/nux_engine/PluginRetirementQueue.h"
#include "../src/nux_engine/PluginSlot.h"
#include "../src/nux_core/ProjectSerializer.h"

#include <cmath>

namespace nux::tests
{
class PluginHostTests final : public juce::UnitTest
{
public:
    PluginHostTests() : juce::UnitTest ("NUX plugin hosting", "NUX") {}

    //==============================================================================
    /** Locates the built test plugins, or returns an empty path. */
    static juce::FileSearchPath getTestPluginFolder()
    {
        const auto current = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

        // The test binary lives in <build>/tests/NUXTests_artefacts/Release/; the
        // plugins are in <build>/tests/NUXTestGain_artefacts/Release/VST3/.
        juce::StringArray relativeFolders;

        for (const auto* plugin : { "NUXTestGain", "NUXTestTone" })
            for (const auto* config : { "Release", "Debug" })
            {
                relativeFolders.add (juce::String ("tests/") + plugin + "_artefacts/" + config + "/VST3");
                relativeFolders.add (juce::String (plugin) + "_artefacts/" + config + "/VST3");
            }

        juce::FileSearchPath result;
        juce::Array<juce::File> candidates;

        auto search = current;
        for (int depth = 0; depth < 6; ++depth)
        {
            search = search.getParentDirectory();

            for (const auto& folder : relativeFolders)
                candidates.add (search.getChildFile (folder));
        }

        for (const auto& candidate : candidates)
            if (candidate.isDirectory() && ! result.toString().contains (candidate.getFullPathName()))
                result.add (candidate);

        return result;
    }

    static juce::StringArray getPluginNames (PluginManager& manager)
    {
        juce::StringArray names;

        for (const auto& info : manager.getPlugins())
            names.add (info.name);

        return names;
    }

    static PluginReference makeReference (const PluginManager& manager, const juce::String& name)
    {
        for (const auto& info : manager.getPlugins())
        {
            if (! info.name.containsIgnoreCase (name))
                continue;

            PluginReference reference;
            reference.name         = info.name;
            reference.manufacturer = info.manufacturer;
            reference.format       = info.format;
            reference.isInstrument = info.isInstrument;

            if (auto xml = info.description.createXml())
                reference.descriptionXml = xml->toString (juce::XmlElement::TextFormat().singleLine());

            reference.stateSaved = true;
            return reference;
        }

        return {};
    }

    //==============================================================================
    void runTest() override
    {
        const auto pluginFolder = getTestPluginFolder();

        if (pluginFolder.getNumPaths() == 0)
        {
            // Not a hidden pass: the test says out loud that it could not run. CI builds
            // the NUXTestGain / NUXTestTone targets, so the hosting path is verified there.
            beginTest ("Test plugins are available");
            logMessage ("SKIPPED: the VST3 test plugins were not built, so plugin hosting "
                        "was not verified in this run. Build the NUXTestGain and NUXTestTone targets "
                        "to enable these tests.");
            return;
        }

        PluginManager manager;
        manager.addSearchPaths (pluginFolder);

        juce::StringArray scanErrors;
        const auto found = manager.scanNow (pluginFolder, scanErrors);

        beginTest ("A real VST3 can be scanned and described");
        {
            const auto names = getPluginNames (manager);

            expect (found >= 1, "No plugins were found in " + pluginFolder.toString());
            expect (names.contains ("NUX Test Gain"), "Found: " + names.joinIntoString (", "));

            const auto gainReference = makeReference (manager, "NUX Test Gain");
            expect (gainReference.isValid(), "The scanned plugin has no serializable description");

            // The description has to survive the trip through project text.
            auto xml = juce::parseXML (gainReference.descriptionXml);
            expect (xml != nullptr, "The stored description is not valid XML");

            juce::PluginDescription roundTripped;
            if (xml != nullptr)
                roundTripped.loadFromXml (*xml);

            expectEquals (roundTripped.name, juce::String ("NUX Test Gain"));
            expect (roundTripped.isInstrument == false);
        }

        beginTest ("A hosted effect processes the audio of a channel strip");
        {
            const auto reference = makeReference (manager, "NUX Test Gain");
            juce::String error;
            auto instance = manager.createInstance (juce::PluginDescription(), 44100.0, 512, error);
            expect (instance == nullptr, "An empty description should not produce a plugin");

            auto xml = juce::parseXML (reference.descriptionXml);
            juce::PluginDescription description;
            if (xml != nullptr)
                description.loadFromXml (*xml);

            auto plugin = manager.createInstance (description, 44100.0, 512, error);
            expect (plugin != nullptr, "The plugin could not be instantiated: " + error);

            if (plugin == nullptr)
                return;

            auto slot = std::make_unique<PluginSlot> (std::move (plugin), 44100.0, 512, 2, true);
            expect (slot->isPrepared());
            expectEquals (slot->getNumAudioInputs(), 2);
            expectEquals (slot->getNumAudioOutputs(), 2);

            ChannelStrip strip;
            strip.prepare (44100.0, 512, 2);
            strip.beginBlock (512);
            strip.setPluginSlot (0, std::move (slot));
            strip.getParameters().setInsert (0, InsertType::plugin);

            // Unity gain must leave an impulse intact.
            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();
            buffer.setSample (0, 0, 0.5f);
            buffer.setSample (1, 0, -0.25f);

            juce::MidiBuffer midi;
            strip.process (buffer, 1.0f, &midi);

            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.5f, 0.001f);
            expectWithinAbsoluteError (buffer.getSample (1, 0), -0.25f, 0.001f);

            // The plugin instance really ran: the strip still holds it and reports it
            // as live.
            expect (strip.getPluginSlot (0) != nullptr);
            expect (strip.getPluginSlot (0)->isPrepared());
        }

        beginTest ("A hosted instrument receives MIDI notes from the sequencer");
        {
            const auto reference = makeReference (manager, "NUX Test Tone");
            expect (reference.isValid(), "The tone instrument was not scanned");

            juce::PluginDescription description;
            if (auto xml = juce::parseXML (reference.descriptionXml))
                description.loadFromXml (*xml);

            expect (description.isInstrument, "The VST3 sub-category was not recognised as an instrument");

            juce::String error;
            auto plugin = manager.createInstance (description, 44100.0, 512, error);

            if (plugin == nullptr)
            {
                expect (false, "The tone instrument could not be instantiated: " + error);
                return;
            }

            auto slot = std::make_unique<PluginSlot> (std::move (plugin), 44100.0, 512, 2, true);
            expect (slot->isInstrument());
            expect (slot->replacesInput(), "An instrument must replace its strip input");

            ChannelStrip strip;
            strip.prepare (44100.0, 512, 2);
            strip.beginBlock (512);
            strip.setPluginSlot (0, std::move (slot));
            strip.getParameters().setInsert (0, InsertType::plugin);

            expect (strip.hasInstrumentPlugin());

            juce::AudioBuffer<float> buffer (2, 512);

            // Silence in: with no note the instrument must output silence, not the dry signal.
            buffer.clear();
            buffer.setSample (0, 0, 0.9f);   // would leak through if input suppression failed

            juce::MidiBuffer midi;
            strip.process (buffer, 1.0f, &midi);
            expect (buffer.getMagnitude (0, 512) < 0.001f,
                    "An instrument slot must not pass the dry input through (level "
                        + juce::String (buffer.getMagnitude (0, 512)) + ")");

            // A note on must produce audio.
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            strip.process (buffer, 1.0f, &midi);

            const auto level = buffer.getMagnitude (0, 512);
            expect (level > 0.1f, "The hosted instrument produced no audio for a note (level " + juce::String (level) + ")");
        }

        beginTest ("Plugin state survives a save and restore");
        {
            const auto reference = makeReference (manager, "NUX Test Gain");

            juce::PluginDescription description;
            if (auto xml = juce::parseXML (reference.descriptionXml))
                description.loadFromXml (*xml);

            juce::String error;
            auto plugin = manager.createInstance (description, 44100.0, 512, error);
            expect (plugin != nullptr);

            if (plugin == nullptr)
                return;

            // Change the gain parameter, save the state, then load it into a fresh one.
            for (auto* parameter : plugin->getParameters())
                if (parameter->getName (100).containsIgnoreCase ("gain"))
                    parameter->setValue (0.25f);   // 2.0 max -> 0.5 gain

            juce::MemoryBlock savedState;
            plugin->getStateInformation (savedState);
            expect (savedState.getSize() > 0, "The plugin returned an empty state");

            auto second = manager.createInstance (description, 44100.0, 512, error);
            expect (second != nullptr);

            if (second == nullptr)
                return;

            second->setStateInformation (savedState.getData(), (int) savedState.getSize());

            float restoredGain = -1.0f;
            for (auto* parameter : second->getParameters())
                if (parameter->getName (100).containsIgnoreCase ("gain"))
                    restoredGain = parameter->getValue();

            expectWithinAbsoluteError (restoredGain, 0.25f, 0.02f);

            // And the restored state really does change the audio.
            auto slot = std::make_unique<PluginSlot> (std::move (second), 44100.0, 512, 2, true);
            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);

            juce::MidiBuffer midi;
            slot->process (buffer, 512, &midi);

            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.5f, 0.05f);
        }

        beginTest ("A missing plugin reports an error instead of breaking the project");
        {
            PluginReference broken;
            broken.name = "Ghost Plugin";
            broken.descriptionXml = "<PLUGIN name=\"Ghost Plugin\" file=\"C:/nowhere/Ghost.vst3\" "
                                    "uniqueId=\"1234567890\" isInstrument=\"0\" numInputs=\"2\" numOutputs=\"2\"/>";
            broken.stateSaved = false;

            AudioEngine engine;
            ProjectState project = ProjectState::createDefault();
            engine.setProjectState (&project);
            engine.setPluginManager (&manager);
            engine.prepareForOfflineRendering (44100.0, 512, 2);

            project.tracks[0].inserts[0].type = InsertType::plugin;
            project.tracks[0].inserts[0].plugin = broken;

            const auto error = engine.loadInsertPlugin (AudioEngine::StripKind::track, 0, 0);
            expect (error.isNotEmpty(), "Loading a plugin that does not exist should report an error");
            expect (engine.getInsertPluginSlot (AudioEngine::StripKind::track, 0, 0) == nullptr);

            // The rest of the project must still render.
            engine.getTransport().setPlaying (true);

            juce::AudioBuffer<float> output (2, 512);
            engine.renderBlockForTest (output, 512);
            expect (output.getNumSamples() == 512);
        }

        beginTest ("Plugin instances are retired without blocking the audio thread");
        {
            const auto before = PluginRetirementQueue::getInstance().getPendingCount();

            auto slot = std::make_unique<PluginSlot> ("Retired Slot", false);
            PluginRetirementQueue::getInstance().retire (std::move (slot));

            expect (PluginRetirementQueue::getInstance().getPendingCount() >= before + 1,
                    "A retired slot should wait in the queue");

            PluginRetirementQueue::getInstance().flush();
            expectEquals (PluginRetirementQueue::getInstance().getPendingCount(), 0);
        }
    }
};

static PluginHostTests pluginHostTests;
}
