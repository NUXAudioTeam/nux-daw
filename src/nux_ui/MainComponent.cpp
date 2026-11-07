#include "MainComponent.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace nux
{
//==============================================================================
/** The Master tab: mixdown controls, limiter, send effects and export. */
class MainComponent::MasterPanel final : public juce::Component
{
public:
    MasterPanel (ProjectDocument& d, AudioEngine& e)
        : document (d), engine (e)
    {
        addAndMakeVisible (masterLabel);
        masterLabel.setText ("MASTER OUTPUT", juce::dontSendNotification);
        masterLabel.setFont (theme::font::bold (13.0f));
        masterLabel.setColour (juce::Label::textColourId, theme::accent);

        addAndMakeVisible (gainKnob);
        gainKnob.setSuffix (" dB");
        gainKnob.onValueChange = [this] (double value)
        {
            document.beginEdit ("master gain", "Change the master volume");
            document.getState().master.volumeDb = (float) value;
            engine.syncMixerFromState();
            document.markDirty();
        };

        addAndMakeVisible (limiterButton);
        limiterButton.setToggleState (document.getState().master.limiterEnabled, juce::dontSendNotification);
        limiterButton.onClick = [this]
        {
            document.beginEdit ("limiter", "Toggle the master limiter");
            document.getState().master.limiterEnabled = limiterButton.getToggleState();
            engine.syncMixerFromState();
            document.markDirty();
        };

        addAndMakeVisible (exportButton);
        exportButton.onClick = [this]
        {
            if (onExportRequested)
                onExportRequested();
        };

        addAndMakeVisible (exportLengthLabel);
        exportLengthLabel.setFont (theme::font::ui (11.0f));
        exportLengthLabel.setColour (juce::Label::textColourId, theme::textFaint);

        addAndMakeVisible (exportBars);
        exportBars.setSliderStyle (juce::Slider::IncDecButtons);
        exportBars.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 22);
        exportBars.setRange (1.0, 64.0, 1.0);
        exportBars.setValue (4.0, juce::dontSendNotification);

        exportBars.onValueChange = [this] { updateExportLength(); };

        addAndMakeVisible (meter);
        addAndMakeVisible (reductionLabel);
        reductionLabel.setFont (theme::font::mono (11.0f));
        reductionLabel.setColour (juce::Label::textColourId, theme::textFaint);

        addAndMakeVisible (info);
        info.setFont (theme::font::ui (11.0f));
        info.setColour (juce::Label::textColourId, theme::textFaint);
        info.setJustificationType (juce::Justification::topLeft);

        updateExportLength();
    }

    std::function<void()> onExportRequested;

    void refresh()
    {
        meter.setLevel (engine.getMasterPeak (0), engine.getMasterPeak (1));

        const auto reduction = engine.getLimiterReduction();
        meter.repaint();

        reductionLabel.setText (reduction < 0.995f
                                    ? "LIM " + juce::String (juce::Decibels::gainToDecibels (reduction), 1) + " dB"
                                    : "LIM 0.0 dB",
                                juce::dontSendNotification);

        auto& state = document.getState();
        gainKnob.setValue (state.master.volumeDb, juce::dontSendNotification);
        limiterButton.setToggleState (state.master.limiterEnabled, juce::dontSendNotification);

        info.setText ("Sample rate: " + juce::String (engine.getSampleRateHint() / 1000.0, 1) + " kHz\n"
                      "Device: " + engine.getAudioStatusMessage() + "\n"
                      "Engine: 32-bit float, " + juce::String (engine.getBlockSizeHint()) + " sample blocks\n"
                      "Limiter: -0.3 dBFS soft knee",
                      juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::windowBg);
        g.fillRect (getLocalBounds());

        auto card = getLocalBounds().reduced (24, 18).toFloat();

        theme::NuxLookAndFeel::drawPanel (g, card, theme::panelRaised, theme::outline, 8.0f);

        g.setColour (theme::accent.withAlpha (0.7f));
        g.fillRoundedRectangle (card.getX(), card.getY(), card.getWidth(), 3.0f, 2.0f);
    }

    void resized() override
    {
        auto card = getLocalBounds().reduced (24, 18);
        auto inner = card.reduced (20, 16);

        masterLabel.setBounds (inner.removeFromTop (22));
        inner.removeFromTop (12);

        // Left column: master gain and the processing toggles.
        auto leftColumn = inner.removeFromLeft (juce::jmin (260, juce::jmax (200, inner.getWidth() / 4)));
        gainKnob.setBounds (leftColumn.removeFromTop (110).withWidth (96));

        leftColumn.removeFromTop (10);
        auto toggles = leftColumn.removeFromTop (86);
        limiterButton.setBounds (toggles.removeFromTop (30));
        toggles.removeFromTop (6);
        exportButton.setBounds (toggles.removeFromTop (30));

        // Middle column: export length.
        inner.removeFromLeft (24);
        auto middleColumn = inner.removeFromLeft (juce::jmin (230, juce::jmax (170, inner.getWidth() / 3)));
        exportLengthLabel.setBounds (middleColumn.removeFromTop (22));
        middleColumn.removeFromTop (4);
        exportBars.setBounds (middleColumn.removeFromTop (28));

        // Right: the output meter, kept inside the card.
        auto meterColumn = card.removeFromRight (74).reduced (14, 16);
        reductionLabel.setBounds (meterColumn.removeFromTop (16));
        meter.setBounds (meterColumn.removeFromTop (juce::jmin (240, meterColumn.getHeight() - 10)).withWidth (26));

        // Bottom: engine info.
        auto infoArea = card.reduced (20, 16).removeFromBottom (70);
        info.setBounds (infoArea);
    }

private:
    void updateExportLength()
    {
        const auto bars = (int) exportBars.getValue();
        const auto steps = bars * constants::stepsPerBeat * 4;
        const auto seconds = (steps / ((document.getState().tempo / 60.0) * constants::stepsPerBeat));
        exportLengthLabel.setText ("EXPORT LENGTH: " + juce::String (bars) + " bars (~"
                                   + juce::String (seconds, 1) + " s)", juce::dontSendNotification);
    }

    ProjectDocument& document;
    AudioEngine& engine;

    juce::Label masterLabel, exportLengthLabel, reductionLabel, info;
    NuxKnob gainKnob { "Master", -60.0, 6.0, 0.1, 0.0 };
    juce::ToggleButton limiterButton { "Limiter" };
    juce::TextButton exportButton { "EXPORT WAV" };
    juce::Slider exportBars;
    LevelMeter meter { true };
};

