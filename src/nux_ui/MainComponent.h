/* NUX DAW - the application window content: tabs, workspaces, menus, shortcuts. */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../nux_core/ProjectDocument.h"
#include "../nux_engine/AudioEngine.h"
#include "../nux_engine/PluginManager.h"
#include "AudioSettingsPanel.h"
#include "MixerView.h"
#include "PianoRollView.h"
#include "PluginPanel.h"
#include "Theme.h"
#include "TimelineView.h"
#include "TransportBar.h"
#include "Widgets.h"

namespace nux
{
class NuxApplication;

enum class Workspace
{
    main = 0,
    mixer,
    pianoRoll,
    plugins,
    master
};

class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            private juce::Timer,
                            private juce::ChangeListener,
                            private juce::AsyncUpdater
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    //==============================================================================
    // Project handling
    void newProject();
    void openProject();
    void saveProject (bool saveAs);
    /** Returns true if the app may quit right away. When there is unsaved work the
        question is asked asynchronously and quitting continues from the callback. */
    bool confirmCloseProject();
    void loadProjectFile (const juce::File& file);

    void exportWav();
    void importAudioFileInto (const juce::File& file, int trackIndex);

    /** Builds a demo project that contains an audio clip. Used by the smoke test. */
    void loadDemoProjectWithAudioClip (const juce::File& audioFile);

    /** Loads a plugin into a track's first insert slot and reports the result.
        Used by the headless smoke test and by any future command line tooling. */
    juce::String loadPluginIntoDemoTrack (int trackIndex, const juce::File& pluginFolder,
                                          const juce::String& pluginName);

    /** Opens the mixer with the parameter editor showing one insert, and reports
        what the editor found there. Used by the headless smoke test. */
    juce::String showInsertEditorForDemo (const juce::String& effectName, int trackIndex, int slotIndex);

    /** Changes one parameter of the insert currently shown in the editor. */
    bool setDemoInsertParameter (int parameterIndex, double value);
    void toggleAudioSettings();
    void showAbout();

    //==============================================================================
    juce::ApplicationCommandManager& getCommandManager() noexcept { return commandManager; }
    void registerCommands();

    //==============================================================================
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleAsyncUpdate() override;
    void setWorkspace (Workspace workspace);
    void updateWindowTitle();
    void showAudioSettingsWindow();
    void closeAudioSettingsWindow();
    void refreshAllViews();
    void showStatus (const juce::String& message);

    theme::NuxLookAndFeel globalLookAndFeel;
    juce::ApplicationCommandManager commandManager;

    ProjectDocument document;
    AudioEngine engine;
    PluginManager pluginManager;

    TransportBar transportBar;
    TabSelector tabs;
    TimelineView timelineView;
    MixerView mixerView;
    PianoRollView pianoRollView;
    PluginPanel pluginPanel;

    /** The Master tab: master strip, send effects and export controls. */
    class MasterPanel;
    std::unique_ptr<MasterPanel> masterPanel;

    juce::Label statusLabel;
    juce::TooltipWindow tooltips { this, 600 };

    std::unique_ptr<juce::DialogWindow> audioSettingsWindow;
    AudioSettingsPanel* audioSettingsPanel = nullptr;

    Workspace currentWorkspace = Workspace::main;
    bool audioInitialised = false;
    bool pendingQuitAfterSave = false;
};
}
