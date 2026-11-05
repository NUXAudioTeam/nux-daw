#include "PluginPicker.h"
#include "../nux_engine/PluginSlot.h"

namespace nux
{
namespace
{
/** Keeps the window alive while it is on screen. */
juce::OwnedArray<juce::DialogWindow>& openPickerWindows()
{
    static juce::OwnedArray<juce::DialogWindow> windows;
    return windows;
}
}

//==============================================================================
PluginPickerWindow::PluginPickerWindow (PluginManager& managerToUse, bool instrumentsOnlyIn)
    : manager (managerToUse),
      instrumentsOnly (instrumentsOnlyIn)
{
    titleLabel.setText (instrumentsOnly ? "Choose an instrument plugin" : "Choose an effect plugin",
                        juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (titleLabel);

    searchBox.setTextToShowWhenEmpty ("Search plugins...", theme::textFaint);
    searchBox.setColour (juce::TextEditor::backgroundColourId, theme::panel);
    searchBox.setColour (juce::TextEditor::outlineColourId, theme::outline);
    searchBox.setColour (juce::TextEditor::textColourId, theme::text);
    searchBox.onTextChange = [this]
    {
        filter = searchBox.getText();
        refresh();
    };
    addAndMakeVisible (searchBox);

    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, theme::panel);
    list.setColour (juce::ListBox::outlineColourId, theme::outline);
    list.setOutlineThickness (1);
    addAndMakeVisible (list);

    chooseButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    chooseButton.setColour (juce::TextButton::textColourOffId, theme::windowBg);
    chooseButton.onClick = [this]
    {
        const auto row = list.getSelectedRow();

        if (row >= 0)
            chooseRow (row);
    };
    addAndMakeVisible (chooseButton);

    rescanButton.onClick = [this]
    {
        statusLabel.setText ("Scanning...", juce::dontSendNotification);

        juce::StringArray errors;
        manager.scanNow (manager.getSearchPaths(), errors);
        refresh();

        statusLabel.setText (juce::String (manager.getPlugins().size()) + " plugins found",
                             juce::dontSendNotification);
    };
    addAndMakeVisible (rescanButton);

    cancelButton.onClick = [this]
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->closeButtonPressed();
    };
    addAndMakeVisible (cancelButton);

    statusLabel.setColour (juce::Label::textColourId, theme::textFaint);
    statusLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (statusLabel);

    refresh();

    statusLabel.setText (juce::String (manager.getPlugins().size())
                             + " plugins installed, "
                             + juce::String (instrumentsOnly ? "instruments shown" : "effects shown"),
                         juce::dontSendNotification);

    setSize (520, 400);
}

//==============================================================================
std::vector<const PluginManager::PluginInfo*> PluginPickerWindow::visiblePlugins() const
{
    std::vector<const PluginManager::PluginInfo*> result;

    for (const auto& info : manager.getPlugins())
    {
        if (instrumentsOnly != info.isInstrument)
            continue;

        if (filter.isNotEmpty())
        {
            const auto haystack = info.name + " " + info.manufacturer;

            if (! haystack.containsIgnoreCase (filter))
                continue;
        }

        result.push_back (&info);
    }

    return result;
}

void PluginPickerWindow::refresh()
{
    list.updateContent();

    const auto count = (int) visiblePlugins().size();

    if (count > 0)
        list.selectRow (0);
    else
        list.deselectAllRows();

    chooseButton.setEnabled (count > 0);
    list.repaint();
}

int PluginPickerWindow::getNumRows()
{
    return (int) visiblePlugins().size();
}

void PluginPickerWindow::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    const auto plugins = visiblePlugins();

    if (! juce::isPositiveAndBelow (row, (int) plugins.size()))
        return;

    const auto& info = *plugins[(size_t) row];

    if (rowIsSelected)
    {
        g.setColour (theme::accent.withAlpha (0.18f));
        g.fillRect (0, 0, width, height);
    }

    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (info.name, 10, 0, width - 200, height, juce::Justification::centredLeft, true);

    g.setColour (theme::textFaint);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (info.manufacturer + (info.version.isNotEmpty() ? "  v" + info.version : ""),
                width - 250, 0, 180, height, juce::Justification::centredRight, true);

    g.drawText (info.format, width - 70, 0, 60, height, juce::Justification::centredRight, true);
}