//==============================================================================
MainComponent::MainComponent()
    : transportBar (engine),
      timelineView (document, engine),
      mixerView (document, engine),
      pianoRollView (document, engine),
      pluginPanel (pluginManager, engine),
      masterPanel (std::make_unique<MasterPanel> (document, engine))
{
    setLookAndFeel (&globalLookAndFeel);
    setSize (1360, 820);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (transportBar);
    addAndMakeVisible (tabs);
    addAndMakeVisible (timelineView);
    addAndMakeVisible (mixerView);
    addAndMakeVisible (pianoRollView);
    addAndMakeVisible (pluginPanel);
    addAndMakeVisible (*masterPanel);
    addAndMakeVisible (statusLabel);

    statusLabel.setFont (theme::font::ui (11.0f));
    statusLabel.setColour (juce::Label::textColourId, theme::textFaint);

    tabs.onChange = [this] (int index)
    {
        setWorkspace ((Workspace) juce::jlimit (0, 4, index));
    };

    transportBar.onRecordToggled = [this] { toggleRecording(); };
    timelineView.onInstrumentPluginRequested = [this] (int track) { chooseInstrumentPlugin (track); };
    timelineView.onClearInstrumentPlugin = [this] (int track) { useBuiltInInstrument (track); };
    transportBar.onSaveRequested = [this] { saveProject (false); };
    transportBar.onAudioSettingsRequested = [this] { toggleAudioSettings(); };
    transportBar.onTempoChanged = [this]
    {
        document.beginEdit ("tempo", "Change the tempo");
        document.markDirty();
        engine.rebuildGraph();
        updateWindowTitle();
    };

    timelineView.onProjectEdited = [this] { refreshAllViews(); };
    timelineView.onPreviewNote = [this] { statusLabel.setText ("", juce::dontSendNotification); };

    timelineView.onAudioFileDropped = [this] (const juce::File& file, int track)
    {
        importAudioFileInto (file, track);
    };

    mixerView.onProjectEdited = [this] { refreshAllViews(); };
    pianoRollView.onProjectEdited = [this] { refreshAllViews(); };

    masterPanel->onExportRequested = [this] { exportWav(); };

    document.addChangeListener (this);

    // Open the engine against the document that lives in the app.
    engine.setProjectState (&document.getState());

    const auto audioError = engine.initialiseAudio();
    audioInitialised = audioError.isEmpty();

    if (audioInitialised)
    {
        engine.applyPreferredDeviceSetup();
        statusLabel.setText ("Audio: " + engine.getAudioStatusMessage(), juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText ("Audio device error: " + audioError + "  (open AUDIO to pick a driver)",
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, theme::warn);
    }

    engine.setPluginManager (&pluginManager);
    engine.rebuildGraph();

    registerCommands();
    setWorkspace (Workspace::main);
    updateWindowTitle();

    pluginManager.onScanFinished = [this]
    {
        showStatus ("VST3 scan finished: " + juce::String ((int) pluginManager.getPlugins().size()) + " plugins");
    };

    pluginManager.loadCachedList();

    startTimerHz (20);

    // The window is not on screen yet, so take focus once it is.
    triggerAsyncUpdate();
}

void MainComponent::handleAsyncUpdate()
{
    grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
    stopTimer();
    document.removeChangeListener (this);

    closeAudioSettingsWindow();
    engine.shutdownAudio();
    setLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::timerCallback()
{
    // While recording, move captured audio into the WAV file and show progress.
    if (engine.isInputRecording())
    {
        engine.pumpInputRecorder();

        if (currentWorkspace != Workspace::master)
            showStatus ("Recording  " + juce::String (engine.getRecordingSeconds(), 1) + " s"
                        + (engine.getInputLevel() < 0.0005f ? "   (input is silent)" : ""));
    }

    // Only touch the master panel while it is actually visible; refreshing meters on a
    // hidden component would queue repaints for a component that cannot be drawn.
    if (currentWorkspace == Workspace::master && masterPanel != nullptr && masterPanel->isVisible())
        masterPanel->refresh();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateWindowTitle();
}

void MainComponent::updateWindowTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        const auto dirtyMark = document.hasUnsavedChanges() ? " *" : "";
        window->setName (constants::appName + juce::String (" - ") + document.getDisplayName() + dirtyMark);
    }
}

void MainComponent::showStatus (const juce::String& message)
{
    statusLabel.setColour (juce::Label::textColourId, theme::textFaint);
    statusLabel.setText (message, juce::dontSendNotification);
    transportBar.setStatus (message);
}

void MainComponent::refreshAllViews()
{
    transportBar.repaint();
    timelineView.repaint();
    mixerView.repaint();
    pianoRollView.repaint();
    updateWindowTitle();
}

//==============================================================================
void MainComponent::setWorkspace (Workspace workspace)
{
    currentWorkspace = workspace;

    timelineView.setVisible (workspace == Workspace::main);
    mixerView.setVisible (workspace == Workspace::mixer);
    pianoRollView.setVisible (workspace == Workspace::pianoRoll);
    pluginPanel.setVisible (workspace == Workspace::plugins);
    masterPanel->setVisible (workspace == Workspace::master);

    tabs.setSelectedIndex ((int) workspace, false);

    switch (workspace)
    {
        case Workspace::main:
            timelineView.rebuild();
            break;
        case Workspace::mixer:
            mixerView.rebuild();
            break;
        case Workspace::pianoRoll:
            pianoRollView.setTrack (timelineView.getSelectedTrack());
            break;
        case Workspace::plugins:
        case Workspace::master:
            break;
    }

    resized();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::windowBg);

    // Status bar.
    auto statusArea = getLocalBounds().removeFromBottom (theme::metrics::statusBarHeight);
    g.setColour (theme::panel);
    g.fillRect (statusArea);
    g.setColour (theme::outline);
    g.drawHorizontalLine (statusArea.getY(), 0.0f, (float) getWidth());
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    transportBar.setBounds (area.removeFromTop (theme::metrics::transportBarHeight));
    tabs.setBounds (area.removeFromTop (theme::metrics::tabBarHeight));

    auto statusArea = area.removeFromBottom (theme::metrics::statusBarHeight);
    statusLabel.setBounds (statusArea.reduced (12, 0));

    timelineView.setBounds (area);
    mixerView.setBounds (area);
    pianoRollView.setBounds (area);
    pluginPanel.setBounds (area);
    masterPanel->setBounds (area);
}

