/* NUX DAW - the plugin chooser.

   Shows every plugin the scan found, with a search box and separate sections for
   effects and instruments. The caller gets back the chosen PluginDescription, or
   nothing if the window was dismissed.

   The picker also owns the little "rescan" affordance, because that is the moment
   a user notices a plugin they just installed is missing. */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "../nux_engine/PluginManager.h"
#include "Theme.h"

namespace nux
{
class PluginSlot;

class PluginPickerWindow final : public juce::Component,
                                 private juce::ListBoxModel
{
public:
    /** `instrumentsOnly` limits the list to instruments (used for a track's
        instrument slot) instead of effects. */
    PluginPickerWindow (PluginManager& manager, bool instrumentsOnly);

    /** Called with the chosen plugin. Not called when the window is dismissed. */
    std::function<void (const juce::PluginDescription&)> onPluginChosen;

    void paint (juce::Graphics&) override;
    void resized() override;

    //==============================================================================
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;

private:
    void refresh();
    void chooseRow (int row);
    std::vector<const PluginManager::PluginInfo*> visiblePlugins() const;

    PluginManager& manager;
    bool instrumentsOnly;
    juce::String filter;

    juce::Label titleLabel;
    juce::TextEditor searchBox;
    juce::ListBox list { "plugins", this };
    juce::TextButton chooseButton { "Load" };
    juce::TextButton rescanButton { "Rescan" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label statusLabel;

    std::unique_ptr<juce::FileChooser> folderChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPickerWindow)
};

/** A floating window holding a hosted plugin's own editor.

    The window keeps the plugin instance alive for as long as it is open, so an
    instance is never destroyed while its editor is on screen. */
class PluginEditorWindow final : public juce::DocumentWindow,
                                 private juce::Timer
{
public:
    /** Opens the editor for a live plugin slot. Returns nullptr when the plugin has
        no editor (many plugins are headless). The window owns itself. */
    static PluginEditorWindow* createAndShow (PluginSlot& slot);

    void closeButtonPressed() override;

private:
    PluginEditorWindow (juce::String nameIn, juce::AudioProcessorEditor* editorToOwn);

    void timerCallback() override;

    juce::String pluginName;
    std::unique_ptr<juce::AudioProcessorEditor> editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditorWindow)
};

/** Opens the picker as a modal-ish floating window and calls `onChosen` when the
    user loads a plugin. Ownership of the window is handled internally. */
void showPluginPicker (PluginManager& manager, bool instrumentsOnly,
                       std::function<void (const juce::PluginDescription&)> onChosen);
}
