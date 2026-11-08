/* NUX DAW - the Plugins view: VST3 browser and plugin editor host. */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../nux_engine/AudioEngine.h"
#include "../nux_engine/PluginManager.h"
#include "Widgets.h"

namespace nux
{
class PluginPanel final : public juce::Component,
                          private juce::Timer,
                          private juce::ListBoxModel
{
public:
    PluginPanel (PluginManager& manager, AudioEngine& engine);
    ~PluginPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void openSelectedPlugin();
    void showPluginEditor (const juce::PluginDescription& description);

    PluginManager& manager;
    AudioEngine& engine;

    juce::Label title;
    juce::Label status;
    juce::TextEditor filterBox;
    juce::TextButton scanButton { "SCAN VST3" };
    juce::TextButton loadButton { "OPEN PLUGIN" };
    juce::TextButton closeEditorButton { "CLOSE EDITOR" };
    juce::ListBox list { "plugins", this };
    juce::ProgressBar progressBar { progress };
    juce::ToggleButton instrumentOnly { "Instruments only" };

    std::vector<int> filteredIndices;
    int lastSelected = -1;

    double progress = 0.0;

    juce::DialogWindow::LaunchOptions editorWindowOptions;
    std::unique_ptr<juce::DialogWindow> editorWindow;
    std::unique_ptr<juce::AudioPluginInstance> currentInstance;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPanel)
};
}