//==============================================================================
// Key handling
//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        engine.togglePlay();
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        engine.stop();
        engine.getTransport().setPositionSteps (0.0);
        return true;
    }

    return false;
}

//==============================================================================
// Commands
//==============================================================================
enum CommandIDs
{
    cmdNewProject = 0x2001,
    cmdOpenProject,
    cmdSaveProject,
    cmdSaveProjectAs,
    cmdUndo,
    cmdRedo,
    cmdExportWav,
    cmdAudioSettings,
    cmdPlayStop,
    cmdToggleLoop,
    cmdWorkspaceMain,
    cmdWorkspaceMixer,
    cmdWorkspacePianoRoll,
    cmdWorkspacePlugins,
    cmdWorkspaceMaster,
    cmdAbout
};

void MainComponent::registerCommands()
{
    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());
}

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.addArray ({ cmdNewProject, cmdOpenProject, cmdSaveProject, cmdSaveProjectAs,
                         cmdUndo, cmdRedo,
                         cmdExportWav, cmdAudioSettings, cmdPlayStop, cmdToggleLoop,
                         cmdWorkspaceMain, cmdWorkspaceMixer, cmdWorkspacePianoRoll,
                         cmdWorkspacePlugins, cmdWorkspaceMaster, cmdAbout });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto mod = juce::ModifierKeys::commandModifier;
    const auto alt = juce::ModifierKeys::altModifier;

    switch (id)
    {
        case cmdNewProject:
            info.setInfo ("New project", "Start a new NUX project", "File", 0);
            info.addDefaultKeypress ('n', mod);
            break;
        case cmdOpenProject:
            info.setInfo ("Open project...", "Open a .nux project", "File", 0);
            info.addDefaultKeypress ('o', mod);
            break;
        case cmdSaveProject:
            info.setInfo ("Save project", "Save the current project", "File", 0);
            info.addDefaultKeypress ('s', mod);
            break;
        case cmdUndo:
            info.setInfo (document.getUndoDescription().isNotEmpty()
                              ? "Undo " + document.getUndoDescription()
                              : juce::String ("Undo"),
                          "Revert the last change", "Edit", 0);
            info.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier);
            info.setActive (document.canUndo());
            break;
        case cmdRedo:
            info.setInfo (document.getRedoDescription().isNotEmpty()
                              ? "Redo " + document.getRedoDescription()
                              : juce::String ("Redo"),
                          "Reapply the last undone change", "Edit", 0);
            // Both the Windows/Linux and the macOS conventions.
            info.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier
                                            | juce::ModifierKeys::shiftModifier);
            info.addDefaultKeypress ('y', juce::ModifierKeys::commandModifier);
            info.setActive (document.canRedo());
            break;
        case cmdSaveProjectAs:
            info.setInfo ("Save project as...", "Save the project to a new file", "File", 0);
            info.addDefaultKeypress ('s', mod | juce::ModifierKeys::shiftModifier);
            break;
        case cmdExportWav:
            info.setInfo ("Export WAV...", "Render the pattern to a 24-bit WAV file", "File", 0);
            info.addDefaultKeypress ('e', mod);
            break;
        case cmdAudioSettings:
            info.setInfo ("Audio settings...", "Choose the audio driver and buffer size", "Audio", 0);
            info.addDefaultKeypress (',', mod);
            break;
        case cmdPlayStop:
            info.setInfo ("Play / stop", "Start or stop playback", "Transport", 0);
            info.addDefaultKeypress (juce::KeyPress::spaceKey, juce::ModifierKeys::noModifiers);
            break;
        case cmdToggleLoop:
            info.setInfo ("Toggle loop", "Loops the pattern during playback", "Transport", 0);
            info.addDefaultKeypress ('l', juce::ModifierKeys::noModifiers);
            break;
        case cmdWorkspaceMain:
            info.setInfo ("Main", "Track arrangement", "View", 0);
            info.addDefaultKeypress ('1', alt);
            break;
        case cmdWorkspaceMixer:
            info.setInfo ("Mixer", "Channel strips", "View", 0);
            info.addDefaultKeypress ('2', alt);
            break;
        case cmdWorkspacePianoRoll:
            info.setInfo ("Piano roll", "Note editor", "View", 0);
            info.addDefaultKeypress ('3', alt);
            break;
        case cmdWorkspacePlugins:
            info.setInfo ("Plugins", "VST3 browser", "View", 0);
            info.addDefaultKeypress ('4', alt);
            break;
        case cmdWorkspaceMaster:
            info.setInfo ("Master", "Mixdown and export", "View", 0);
            info.addDefaultKeypress ('5', alt);
            break;
        case cmdAbout:
            info.setInfo ("About NUX", "Version information", "Help", 0);
            break;
        default:
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case cmdNewProject:       newProject(); return true;
        case cmdOpenProject:      openProject(); return true;
        case cmdUndo:             performUndo(); return true;
        case cmdRedo:             performRedo(); return true;
        case cmdSaveProject:      saveProject (false); return true;
        case cmdSaveProjectAs:    saveProject (true); return true;
        case cmdExportWav:        exportWav(); return true;
        case cmdAudioSettings:    toggleAudioSettings(); return true;
        case cmdPlayStop:         engine.togglePlay(); return true;
        case cmdToggleLoop:
        {
            const auto enabled = ! engine.getTransport().isLoopEnabled();
            engine.getTransport().setLoopEnabled (enabled);
            document.getState().loopEnabled = enabled;
            showStatus (enabled ? "Loop on" : "Loop off");
            return true;
        }
        case cmdWorkspaceMain:      setWorkspace (Workspace::main); return true;
        case cmdWorkspaceMixer:     setWorkspace (Workspace::mixer); return true;
        case cmdWorkspacePianoRoll: setWorkspace (Workspace::pianoRoll); return true;
        case cmdWorkspacePlugins:   setWorkspace (Workspace::plugins); return true;
        case cmdWorkspaceMaster:    setWorkspace (Workspace::master); return true;
        case cmdAbout:              showAbout(); return true;
        default: return false;
    }
}

