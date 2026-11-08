#include "InsertEditorPanel.h"
#include "PluginPicker.h"

namespace nux
{
InsertEditorPanel::InsertEditorPanel (ProjectDocument& documentToUse, AudioEngine& engineToUse, Target targetToUse)
    : document (documentToUse),
      engine (engineToUse),
      target (std::move (targetToUse))
{
    titleLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (titleLabel);

    subtitleLabel.setFont (juce::FontOptions (11.0f));
    subtitleLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (subtitleLabel);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, theme::warn);
    bypassButton.setColour (juce::TextButton::buttonColourId, theme::panelRaised);
    bypassButton.onClick = [this]
    {
        if (auto* slot = findSlot())
        {
            document.beginEdit ("insert bypass", "Bypass an insert");

            slot->bypassed = ! slot->bypassed;
            engine.syncMixerFromState();
            document.markDirty();
            refreshFromState();

            if (onEdited)
                onEdited();
        }
    };
    addAndMakeVisible (bypassButton);

    resetButton.setColour (juce::TextButton::buttonColourId, theme::panelRaised);
    resetButton.onClick = [this]
    {
        if (auto* slot = findSlot())
        {
            document.beginEdit ("insert reset", "Reset an insert");

            const auto type = slot->type;
            applyInsertDefaults (*slot, type);
            engine.syncMixerFromState();
            document.markDirty();
            refreshFromState();

            if (onEdited)
                onEdited();
        }
    };
    addAndMakeVisible (resetButton);

    pluginButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    pluginButton.setColour (juce::TextButton::textColourOffId, theme::windowBg);
    pluginButton.onClick = [this]
    {
        auto* manager = engine.getPluginManager();

        if (manager == nullptr)
            return;

        const auto kind = target.kind;
        const auto stripIndex = target.stripIndex;
        const auto slotIndex = target.slotIndex;
        auto* self = this;

        showPluginPicker (*manager, false, [self, kind, stripIndex, slotIndex]
                          (const juce::PluginDescription& description)
        {
            auto& state = self->document.getState();
            InsertSlot* modelSlot = nullptr;

            if (kind == AudioEngine::StripKind::track
                && juce::isPositiveAndBelow (stripIndex, (int) state.tracks.size()))
                modelSlot = &state.tracks[(size_t) stripIndex].inserts[(size_t) slotIndex];
            else if (kind == AudioEngine::StripKind::bus
                     && juce::isPositiveAndBelow (slotIndex, constants::maxBusInserts))
                modelSlot = &state.buses[(size_t) stripIndex].inserts[(size_t) slotIndex];
            else if (kind == AudioEngine::StripKind::master)
                modelSlot = &state.master.inserts[(size_t) slotIndex];

            if (modelSlot == nullptr)
                return;

            PluginReference reference;
            reference.name         = description.name;
            reference.manufacturer = description.manufacturerName;
            reference.format       = description.pluginFormatName;
            reference.isInstrument = description.isInstrument;

            if (auto xml = description.createXml())
                reference.descriptionXml = xml->toString (juce::XmlElement::TextFormat().singleLine());

            reference.stateSaved = false;

            self->document.beginEdit ("insert plugin", "Load a plugin");

            modelSlot->type = InsertType::plugin;
            modelSlot->bypassed = false;
            modelSlot->plugin = reference;
            modelSlot->pluginStateBase64.clear();

            const auto error = self->engine.loadInsertPlugin (kind, stripIndex, slotIndex);

            self->engine.syncMixerFromState();
            self->document.markDirty();
            self->refreshFromState();

            if (self->onEdited)
                self->onEdited();

            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Could not load " + description.name, error);
        });
    };
    addAndMakeVisible (pluginButton);

    emptyLabel.setFont (juce::FontOptions (12.0f));
    emptyLabel.setColour (juce::Label::textColourId, theme::textFaint);
    emptyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (emptyLabel);

    refreshFromState();
}

InsertEditorPanel::~InsertEditorPanel() = default;