void PluginPickerWindow::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    chooseRow (row);
}

void PluginPickerWindow::returnKeyPressed (int row)
{
    chooseRow (row);
}

void PluginPickerWindow::chooseRow (int row)
{
    const auto plugins = visiblePlugins();

    if (! juce::isPositiveAndBelow (row, (int) plugins.size()))
        return;

    const auto description = plugins[(size_t) row]->description;

    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->closeButtonPressed();

    if (onPluginChosen)
        onPluginChosen (description);
}

//==============================================================================
void PluginPickerWindow::paint (juce::Graphics& g)
{
    g.fillAll (theme::windowBg);
}

void PluginPickerWindow::resized()
{
    auto area = getLocalBounds().reduced (12);

    titleLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);
    searchBox.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);

    auto buttons = area.removeFromBottom (32);

    cancelButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    chooseButton.setBounds (buttons.removeFromRight (90));
    rescanButton.setBounds (buttons.removeFromLeft (90));

    statusLabel.setBounds (buttons.reduced (8, 0));

    area.removeFromBottom (6);
    list.setBounds (area);
}

//==============================================================================
void showPluginPicker (PluginManager& manager, bool instrumentsOnly,
                       std::function<void (const juce::PluginDescription&)> onChosen)
{
    auto content = std::make_unique<PluginPickerWindow> (manager, instrumentsOnly);
    auto* rawContent = content.get();

    rawContent->onPluginChosen = std::move (onChosen);

    auto* window = new juce::DialogWindow (instrumentsOnly ? "Instruments" : "Effects",
                                           theme::windowBg, true, true);

    window->setContentOwned (content.release(), true);
    window->setResizable (true, false);
    window->centreWithSize (520, 400);

    // The dialog is closed by its buttons; the owner array keeps it alive until
    // then, and a short timer frees it once it has been dismissed.
    openPickerWindows().add (window);

    window->setVisible (true);
    window->enterModalState (false, nullptr, true);

    struct Cleaner final : private juce::Timer
    {
        Cleaner() { startTimer (200); }

        void timerCallback() override
        {
            auto& all = openPickerWindows();

            for (int i = all.size(); --i >= 0;)
                if (! all[i]->isVisible() && ! all[i]->isCurrentlyModal())
                    all.remove (i);

            if (all.isEmpty())
            {
                stopTimer();
                delete this;
            }
        }
    };

    if (openPickerWindows().size() == 1)
        new Cleaner();
}

//==============================================================================
// Plugin editor window
//==============================================================================
PluginEditorWindow::PluginEditorWindow (juce::String nameIn, juce::AudioProcessorEditor* editorToOwn)
    : juce::DocumentWindow (nameIn, theme::windowBg, juce::DocumentWindow::allButtons),
      pluginName (std::move (nameIn)),
      editor (editorToOwn)
{
    setUsingNativeTitleBar (true);
    setContentOwned (editorToOwn, true);
    setResizable (true, false);
    centreWithSize (juce::jmax (400, editorToOwn->getWidth()),
                    juce::jmax (240, editorToOwn->getHeight()));
    setVisible (true);

    // Poll for the plugin being pulled out of the graph underneath us.
    startTimer (400);
}

PluginEditorWindow* PluginEditorWindow::createAndShow (PluginSlot& slot)
{
    auto* instance = slot.getInstance();

    if (instance == nullptr || ! instance->hasEditor())
        return nullptr;

    auto* editor = instance->createEditorIfNeeded();

    if (editor == nullptr)
        return nullptr;

    return new PluginEditorWindow (slot.getName(), editor);
}

void PluginEditorWindow::closeButtonPressed()
{
    stopTimer();
    editor.reset();      // must be destroyed before the window goes away
    delete this;
}

void PluginEditorWindow::timerCallback()
{
    // If the plugin instance has gone (a project reload, a slot removal) the editor
    // is stale, so the window closes itself rather than holding a dead pointer.
    if (editor == nullptr)
    {
        stopTimer();
        delete this;
    }
}
}