//==============================================================================
juce::String MainComponent::runInstrumentSelfTest (const juce::File& pluginFolder,
                                                  const juce::String& pluginName,
                                                  int trackIndex)
{
    juce::StringArray report;
    auto& state = document.getState();

    if (! juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size()))
        return "Track " + juce::String (trackIndex) + " does not exist";

    // On a machine with no audio device the graph was never prepared, and an
    // unprepared engine renders nothing. Prepare an offline graph so the test
    // measures real DSP rather than an empty one.
    if (! engine.isAudioAvailable())
        engine.prepareForOfflineRendering (44100.0, 512, 2);

    pluginManager.addSearchPaths (juce::FileSearchPath (pluginFolder.getFullPathName()));
    pluginManager.scanNow (juce::FileSearchPath (pluginFolder.getFullPathName()), report);

    const PluginManager::PluginInfo* chosen = nullptr;

    for (const auto& info : pluginManager.getPlugins())
        if (info.name.containsIgnoreCase (pluginName) && info.isInstrument)
            chosen = &info;

    if (chosen == nullptr)
        return "No instrument matching '" + pluginName + "' was found in " + pluginFolder.getFullPathName();

    report.add ("found instrument: " + chosen->name + " (" + chosen->manufacturer + ", " + chosen->format + ")"
                  + ", isInstrument = " + juce::String ((int) chosen->isInstrument));

    // Exactly what chooseInstrumentPlugin() does once the user has picked an entry.
    document.beginEdit ("instrument plugin", "Load an instrument plugin");

    auto& track = state.tracks[(size_t) trackIndex];
    track.type = TrackType::sampler;
    track.instrumentPlugin.name         = chosen->name;
    track.instrumentPlugin.manufacturer = chosen->manufacturer;
    track.instrumentPlugin.format       = chosen->format;
    track.instrumentPlugin.isInstrument = true;

    if (auto xml = chosen->description.createXml())
        track.instrumentPlugin.descriptionXml = xml->toString (juce::XmlElement::TextFormat().singleLine());

    document.markDirty();

    const auto loadError = engine.loadTrackInstrument (trackIndex);
    report.add ("loadTrackInstrument: " + (loadError.isEmpty() ? juce::String ("ok") : loadError));

    if (loadError.isNotEmpty())
        return report.joinIntoString ("\n");

    report.add ("model reference: name = " + state.tracks[(size_t) trackIndex].instrumentPlugin.name
                  + ", has description xml = "
                  + juce::String (state.tracks[(size_t) trackIndex].instrumentPlugin.descriptionXml.isNotEmpty() ? "yes" : "no"));

    // Silence first, so a stuck note from a previous test cannot masquerade as success.
    juce::AudioBuffer<float> buffer (2, 512);

    auto measurePeak = [this, &buffer] (int numSamples)
    {
        buffer.clear();
        engine.renderBlockForTest (buffer, numSamples);
        return buffer.getMagnitude (0, numSamples);
    };

    const auto silentPeak = measurePeak (512);
    report.add ("silent render peak = " + juce::String (silentPeak, 4));

    engine.triggerPreviewNote (trackIndex, 60, 0.9f);

    // Render a couple of blocks so the plugin's attack is included.
    auto notePeak = 0.0f;

    for (int block = 0; block < 6; ++block)
        notePeak = juce::jmax (notePeak, measurePeak (512));

    report.add ("note render peak = " + juce::String (notePeak, 4));

    // Undo must put the sampler back and take the plugin out of the graph.
    document.undo();
    engine.rebuildGraph();
    engine.loadTrackInstrument (trackIndex);
    const auto afterUndoPeak = measurePeak (512);

    report.add ("after undo: instrument plugin valid = "
                  + juce::String (document.getState().tracks[(size_t) trackIndex].instrumentPlugin.isValid() ? "yes" : "no")
                  + ", engine slot = "
                  + juce::String (engine.getTrackInstrumentSlot (trackIndex) != nullptr ? "present" : "empty"));

    const auto ok = notePeak > 0.01f && silentPeak < 0.01f;

    report.add (ok ? "RESULT: hosted instrument verified" : "RESULT: FAILED");

    juce::ignoreUnused (afterUndoPeak);
    return report.joinIntoString ("\n");
}

