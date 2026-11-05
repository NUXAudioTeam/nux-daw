#include "PluginPanel.h"

namespace nux
{
PluginPanel::PluginPanel (PluginManager& m, AudioEngine& e)
    : manager (m), engine (e)
{
    addAndMakeVisible (title);
    addAndMakeVisible (status);
    addAndMakeVisible (filterBox);
    addAndMakeVisible (scanButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (closeEditorButton);
    addAndMakeVisible (list);
    addAndMakeVisible (progressBar);
    addAndMakeVisible (instrumentOnly);

    title.setText ("VST3 PLUGINS", juce::dontSendNotification);
    title.setFont (theme::font::bold (14.0f));
    title.setColour (juce::Label::textColourId, theme::accent);

    status.setFont (theme::font::ui (11.0f));
    status.setColour (juce::Label::textColourId, theme::textFaint);

    filterBox.setTextToShowWhenEmpty ("Filter by name or maker", theme::textFaint);
    filterBox.onTextChange = [this]
    {
        list.updateContent();
        list.repaint();
    };

    scanButton.onClick = [this]
    {
        manager.startScan();
        status.setText ("Scanning VST3 folders...", juce::dontSendNotification);
    };

    loadButton.onClick = [this] { openSelectedPlugin(); };
    closeEditorButton.onClick = [this]
    {
        currentInstance.reset();
        editorWindow.reset();
        status.setText ("Plugin closed.", juce::dontSendNotification);
    };

    instrumentOnly.onClick = [this] { list.updateContent(); list.repaint(); };

    progressBar.setColour (juce::ProgressBar::backgroundColourId, theme::surface);
    progressBar.setColour (juce::ProgressBar::foregroundColourId, theme::accent);

    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, theme::panel);
    list.setColour (juce::ListBox::outlineColourId, theme::outline);
    list.setOutlineThickness (1);

    manager.onScanProgress = [this]
    {
        // Safe: the scanner only calls this from its own thread; the timer repaints.
        progress = manager.getScanProgress();
    };

    manager.onScanFinished = [this]
    {
        list.updateContent();
        list.repaint();
    };

    manager.loadCachedList();

    startTimerHz (4);
}

PluginPanel::~PluginPanel()
{
    stopTimer();
    manager.onScanProgress = nullptr;
    manager.onScanFinished = nullptr;
    currentInstance.reset();
    editorWindow.reset();
}

void PluginPanel::timerCallback()
{
    progress = manager.getScanProgress();
    progressBar.repaint();

    const auto newStatus = manager.getScanStatus();

    if (status.getText() != newStatus)
        status.setText (newStatus, juce::dontSendNotification);

    scanButton.setEnabled (! manager.isScanning());
    list.updateContent();
}

int PluginPanel::getNumRows()
{
    const auto& plugins = manager.getPlugins();
    const auto filter = filterBox.getText().trim();
    const auto onlyInstruments = instrumentOnly.getToggleState();

    filteredIndices.clear();

    for (int i = 0; i < (int) plugins.size(); ++i)
    {
        const auto& plugin = plugins[(size_t) i];

        if (onlyInstruments && ! plugin.isInstrument)
            continue;

        if (filter.isNotEmpty()
            && ! plugin.name.containsIgnoreCase (filter)
            && ! plugin.manufacturer.containsIgnoreCase (filter))
            continue;

        filteredIndices.push_back (i);
    }

    return (int) filteredIndices.size();
}

void PluginPanel::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    const auto& plugins = manager.getPlugins();

    if (! juce::isPositiveAndBelow (rowNumber, (int) filteredIndices.size()))
        return;

    const auto& plugin = plugins[(size_t) filteredIndices[(size_t) rowNumber]];