//==============================================================================
InsertSlot* InsertEditorPanel::findSlot()
{
    auto& state = document.getState();

    if (target.kind == AudioEngine::StripKind::track)
    {
        if (juce::isPositiveAndBelow (target.stripIndex, (int) state.tracks.size()))
            return &state.tracks[(size_t) target.stripIndex].inserts[(size_t) target.slotIndex];

        return nullptr;
    }

    if (target.kind == AudioEngine::StripKind::bus)
    {
        if (juce::isPositiveAndBelow (target.stripIndex, constants::numMixerBuses)
            && juce::isPositiveAndBelow (target.slotIndex, constants::maxBusInserts))
            return &state.buses[(size_t) target.stripIndex].inserts[(size_t) target.slotIndex];

        return nullptr;
    }

    return &state.master.inserts[(size_t) target.slotIndex];
}

bool InsertEditorPanel::hasEditableParameters() const
{
    const auto type = const_cast<InsertEditorPanel*> (this)->findSlot() != nullptr
                        ? const_cast<InsertEditorPanel*> (this)->findSlot()->type
                        : InsertType::none;

    return ! getInsertParameters (type).empty();
}

juce::String InsertEditorPanel::getSlotTitle() const
{
    auto* self = const_cast<InsertEditorPanel*> (this);
    auto* slot = self->findSlot();

    const auto stripLabel = target.stripName.isNotEmpty() ? target.stripName : juce::String ("Channel");
    const auto effectName = slot != nullptr ? (slot->type == InsertType::plugin && slot->plugin.name.isNotEmpty()
                                                   ? slot->plugin.name
                                                   : getInsertDisplayName (slot->type))
                                            : juce::String ("Empty");

    return stripLabel + "  -  Insert " + juce::String (target.slotIndex + 1) + ": " + effectName;
}

void InsertEditorPanel::buildControlsForCurrentType()
{
    knobs.clear();

    for (size_t i = 0; i < descriptors.size(); ++i)
    {
        const auto& descriptor = descriptors[i];

        auto knob = std::make_unique<NuxKnob> (descriptor.name,
                                               descriptor.minimum,
                                               descriptor.maximum,
                                               descriptor.interval,
                                               descriptor.defaultValue);

        knob->setSuffix (descriptor.unit);
        knob->setKnobSize (compact ? 34 : 44);

        if (descriptor.isLogarithmic)
            knob->getSlider().setSkewFactorFromMidPoint (std::sqrt (descriptor.minimum * descriptor.maximum));

        const auto index = (int) i;

        knob->onValueChange = [this, index] (double value)
        {
            if (updating)
                return;

            applyValueToSlot (index, value);
        };

        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }
}

void InsertEditorPanel::applyValueToSlot (int parameterIndex, double newValue)
{
    auto* slot = findSlot();

    if (slot == nullptr)
        return;

    auto values = readInsertValues (*slot);

    if (! juce::isPositiveAndBelow (parameterIndex, (int) values.size()))
        return;

    // Coalesced by key, so dragging a knob is one undo step rather than hundreds.
    document.beginEdit ("insert param " + juce::String (parameterIndex), "Change an insert parameter");

    values[(size_t) parameterIndex] = newValue;

    writeInsertValues (*slot, values);

    engine.syncMixerFromState();
    document.markDirty();

    if (onEdited)
        onEdited();
}

void InsertEditorPanel::setParameterFromHost (int parameterIndex, double value)
{
    if (! juce::isPositiveAndBelow (parameterIndex, (int) knobs.size()))
        return;

    if (parameterIndex < (int) descriptors.size())
        value = juce::jlimit (descriptors[(size_t) parameterIndex].minimum,
                              descriptors[(size_t) parameterIndex].maximum,
                              value);

    knobs[(size_t) parameterIndex]->setValue (value);   // fires onValueChange
    applyValueToSlot (parameterIndex, value);
}

std::vector<double> InsertEditorPanel::getParameterValues() const
{
    if (auto* slot = const_cast<InsertEditorPanel*> (this)->findSlot())
        return readInsertValues (*slot);

    return {};
}