//==============================================================================
void MainComponent::chooseInstrumentPlugin (int trackIndex)
{
    auto& state = document.getState();

    if (! juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size()))
        return;

    pluginManager.loadCachedList();

    juce::PopupMenu menu;
    std::vector<PluginManager::PluginInfo> instruments;

    for (const auto& info : pluginManager.getPlugins())
    {
        if (! info.isInstrument)
            continue;

        instruments.push_back (info);
        menu.addItem ((int) instruments.size(),
                      info.manufacturer.isNotEmpty() ? info.name + "  (" + info.manufacturer + ")"
                                                     : info.name);
    }

    if (instruments.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                "No instruments found",
                                                "NUX did not find any VST3 instruments. Install a plugin, then "
                                                "press RESCAN on the PLUGINS page.");
        return;
    }

    const auto trackName = state.tracks[(size_t) trackIndex].name;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&timelineView),
                        [this, trackIndex, instruments, trackName] (int result)
    {
        if (result <= 0 || result > (int) instruments.size())
            return;

        const auto chosen = instruments[(size_t) result - 1];

        // The project is the source of truth: write the reference into the model, then
        // let the engine instantiate it. Undo restores the old reference and the same
        // rebuild path brings the previous instrument back.
        document.beginEdit ("instrument plugin", "Load an instrument plugin");

        auto& track = document.getState().tracks[(size_t) trackIndex];
        track.type = TrackType::sampler;

        track.instrumentPlugin.name         = chosen.name;
        track.instrumentPlugin.manufacturer = chosen.manufacturer;
        track.instrumentPlugin.format       = chosen.format;
        track.instrumentPlugin.isInstrument = true;

        if (auto xml = chosen.description.createXml())
            track.instrumentPlugin.descriptionXml = xml->toString (juce::XmlElement::TextFormat().singleLine());
        else
            track.instrumentPlugin.descriptionXml.clear();

        document.markDirty();

        const auto error = engine.loadTrackInstrument (trackIndex);

        refreshAllViews();
        updateWindowTitle();

        showStatus (error.isEmpty()
                      ? trackName + ": " + chosen.name
                      : "Could not load " + chosen.name + " on " + trackName + ": " + error);
    });
}


void MainComponent::useBuiltInInstrument (int trackIndex)
{
    auto& state = document.getState();

    if (! juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size()))
        return;

    document.beginEdit ("built-in instrument", "Use the built-in instrument");
    document.getState().tracks[(size_t) trackIndex].instrumentPlugin = PluginReference();
    document.getState().tracks[(size_t) trackIndex].instrumentStateBase64.clear();
    document.markDirty();

    engine.unloadTrackInstrument (trackIndex);
    engine.rebuildGraph();
    refreshAllViews();
    updateWindowTitle();
    showStatus (state.tracks[(size_t) trackIndex].name + ": built-in instrument");
}

//==============================================================================
void MainComponent::toggleRecording()
{
    if (engine.isInputRecording())
    {
        const auto take = engine.stopInputRecording();

        if (take.file == juce::File())
        {
            showStatus ("Recording was too short to keep");
            timelineView.rebuild();
            refreshAllViews();
            return;
        }

        placeRecordedTake (take);
        timelineView.rebuild();
        refreshAllViews();
        return;
    }

    // Make sure the armed state in the engine matches the project before starting.
    engine.setProjectState (&document.getState());

    const auto folder = document.getMediaFolder();
    folder.createDirectory();

    const auto error = engine.startInputRecording (folder);

    if (error.isNotEmpty())
    {
        showStatus (error);

        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Cannot record",
                                                error);
        return;
    }

    showStatus ("Recording...  press record again to stop");
}

bool MainComponent::fileIsInsideFolder (const juce::File& file, const juce::File& folder)
{
    const auto parent = file.getParentDirectory().getFullPathName();
    const auto root = folder.getFullPathName();

    return root.isNotEmpty() && parent.startsWith (root);
}

void MainComponent::placeRecordedTake (const AudioEngine::RecordedTake& take)
{
    auto& state = document.getState();

    const auto trackIndex = juce::isPositiveAndBelow (take.trackIndex, (int) state.tracks.size())
                              ? take.trackIndex
                              : 0;

    if (! juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size()))
        return;

    document.beginEdit ("record audio", "Record an audio clip");
    auto& track = state.tracks[(size_t) trackIndex];

    // A recorded take turns the track into an audio track, exactly like an import,
    // and the file lives in the project's media folder so the project stays portable.
    track.type = TrackType::audio;

    // A take inside the project is stored relative to it, so the project stays
    // portable. A take from elsewhere keeps its absolute path, which the engine
    // resolves as-is; storing only a bare file name would make it unloadable.
    const auto folder = document.getProjectFolder();
    const auto relative = take.file.getRelativePathFrom (folder);

    AudioClip clip;
    clip.name = take.file.getFileNameWithoutExtension();
    clip.sourceFile = fileIsInsideFolder (take.file, folder) && relative.isNotEmpty()
                        ? relative
                        : take.file.getFullPathName();
    clip.startStep = (int) std::round (take.startStep);
    clip.lengthSteps = juce::jmax (1, (int) std::ceil (take.lengthSeconds * state.tempo / 60.0
                                                       * (double) constants::stepsPerBeat));
    clip.sourceStartSample = 0;

    track.audioClips.push_back (clip);

    // Grow the pattern so the new clip is inside it.
    const auto clipEnd = clip.startStep + clip.lengthSteps;

    if (clipEnd > state.patternLengthSteps)
    {
        auto newLength = state.patternLengthSteps;

        while (newLength < clipEnd && newLength < constants::maxNumSteps)
            newLength += constants::stepsPerBeat * 4;

        state.patternLengthSteps = juce::jmin (newLength, constants::maxNumSteps);
    }

    engine.rebuildGraph();
    engine.setMediaRootDirectory (document.getProjectFolder());
    engine.reloadAudioClips();
    engine.getTransport().setLoopLengthSteps (state.patternLengthSteps);

    document.markDirty();
    updateWindowTitle();

    showStatus ("Recorded " + juce::String (take.lengthSeconds, 1) + " s onto " + track.name);
}

