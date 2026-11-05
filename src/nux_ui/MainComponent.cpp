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
            document.getState().master.volumeDb = (float) value;
            engine.syncMixerFromState();
            document.markDirty();
        };

        addAndMakeVisible (limiterButton);
        limiterButton.setToggleState (document.getState().master.limiterEnabled, juce::dontSendNotification);
        limiterButton.onClick = [this]
        {
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

    transportBar.onSaveRequested = [this] { saveProject (false); };
    transportBar.onAudioSettingsRequested = [this] { toggleAudioSettings(); };
    transportBar.onTempoChanged = [this]
    {
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