    if (rowIsSelected)
    {
        g.setColour (theme::accentDeep.withAlpha (0.35f));
        g.fillRect (0, 0, width, height);
    }

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);

    g.setColour (plugin.isInstrument ? theme::accent : theme::text);
    g.setFont (theme::font::bold (12.0f));
    g.drawFittedText (plugin.name, area.removeFromLeft (juce::jmax (120, width - 260)),
                      juce::Justification::centredLeft, 1, 0.85f);

    g.setColour (theme::textFaint);
    g.setFont (theme::font::ui (11.0f));
    g.drawFittedText (plugin.manufacturer, area.removeFromLeft (140),
                      juce::Justification::centredLeft, 1, 0.85f);
    g.drawFittedText (plugin.format + (plugin.isInstrument ? " | INSTR" : " | FX"), area,
                      juce::Justification::centredRight, 1, 0.85f);
}

void PluginPanel::selectedRowsChanged (int lastRowSelected)
{
    lastSelected = lastRowSelected;
    loadButton.setEnabled (juce::isPositiveAndBelow (lastRowSelected, (int) filteredIndices.size()));
}

void PluginPanel::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    openSelectedPlugin();
}

void PluginPanel::openSelectedPlugin()
{
    const auto& plugins = manager.getPlugins();

    if (! juce::isPositiveAndBelow (lastSelected, (int) filteredIndices.size()))
        return;

    showPluginEditor (plugins[(size_t) filteredIndices[(size_t) lastSelected]].description);
}

void PluginPanel::showPluginEditor (const juce::PluginDescription& description)
{
    juce::String error;

    currentInstance = manager.createInstance (description,
                                              engine.getTransport().getTempo() > 0 ? 44100.0 : 44100.0,
                                              512, error);

    if (currentInstance == nullptr)
    {
        status.setText ("Could not open " + description.name + ": " + error, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, theme::recordRed);
        return;
    }

    status.setColour (juce::Label::textColourId, theme::accent);

    if (! currentInstance->hasEditor())
    {
        status.setText (description.name + " has no editor window (headless plugin).",
                        juce::dontSendNotification);
        return;
    }

    auto* editor = currentInstance->createEditorIfNeeded();

    if (editor == nullptr)
    {
        status.setText ("The editor for " + description.name + " could not be created.",
                        juce::dontSendNotification);
        return;
    }

    editorWindow.reset (new juce::DialogWindow (description.name,
                                                theme::windowBg, true, true));

    editorWindow->setContentOwned (editor, true);
    editorWindow->setResizable (true, false);
    editorWindow->centreWithSize (juce::jmax (500, editor->getWidth()),
                                  juce::jmax (320, editor->getHeight()));
    editorWindow->setVisible (true);

    status.setText ("Loaded " + description.name + "  (" + description.pluginFormatName + ")",
                    juce::dontSendNotification);
}

void PluginPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::windowBg);
    g.fillRect (getLocalBounds());

    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (64));

    g.setColour (theme::outline);
    g.drawHorizontalLine (63, 0.0f, (float) getWidth());
}

void PluginPanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (64).reduced (12, 6);

    title.setBounds (header.removeFromTop (20));

    auto controls = header;
    scanButton.setBounds (controls.removeFromLeft (92).reduced (0, 2));
    controls.removeFromLeft (6);
    loadButton.setBounds (controls.removeFromLeft (104).reduced (0, 2));
    controls.removeFromLeft (6);
    closeEditorButton.setBounds (controls.removeFromLeft (110).reduced (0, 2));
    controls.removeFromLeft (12);
    filterBox.setBounds (controls.removeFromLeft (juce::jmax (120, juce::jmin (220, controls.getWidth() - 220))));
    controls.removeFromLeft (8);
    instrumentOnly.setBounds (controls.removeFromLeft (130));

    auto footer = area.removeFromBottom (34).reduced (12, 6);
    progressBar.setBounds (footer.removeFromRight (juce::jmin (200, footer.getWidth() / 2)));
    footer.removeFromRight (10);
    status.setBounds (footer);

    list.setBounds (area.reduced (12, 6));
}
}