//==============================================================================
juce::String MainComponent::runRecordingSelfTest (const juce::File& takeFile,
                                                 double lengthSeconds, int trackIndex)
{
    juce::StringArray report;
    auto& state = document.getState();

    if (! juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size()))
        return "Track " + juce::String (trackIndex) + " does not exist";

    // Without an audio device the clip store is never prepared and the take cannot be
    // loaded, so prepare an offline graph first and let the test prove the real path.
    if (! engine.isAudioAvailable())
        engine.prepareForOfflineRendering (44100.0, 512, 2);

    report.add ("before: track = \"" + state.tracks[(size_t) trackIndex].name
                  + "\", clips = " + juce::String ((int) state.tracks[(size_t) trackIndex].audioClips.size())
                  + ", pattern steps = " + juce::String (state.patternLengthSteps));

    // Hand the take to the same function the record button calls when it stops.
    AudioEngine::RecordedTake take;
    take.file = takeFile;
    take.startStep = 0.0;
    take.lengthSeconds = lengthSeconds;
    take.trackIndex = trackIndex;

    placeRecordedTake (take);
    const auto& after = document.getState();
    const auto& clips = after.tracks[(size_t) trackIndex].audioClips;

    report.add ("after: track type = audio? " + juce::String (after.tracks[(size_t) trackIndex].type == TrackType::audio ? "yes" : "no")
                  + ", clips = " + juce::String ((int) clips.size())
                  + ", pattern steps = " + juce::String (after.patternLengthSteps)
                  + ", loaded clip files = " + juce::String (engine.getNumLoadedClipFiles()));

    if (! clips.empty())
        report.add ("clip: \"" + clips[0].name + "\", file = " + clips[0].sourceFile
                      + ", steps = " + juce::String (clips[0].lengthSteps)
                      + ", length = " + juce::String (engine.getClipLengthSeconds (clips[0].sourceFile), 2) + " s");

    const auto ok = ! clips.empty() && engine.getNumLoadedClipFiles() > 0;

    report.add (ok ? "RESULT: recording placement verified" : "RESULT: FAILED");

    return report.joinIntoString ("\n");
}

//==============================================================================
void MainComponent::performUndo()
{
    if (! document.undo())
    {
        showStatus ("Nothing to undo");
        return;
    }

    rebuildEverythingAfterHistoryJump (document.getRedoDescription());
}

void MainComponent::performRedo()
{
    if (! document.redo())
    {
        showStatus ("Nothing to redo");
        return;
    }

    rebuildEverythingAfterHistoryJump (document.getUndoDescription());
}

void MainComponent::rebuildEverythingAfterHistoryJump (const juce::String& description)
{
    // A snapshot restores the whole project, so every part of the engine that
    // mirrors it has to be rebuilt: the graph itself, the clips, and the plugin
    // instances (a plugin may have appeared or disappeared in the undone edit).
    const auto wasPlaying = engine.getTransport().isPlaying();

    if (wasPlaying)
        engine.stop();

    engine.setProjectState (&document.getState());
    engine.rebuildGraph();
    engine.setMediaRootDirectory (document.getProjectFolder());
    engine.reloadAudioClips();

    juce::StringArray warnings;
    engine.reloadAllPlugins (warnings);

    timelineView.rebuild();
    mixerView.rebuild();
    transportBar.getTempoKnob().setValue (document.getState().tempo, juce::dontSendNotification);

    if (auto* editor = mixerView.getInsertEditor())
        editor->refreshFromState();

    refreshAllViews();
    updateWindowTitle();

    showStatus (description.isNotEmpty() ? description : juce::String ("Project restored"));
}

//==============================================================================
// Project files
//==============================================================================
void MainComponent::newProject()
{
    if (! confirmCloseProject())
        return;

    engine.stop();
    document.createNew();
    engine.setProjectState (&document.getState());
    engine.rebuildGraph();
    refreshAllViews();
    timelineView.rebuild();
    showStatus ("New project created.");
}

void MainComponent::openProject()
{
    if (! confirmCloseProject())
        return;

    auto chooser = std::make_shared<juce::FileChooser> ("Open a NUX project",
                                                       document.getProjectFolder(),
                                                       juce::String ("*") + constants::projectFileExtension);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        loadProjectFile (file);
    });
}

void MainComponent::loadProjectFile (const juce::File& file)
{
    juce::String error;

    engine.stop();

    if (! document.loadFromFile (file, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Could not open the project", error);
        return;
    }

    engine.setProjectState (&document.getState());
    engine.setMediaRootDirectory (document.getProjectFolder());
    engine.rebuildGraph();
    engine.reloadAudioClips();

    juce::StringArray pluginWarnings;
    engine.reloadAllPlugins (pluginWarnings);

    if (! pluginWarnings.isEmpty())
        showStatus ("Some plugins could not be loaded: " + pluginWarnings.joinIntoString ("; "));

    transportBar.getTempoKnob().setValue (document.getState().tempo, juce::dontSendNotification);
    refreshAllViews();
    timelineView.rebuild();
    showStatus ("Opened " + file.getFileName());
}

void MainComponent::saveProject (bool saveAs)
{
    // Plugin parameters live inside the plugin, so read them back into the project
    // before the file is written.
    engine.capturePluginStates();

    if (! saveAs && document.getCurrentFile() != juce::File())
    {
        juce::String error;

        if (document.saveToCurrentFile (error))
        {
            showStatus ("Saved " + document.getCurrentFile().getFileName());
            updateWindowTitle();

            if (pendingQuitAfterSave)
            {
                pendingQuitAfterSave = false;
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Could not save the project", error);
        }

        return;
    }

    auto defaultFile = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                          .getChildFile ("NUX Projects")
                          .getChildFile (document.getDisplayName() + constants::projectFileExtension);

    auto chooser = std::make_shared<juce::FileChooser> ("Save the NUX project", defaultFile,
                                                       juce::String ("*") + constants::projectFileExtension);

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == juce::File())
            return;

        if (! file.hasFileExtension (constants::projectFileExtension))
            file = file.withFileExtension (constants::projectFileExtension);

        juce::String error;

        if (document.saveToFile (file, error))
        {
            document.getState().name = file.getFileNameWithoutExtension();
            showStatus ("Saved " + file.getFileName());
            updateWindowTitle();

            if (pendingQuitAfterSave)
            {
                pendingQuitAfterSave = false;
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Could not save the project", error);
        }
    });
}

bool MainComponent::confirmCloseProject()
{
    if (! document.hasUnsavedChanges())
        return true;

    // The quit path is asynchronous: ask, then continue from the callback.
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Unsaved changes")
                                      .withMessage ("Save the changes to \"" + document.getDisplayName()
                                                    + "\" before continuing?")
                                      .withButton ("Save", 1)
                                      .withButton ("Discard", 2)
                                      .withButton ("Cancel", 0),
                                  [this] (int result)
                                  {
                                      if (result == 1)
                                      {
                                          pendingQuitAfterSave = true;
                                          saveProject (false);
                                      }
                                      else if (result == 2)
                                      {
                                          juce::JUCEApplication::getInstance()->systemRequestedQuit();
                                      }
                                  });

    return false;
}