//==============================================================================
void InsertEditorPanel::refreshFromState()
{
    auto* slot = findSlot();
    const auto type = slot != nullptr ? slot->type : InsertType::none;

    titleLabel.setText (getSlotTitle(), juce::dontSendNotification);

    if (slot == nullptr)
    {
        emptyLabel.setText ("This insert does not exist any more.", juce::dontSendNotification);
        emptyLabel.setVisible (true);
        bypassButton.setVisible (false);
        resetButton.setVisible (false);
        pluginButton.setVisible (false);
        descriptors.clear();
        knobs.clear();
        resized();
        return;
    }

    descriptors = getInsertParameters (type);

    if (type != currentType || knobs.size() != descriptors.size())
    {
        currentType = type;
        buildControlsForCurrentType();
    }

    const auto isPlugin = type == InsertType::plugin;
    const auto isEmpty  = type == InsertType::none;

    bypassButton.setVisible (! isEmpty);
    bypassButton.setToggleState (slot->bypassed, juce::dontSendNotification);
    resetButton.setVisible (! isEmpty && ! isPlugin);
    pluginButton.setVisible (isPlugin);

    if (isPlugin)
    {
        subtitleLabel.setText (slot->plugin.format.isNotEmpty()
                                   ? slot->plugin.format + " plugin"
                                   : juce::String ("Plugin"),
                               juce::dontSendNotification);
        emptyLabel.setText ("Plugins draw their own controls.", juce::dontSendNotification);
        emptyLabel.setVisible (true);
    }
    else if (isEmpty)
    {
        subtitleLabel.setText ("No effect in this slot", juce::dontSendNotification);
        emptyLabel.setText ("Use the insert menu to add an effect or a plugin.",
                            juce::dontSendNotification);
        emptyLabel.setVisible (true);
    }
    else
    {
        subtitleLabel.setText ("Built-in effect", juce::dontSendNotification);
        emptyLabel.setVisible (false);

        // Push the current model values into the controls.
        const auto values = readInsertValues (*slot);

        updating = true;

        // readInsertValues already returns descriptor units, so no conversion here.
        for (size_t i = 0; i < knobs.size() && i < values.size(); ++i)
            knobs[i]->setValue (values[i]);

        updating = false;
    }

    resized();
    repaint();
}

void InsertEditorPanel::setCompact (bool shouldBeCompact)
{
    if (compact == shouldBeCompact)
        return;

    compact = shouldBeCompact;
    resized();
    repaint();
}

//==============================================================================
void InsertEditorPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    g.setColour (theme::outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

    // A thin accent bar marks the edited insert's channel colour.
    g.setColour (theme::accent.withAlpha (0.65f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().removeFromLeft (3.0f), 1.5f);

    if (descriptors.empty() && emptyLabel.isVisible() == false)
    {
        g.setColour (theme::textFaint);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("No editable parameters", getLocalBounds().reduced (10),
                    juce::Justification::centred);
    }
}

void InsertEditorPanel::resized()
{
    auto area = getLocalBounds().reduced (compact ? 8 : 10);

    titleLabel.setBounds (area.removeFromTop (18));
    subtitleLabel.setBounds (area.removeFromTop (14));
    area.removeFromTop (6);

    auto buttons = area.removeFromBottom (compact ? 22 : 26);
    const auto buttonWidth = juce::jmax (58, buttons.getWidth() / 3);

    bypassButton.setBounds (buttons.removeFromLeft (buttonWidth).reduced (2, 0));

    if (resetButton.isVisible())
        resetButton.setBounds (buttons.removeFromLeft (buttonWidth).reduced (2, 0));

    if (pluginButton.isVisible())
        pluginButton.setBounds (buttons.reduced (2, 0));

    area.removeFromBottom (4);

    if (knobs.empty())
    {
        emptyLabel.setBounds (area);
        return;
    }

    emptyLabel.setBounds (area);

    const auto knobWidth = area.getWidth() / juce::jmax (1, (int) knobs.size());

    for (auto& knob : knobs)
        knob->setBounds (area.removeFromLeft (knobWidth));
}
}