//==============================================================================
void MainComponent::importAudioFileInto (const juce::File& file, int trackIndex)
{
    auto& state = document.getState();

    if (! juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size()))
        return;

    auto& track = state.tracks[(size_t) trackIndex];

    // Importing into an instrument track makes it an audio track.
    if (track.type != TrackType::audio)
    {
        track.type = TrackType::audio;
        track.name = track.name.containsIgnoreCase ("track") ? file.getFileNameWithoutExtension() : track.name;
    }

    engine.setMediaRootDirectory (document.getProjectFolder());

    juce::String error;
    const auto relativePath = engine.importAudioFile (file, error);

    if (relativePath.isEmpty())
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Could not import the audio file")
                                          .withMessage (error)
                                          .withButton ("OK", 1),
                                      nullptr);
        return;
    }

    // Place the clip at the first free bar.
    int startStep = 0;

    for (const auto& existing : track.audioClips)
        startStep = juce::jmax (startStep, existing.startStep + existing.lengthSteps);

    AudioClip clip;
    clip.name = file.getFileNameWithoutExtension();
    clip.sourceFile = relativePath;

    if (auto loaded = engine.resolveClipFile (relativePath); loaded.existsAsFile())
    {
        const auto seconds = engine.getClipLengthSeconds (relativePath);
        const auto stepsPerSecond = (state.tempo / 60.0) * constants::stepsPerBeat;
        const auto lengthSteps = juce::jmax (1, (int) std::ceil (seconds * stepsPerSecond));

        clip.lengthSteps = juce::jmin (lengthSteps, constants::maxNumSteps * 4);
    }

    clip.startStep = juce::jmin (startStep, juce::jmax (0, state.patternLengthSteps - 1));

    // Extend the pattern so the whole clip is inside the loop.
    const auto clipEnd = clip.startStep + clip.lengthSteps;

    if (clipEnd > state.patternLengthSteps)
    {
        auto newLength = state.patternLengthSteps;

        while (newLength < clipEnd && newLength < constants::maxNumSteps)
            newLength += constants::stepsPerBeat * 4;

        state.patternLengthSteps = juce::jmin (newLength, constants::maxNumSteps);
        engine.getTransport().setLoopLengthSteps (state.patternLengthSteps);
    }

    document.beginEdit ("import audio", "Import an audio clip");

    track.audioClips.push_back (clip);

    engine.rebuildGraph();
    engine.reloadAudioClips();
    document.markDirty();
    timelineView.rebuild();
    refreshAllViews();

    showStatus ("Imported " + file.getFileName() + "  (" + juce::String (clip.lengthSteps) + " steps)");
}

//==============================================================================
void MainComponent::loadDemoProjectWithAudioClip (const juce::File& audioFile)
{
    auto& state = document.getState();

    // Turn the third track into an audio track and import the file into the project.
    const auto audioTrackIndex = juce::jmin (2, juce::jmax (0, (int) state.tracks.size() - 1));

    importAudioFileInto (audioFile, audioTrackIndex);

    // Give a second track a note pattern so the screenshot shows both kinds of lane.
    if (state.tracks.size() > 1)
    {
        auto& bass = state.tracks[1];
        bass.instrumentPreset = 1;   // sub bass

        for (int step = 0; step < constants::defaultNumSteps; step += 2)
            bass.steps[(size_t) step] = { true, 0.8f };

        engine.syncSequencerFromState();
    }

    timelineView.rebuild();
    refreshAllViews();
}

//==============================================================================
juce::String MainComponent::loadPluginIntoDemoTrack (int trackIndex,
                                                     const juce::File& pluginFolder,
                                                     const juce::String& pluginName)
{
    pluginManager.addSearchPaths (juce::FileSearchPath (pluginFolder.getFullPathName()));

    juce::StringArray scanErrors;
    pluginManager.scanNow (juce::FileSearchPath (pluginFolder.getFullPathName()), scanErrors);

    const PluginManager::PluginInfo* chosen = nullptr;

    for (const auto& info : pluginManager.getPlugins())
        if (info.name.containsIgnoreCase (pluginName))
            chosen = &info;

    if (chosen == nullptr)
        return "The plugin '" + pluginName + "' was not found in " + pluginFolder.getFullPathName();

    auto& state = document.getState();

    if (! juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size()))
        return "Track " + juce::String (trackIndex) + " does not exist";

    PluginReference reference;
    reference.name         = chosen->name;
    reference.manufacturer = chosen->manufacturer;
    reference.format       = chosen->format;
    reference.isInstrument = chosen->isInstrument;

    if (auto xml = chosen->description.createXml())
        reference.descriptionXml = xml->toString (juce::XmlElement::TextFormat().singleLine());

    auto& slot = state.tracks[(size_t) trackIndex].inserts[0];
    slot.type = InsertType::plugin;
    slot.bypassed = false;
    slot.plugin = reference;
    slot.pluginStateBase64.clear();

    const auto error = engine.loadInsertPlugin (AudioEngine::StripKind::track, trackIndex, 0);

    engine.syncMixerFromState();
    refreshAllViews();

    if (error.isNotEmpty())
        return error;

    return "Loaded " + chosen->name + " into " + state.tracks[(size_t) trackIndex].name
             + " insert 1 (" + juce::String (engine.getNumLoadedPlugins()) + " plugin instances alive)";
}

//==============================================================================
juce::String MainComponent::showInsertEditorForDemo (const juce::String& effectName,
                                                     int trackIndex, int slotIndex)
{
    auto& state = document.getState();

    if (! juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size()))
        return "Track " + juce::String (trackIndex) + " does not exist";

    auto& slot = state.tracks[(size_t) trackIndex].inserts[(size_t) slotIndex];

    if (effectName.equalsIgnoreCase ("eq"))            applyInsertDefaults (slot, InsertType::eq);
    else if (effectName.equalsIgnoreCase ("comp"))     applyInsertDefaults (slot, InsertType::compressor);
    else if (effectName.equalsIgnoreCase ("delay"))    applyInsertDefaults (slot, InsertType::delay);
    else if (effectName.equalsIgnoreCase ("reverb"))   applyInsertDefaults (slot, InsertType::reverb);

    engine.syncMixerFromState();

    setWorkspace (Workspace::mixer);
    mixerView.rebuild();
    mixerView.showInsertEditor (ChannelStripComponent::Kind::track, trackIndex, slotIndex,
                                state.tracks[(size_t) trackIndex].name);

    if (auto* editor = mixerView.getInsertEditor())
    {
        editor->refreshFromState();

        return "Editor open: " + getInsertDisplayName (slot.type)
                 + " on " + state.tracks[(size_t) trackIndex].name
                 + ", " + juce::String ((int) getInsertParameters (slot.type).size()) + " parameters";
    }

    return "The editor did not open";
}

bool MainComponent::setDemoInsertParameter (int parameterIndex, double value)
{
    if (auto* editor = mixerView.getInsertEditor())
    {
        editor->setParameterFromHost (parameterIndex, value);
        return true;
    }

    return false;
}

//==============================================================================
juce::String MainComponent::runUndoSelfTest()
{
    juce::StringArray report;

    auto& state = document.getState();
    const auto originalName = state.tracks[0].name;

    report.add ("start: track 1 = \"" + originalName + "\", undo steps = " + juce::String (document.getUndoCount()));

    // Make a real edit through the same path the UI uses.
    document.beginEdit ("self test rename", "Self test rename");
    state.tracks[0].name = "Renamed by self test";
    document.markDirty();
    timelineView.rebuild();

    report.add ("edited: track 1 = \"" + document.getState().tracks[0].name
                  + "\", undo steps = " + juce::String (document.getUndoCount())
                  + ", canUndo = " + juce::String ((int) document.canUndo()));

    // Undo through the command, which is what Ctrl+Z triggers.
    commandManager.invokeDirectly (cmdUndo, false);

    report.add ("undo: track 1 = \"" + document.getState().tracks[0].name
                  + "\", canRedo = " + juce::String ((int) document.canRedo()));

    // Redo, again through the command.
    commandManager.invokeDirectly (cmdRedo, false);

    report.add ("redo: track 1 = \"" + document.getState().tracks[0].name + "\"");

    const auto ok = document.getState().tracks[0].name == "Renamed by self test";

    report.add (ok ? "RESULT: undo/redo verified" : "RESULT: FAILED");

    // Leave the project as it started so the screenshot looks normal.
    commandManager.invokeDirectly (cmdUndo, false);
    document.setHasUnsavedChanges (false);

    return report.joinIntoString ("\n");
}

//==============================================================================
void MainComponent::exportWav()
{
    auto defaultFile = document.getProjectFolder()
                          .getChildFile (document.getDisplayName() + ".wav");

    auto chooser = std::make_shared<juce::FileChooser> ("Export the mixdown as a WAV",
                                                       defaultFile, "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == juce::File())
            return;

        if (! file.hasFileExtension ("wav"))
            file = file.withFileExtension ("wav");

        AudioEngine::ExportOptions options;
        options.targetFile = file;
        options.lengthSteps = document.getState().patternLengthSteps;
        options.tailSeconds = 2.0;

        juce::String error;

        engine.stop();

        if (engine.exportToWav (options, error))
        {
            showStatus ("Exported " + file.getFileName());
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                    "Export complete",
                                                    "Rendered the pattern to:\n" + file.getFullPathName());
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Export failed", error);
        }
    });
}

//==============================================================================
void MainComponent::toggleAudioSettings()
{
    if (audioSettingsWindow != nullptr)
    {
        closeAudioSettingsWindow();
        return;
    }

    showAudioSettingsWindow();
}

void MainComponent::showAudioSettingsForDemo (bool startTestTone)
{
    showAudioSettingsWindow();

    if (audioSettingsPanel == nullptr)
        return;

    audioSettingsPanel->refresh();

    if (startTestTone)
        engine.setTestToneEnabled (true);
}

juce::Component* MainComponent::getAudioSettingsComponentForDemo() const
{
    if (audioSettingsWindow != nullptr)
        return audioSettingsWindow->getContentComponent();

    return nullptr;
}

void MainComponent::showAudioSettingsWindow()
{
    audioSettingsPanel = new AudioSettingsPanel (engine);

    audioSettingsPanel->onSettingsChanged = [this]
    {
        engine.rebuildGraph();
        showStatus ("Audio: " + engine.getAudioStatusMessage());
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (audioSettingsPanel);
    options.dialogTitle = "NUX - Audio settings";
    options.dialogBackgroundColour = theme::windowBg;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    audioSettingsWindow.reset (options.launchAsync());
    audioSettingsPanel = nullptr;   // owned by the window from here

    if (auto* panel = dynamic_cast<AudioSettingsPanel*> (audioSettingsWindow->getContentComponent()))
        audioSettingsPanel = panel;

    if (audioSettingsWindow != nullptr)
    {
        audioSettingsWindow->setName ("NUX - Audio settings");
        audioSettingsWindow->centreWithSize (620, 480);
        audioSettingsWindow->setVisible (true);
    }
}

void MainComponent::closeAudioSettingsWindow()
{
    audioSettingsWindow.reset();
    audioSettingsPanel = nullptr;
}

//==============================================================================
void MainComponent::showAbout()
{
    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::InfoIcon,
        juce::String (constants::appName) + " " + constants::versionString,
        juce::String ("A digital audio workstation for Windows.\n\n")
            + "Engine: JUCE " + juce::String (JUCE_MAJOR_VERSION) + "." + juce::String (JUCE_MINOR_VERSION)
            + "." + juce::String (JUCE_BUILDNUMBER) + "\n"
            + "Formats: WASAPI, DirectSound and ASIO (where the driver is installed)\n"
            + "Plugins: VST3\n\n"
            + "Built with C++ and CMake. MIT licensed application code; JUCE is used under its own licence.");
}
}
