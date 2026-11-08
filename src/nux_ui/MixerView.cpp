#include "MixerView.h"
#include "PluginPicker.h"
#include "../nux_core/InsertParameters.h"

namespace nux
{
//==============================================================================
ChannelStripComponent::ChannelStripComponent (ProjectDocument& d, AudioEngine& e,
                                              Kind k, int i)
    : document (d), engine (e), kind (k), index (i)
{
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (fader);
    addAndMakeVisible (panKnob);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (meter);

    nameLabel.setFont (theme::font::bold (11.5f));
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, theme::text);
    nameLabel.setEditable (kind != Kind::master, false, false);

    nameLabel.onTextChange = [this]
    {
        auto& state = document.getState();

        const auto isTrack = kind == Kind::track && juce::isPositiveAndBelow (index, (int) state.tracks.size());
        const auto isBus   = kind == Kind::bus && juce::isPositiveAndBelow (index, constants::numMixerBuses);

        if (! isTrack && ! isBus)
            return;

        // Record the state before renaming, so undo restores the old name.
        document.beginEdit ("track rename", "Rename a track");

        if (isTrack)
            state.tracks[(size_t) index].name = nameLabel.getText();
        else
            state.buses[(size_t) index].name = nameLabel.getText();

        document.markDirty();

        if (onEdited)
            onEdited();
    };

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.setRange (-60.0, 6.0, 0.1);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setDoubleClickReturnValue (true, 0.0);

    fader.onValueChange = [this]
    {
        auto& state = document.getState();
        const auto value = (float) fader.getValue();

        document.beginEdit ("fader", "Change the volume");

        if (kind == Kind::track && juce::isPositiveAndBelow (index, (int) state.tracks.size()))
            state.tracks[(size_t) index].volumeDb = value;
        else if (kind == Kind::bus)
            state.buses[(size_t) index].volumeDb = value;
        else
            state.master.volumeDb = value;

        engine.syncMixerFromState();
        document.markDirty();
        repaint();
    };

    panKnob.setKnobSize (38);
    panKnob.setSuffix ("");

    panKnob.onValueChange = [this] (double value)
    {
        auto& state = document.getState();

        document.beginEdit ("pan", "Change the pan");

        if (kind == Kind::track && juce::isPositiveAndBelow (index, (int) state.tracks.size()))
            state.tracks[(size_t) index].pan = (float) value;
        else if (kind == Kind::bus)
            state.buses[(size_t) index].pan = (float) value;

        engine.syncMixerFromState();
        document.markDirty();
    };

    muteButton.onClick = [this]
    {
        auto& state = document.getState();
        const auto muted = muteButton.getToggleState();

        document.beginEdit ("mute", "Toggle mute");

        if (kind == Kind::track && juce::isPositiveAndBelow (index, (int) state.tracks.size()))
            state.tracks[(size_t) index].muted = muted;
        else if (kind == Kind::bus)
            state.buses[(size_t) index].muted = muted;

        engine.syncMixerFromState();
        document.markDirty();
    };

    if (kind == Kind::track)
    {
        addAndMakeVisible (soloButton);

        soloButton.onClick = [this]
        {
            auto& state = document.getState();

            document.beginEdit ("solo", "Toggle solo");

            if (juce::isPositiveAndBelow (index, (int) state.tracks.size()))
                state.tracks[(size_t) index].soloed = soloButton.getToggleState();

            engine.syncMixerFromState();
            document.markDirty();
        };
    }

    if (kind == Kind::bus)
    {
        addAndMakeVisible (delaySend);
        addAndMakeVisible (reverbSend);

        delaySend.setKnobSize (32);
        reverbSend.setKnobSize (32);

        delaySend.onValueChange = [this] (double value)
        {
            const auto send = juce::jlimit (0.0f, 1.0f, (float) value);
            auto& strips = engine.getBusSendLevels();

            if (juce::isPositiveAndBelow (index, (int) strips.size()))
            {
                strips[(size_t) index].store (send);
                engine.applyBusSendLevels();
            }
        };

        reverbSend.onValueChange = [this] (double value)
        {
            const auto send = juce::jlimit (0.0f, 1.0f, (float) value);
            auto& strips = engine.getBusReverbSendLevels();

            if (juce::isPositiveAndBelow (index, (int) strips.size()))
            {
                strips[(size_t) index].store (send);
                engine.applyBusSendLevels();
            }
        };
    }

    // Insert slots.
    const int numInserts = kind == Kind::bus ? constants::maxBusInserts : constants::maxTrackInserts;

    for (int slot = 0; slot < numInserts; ++slot)
    {
        // A small Button subclass so a double-click can open the parameter editor
        // while a single click still opens the insert menu.
        struct InsertButton final : public juce::TextButton
        {
            std::function<void()> onDoubleClick;

            void mouseDoubleClick (const juce::MouseEvent& e) override
            {
                juce::ignoreUnused (e);

                if (onDoubleClick)
                    onDoubleClick();
            }
        };

        auto button = std::make_unique<InsertButton>();
        button->setColour (juce::TextButton::buttonColourId, theme::surface);
        button->onClick = [this, slot]
        {
            if (onInsertClicked)
                onInsertClicked (slot);
            else
                showInsertMenu (slot);
        };
        button->onDoubleClick = [this, slot]
        {
            if (onInsertDoubleClicked)
                onInsertDoubleClicked (slot, getStripDisplayName());
        };
        addAndMakeVisible (*button);
        insertButtons.push_back (std::move (button));
    }

    syncFromState();
}

void ChannelStripComponent::setSelected (bool shouldBeSelected)
{
    selected = shouldBeSelected;
    repaint();
}

void ChannelStripComponent::syncFromState()
{
    auto& state = document.getState();

    const InsertSlot* inserts = nullptr;
    juce::Colour accent = theme::accent;

    if (kind == Kind::track && juce::isPositiveAndBelow (index, (int) state.tracks.size()))
    {
        const auto& track = state.tracks[(size_t) index];
        nameLabel.setText (track.name, juce::dontSendNotification);
        fader.setValue (track.volumeDb, juce::dontSendNotification);
        panKnob.setValue (track.pan, juce::dontSendNotification);
        muteButton.setToggleState (track.muted, juce::dontSendNotification);
        soloButton.setToggleState (track.soloed, juce::dontSendNotification);
        inserts = track.inserts.data();
        accent = track.getColour();
    }
    else if (kind == Kind::bus)
    {
        const auto& bus = state.buses[(size_t) index];
        nameLabel.setText (bus.name, juce::dontSendNotification);
        fader.setValue (bus.volumeDb, juce::dontSendNotification);
        panKnob.setValue (bus.pan, juce::dontSendNotification);
        muteButton.setToggleState (bus.muted, juce::dontSendNotification);
        inserts = bus.inserts.data();
        accent = juce::Colour (0xff8fa0b4);
    }
    else
    {
        nameLabel.setText ("MASTER", juce::dontSendNotification);
        fader.setValue (state.master.volumeDb, juce::dontSendNotification);
        panKnob.setValue (0.0, juce::dontSendNotification);
        inserts = state.master.inserts.data();
        accent = theme::accent;
    }

    panKnob.setAccentColour (accent);

    for (size_t slot = 0; slot < insertButtons.size() && inserts != nullptr; ++slot)
    {
        const auto text = describeInsert (inserts[slot]);
        insertButtons[slot]->setButtonText (text);
        insertButtons[slot]->setColour (juce::TextButton::buttonColourId,
                                        inserts[slot].type == InsertType::none ? theme::panel
                                                                              : theme::surfaceHover);
        insertButtons[slot]->setColour (juce::TextButton::textColourOffId,
                                        inserts[slot].type == InsertType::none ? theme::textFaint
                                                                              : theme::accent);
    }

    repaint();
}

juce::String ChannelStripComponent::getStripDisplayName() const
{
    if (kind == Kind::master)
        return "MASTER";

    auto& state = document.getState();

    if (kind == Kind::track && juce::isPositiveAndBelow (index, (int) state.tracks.size()))
        return state.tracks[(size_t) index].name;

    if (kind == Kind::bus && juce::isPositiveAndBelow (index, constants::numMixerBuses))
        return state.buses[(size_t) index].name;

    return "Channel";
}

juce::String ChannelStripComponent::describeInsert (const InsertSlot& slot) const
{
    if (slot.type == InsertType::none)
        return "+ INS";

    if (slot.type == InsertType::plugin)
    {
        auto name = slot.plugin.name.isNotEmpty() ? slot.plugin.name : juce::String ("PLUGIN");

        if (name.length() > 12)
            name = name.substring (0, 11) + juce::String::charToString ((juce::juce_wchar) 0x2026);

        return slot.bypassed ? name + " (off)" : name;
    }

    juce::String name;

    switch (slot.type)
    {
        case InsertType::eq:         name = "EQ"; break;
        case InsertType::compressor: name = "COMP"; break;
        case InsertType::delay:      name = "DELAY"; break;
        case InsertType::reverb:     name = "REVERB"; break;
        case InsertType::none:
        default:                     return "+ INS";
    }

    return slot.bypassed ? name + " (off)" : name;
}

void ChannelStripComponent::showInsertMenu (int slotIndex)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Insert " + juce::String (slotIndex + 1));
    menu.addItem (7, "Plugin...");

    const auto isPluginSlot = [this, slotIndex]
    {
        auto& state = document.getState();

        if (kind == Kind::track && juce::isPositiveAndBelow (index, (int) state.tracks.size()))
            return state.tracks[(size_t) index].inserts[(size_t) slotIndex].type == InsertType::plugin;

        if (kind == Kind::bus && juce::isPositiveAndBelow (slotIndex, constants::maxBusInserts))
            return state.buses[(size_t) index].inserts[(size_t) slotIndex].type == InsertType::plugin;

        return state.master.inserts[(size_t) slotIndex].type == InsertType::plugin;
    }();

    menu.addItem (8, "Open plugin window", isPluginSlot);
    menu.addSeparator();
    menu.addItem (1, "Equaliser");
    menu.addItem (2, "Compressor");
    menu.addItem (3, "Delay");
    menu.addItem (4, "Reverb");
    menu.addSeparator();
    menu.addItem (5, "Bypass / enable");
    menu.addItem (6, "Remove");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (insertButtons[(size_t) slotIndex].get()),
                        [this, slotIndex] (int result)
    {
        if (result == 0)
            return;

        auto& state = document.getState();
        InsertSlot* slot = nullptr;

        document.beginEdit ("insert change", "Edit an insert");

        if (kind == Kind::track && juce::isPositiveAndBelow (index, (int) state.tracks.size()))
            slot = &state.tracks[(size_t) index].inserts[(size_t) slotIndex];
        else if (kind == Kind::bus && juce::isPositiveAndBelow (slotIndex, constants::maxBusInserts))
            slot = &state.buses[(size_t) index].inserts[(size_t) slotIndex];
        else
            slot = &state.master.inserts[(size_t) slotIndex];

        if (result == 7)
        {
            // Load a plugin into this slot. The description is stored in the project
            // first, then the engine instantiates it from there, so the model stays
            // the single source of truth.
            const auto kindCopy = kind;
            const auto indexCopy = index;
            const auto slotCopy = slotIndex;
            auto* enginePtr = &engine;
            auto* documentPtr = &document;
            auto* self = this;

            documentPtr->beginEdit ("insert plugin", "Load a plugin");

            auto* manager = engine.getPluginManager();
            jassert (manager != nullptr);

            if (manager == nullptr)
                return;

            showPluginPicker (*manager,
                              false,
                              [enginePtr, documentPtr, self, kindCopy, indexCopy, slotCopy]
                              (const juce::PluginDescription& description)
            {
                auto& state = documentPtr->getState();
                InsertSlot* modelSlot = nullptr;

                if (kindCopy == Kind::track && juce::isPositiveAndBelow (indexCopy, (int) state.tracks.size()))
                    modelSlot = &state.tracks[(size_t) indexCopy].inserts[(size_t) slotCopy];
                else if (kindCopy == Kind::bus && juce::isPositiveAndBelow (slotCopy, constants::maxBusInserts))
                    modelSlot = &state.buses[(size_t) indexCopy].inserts[(size_t) slotCopy];
                else
                    modelSlot = &state.master.inserts[(size_t) slotCopy];

                PluginReference reference;
                reference.name         = description.name;
                reference.manufacturer = description.manufacturerName;
                reference.format       = description.pluginFormatName;
                reference.isInstrument = description.isInstrument;

                if (auto xml = description.createXml())
                    reference.descriptionXml = xml->toString (juce::XmlElement::TextFormat().singleLine());

                reference.stateSaved = false;

                modelSlot->type = InsertType::plugin;
                modelSlot->bypassed = false;
                modelSlot->plugin = reference;
                modelSlot->pluginStateBase64.clear();

                const auto stripKind = kindCopy == Kind::track ? AudioEngine::StripKind::track
                                     : kindCopy == Kind::bus   ? AudioEngine::StripKind::bus
                                                               : AudioEngine::StripKind::master;

                const auto error = enginePtr->loadInsertPlugin (stripKind, indexCopy, slotCopy);

                enginePtr->syncMixerFromState();
                documentPtr->markDirty();
                self->syncFromState();

                if (self->onEdited)
                    self->onEdited();

                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Could not load " + description.name,
                                                            error);
            });

            return;
        }

        if (result == 8)
        {
            const auto stripKind = kind == Kind::track ? AudioEngine::StripKind::track
                                 : kind == Kind::bus   ? AudioEngine::StripKind::bus
                                                       : AudioEngine::StripKind::master;

            if (auto* plugin = engine.getInsertPluginSlot (stripKind, index, slotIndex))
                if (auto* window = PluginEditorWindow::createAndShow (*plugin))
                    juce::ignoreUnused (window);

            return;
        }

        switch (result)
        {
            // Defaults come from the shared descriptors so the menu, the editor and
            // the DSP layer can never disagree about what a fresh insert starts at.
            case 1: applyInsertDefaults (*slot, InsertType::eq);         break;
            case 2: applyInsertDefaults (*slot, InsertType::compressor); break;
            case 3: applyInsertDefaults (*slot, InsertType::delay);      break;
            case 4: applyInsertDefaults (*slot, InsertType::reverb);     break;
            case 5: slot->bypassed = ! slot->bypassed; break;
            case 6:
            {
                const auto stripKind = kind == Kind::track ? AudioEngine::StripKind::track
                                     : kind == Kind::bus   ? AudioEngine::StripKind::bus
                                                           : AudioEngine::StripKind::master;

                engine.unloadInsertPlugin (stripKind, index, slotIndex);
                *slot = InsertSlot {};
                break;
            }
            default: return;
        }

        engine.syncMixerFromState();
        document.markDirty();
        syncFromState();

        if (onEdited)
            onEdited();
    });
}

void ChannelStripComponent::refreshLevels()
{
    if (kind == Kind::track)
        meter.setLevel (engine.getTrackPeak (index, 0), engine.getTrackPeak (index, 1));
    else if (kind == Kind::master)
        meter.setLevel (engine.getMasterPeak (0), engine.getMasterPeak (1));
}

void ChannelStripComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    auto& state = document.getState();
    juce::Colour accent = theme::accent;

    if (kind == Kind::track && juce::isPositiveAndBelow (index, (int) state.tracks.size()))
        accent = state.tracks[(size_t) index].getColour();
    else if (kind == Kind::bus)
        accent = juce::Colour (0xff8fa0b4);
    else
        accent = theme::accent;

    theme::NuxLookAndFeel::drawPanel (g, bounds, selected ? theme::surface : theme::panelRaised,
                                      selected ? accent.withAlpha (0.65f) : theme::outline, 6.0f);

    // Colour header band.
    g.setColour (accent.withAlpha (0.75f));
    g.fillRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), 3.0f, 2.0f);

    // Insert area separator.
    auto insertArea = juce::Rectangle<float> (bounds.getX() + 6.0f, bounds.getY() + 56.0f,
                                             bounds.getWidth() - 12.0f,
                                             (float) insertButtons.size() * 22.0f + 4.0f);
    g.setColour (theme::windowBg.withAlpha (0.45f));
    g.fillRoundedRectangle (insertArea, 4.0f);

    // Fader scale ticks.
    auto faderBounds = fader.getBounds().toFloat();

    for (int i = 0; i <= 6; ++i)
    {
        const auto y = faderBounds.getY() + faderBounds.getHeight() * (float) i / 6.0f;
        g.setColour (theme::outline);
        g.drawHorizontalLine ((int) y, faderBounds.getRight() + 2.0f, faderBounds.getRight() + 6.0f);
    }

    if (kind == Kind::master)
    {
        g.setColour (theme::accent.withAlpha (0.35f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.2f);
    }
}

void ChannelStripComponent::resized()
{
    auto area = getLocalBounds().reduced (6, 8);

    nameLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);

    for (auto& button : insertButtons)
    {
        button->setBounds (area.removeFromTop (20).reduced (0, 1));
    }

    area.removeFromTop (6);
    panKnob.setBounds (area.removeFromTop (78));

    if (kind == Kind::bus)
    {
        auto sends = area.removeFromTop (76);
        delaySend.setBounds (sends.removeFromLeft (sends.getWidth() / 2));
        reverbSend.setBounds (sends);
        area.removeFromTop (4);
    }

    area.removeFromTop (4);

    auto bottom = area.removeFromBottom (24);
    muteButton.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (1));
    if (kind == Kind::track)
        soloButton.setBounds (bottom.reduced (1));

    auto faderArea = area;
    meter.setBounds (faderArea.removeFromRight (14).reduced (2, 12));
    fader.setBounds (faderArea.reduced (6, 8));
}

//==============================================================================
MixerView::MixerView (ProjectDocument& d, AudioEngine& e)
    : document (d), engine (e)
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (10);

    addAndMakeVisible (titleLabel);
    titleLabel.setFont (theme::font::bold (12.0f));
    titleLabel.setColour (juce::Label::textColourId, theme::textDim);

    startTimerHz (24);
    rebuild();
}

MixerView::~MixerView()
{
    stopTimer();
}

void MixerView::rebuild()
{
    strips.clear();

    auto& state = document.getState();

    for (int i = 0; i < (int) state.tracks.size(); ++i)
    {
        auto strip = std::make_unique<ChannelStripComponent> (document, engine,
                                                              ChannelStripComponent::Kind::track, i);
        strip->onEdited = [this] { if (onProjectEdited) onProjectEdited(); };
        strip->onInsertDoubleClicked = [this, i] (int slotIndex, juce::String stripName)
        {
            showInsertEditor (ChannelStripComponent::Kind::track, i, slotIndex, stripName);
        };
        stripHolder.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }

    for (int i = 0; i < constants::numMixerBuses; ++i)
    {
        auto strip = std::make_unique<ChannelStripComponent> (document, engine,
                                                              ChannelStripComponent::Kind::bus, i);
        strip->onEdited = [this] { if (onProjectEdited) onProjectEdited(); };
        strip->onInsertDoubleClicked = [this, i] (int slotIndex, juce::String stripName)
        {
            showInsertEditor (ChannelStripComponent::Kind::bus, i, slotIndex, stripName);
        };
        stripHolder.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }

    auto master = std::make_unique<ChannelStripComponent> (document, engine,
                                                           ChannelStripComponent::Kind::master, 0);
    master->onEdited = [this] { if (onProjectEdited) onProjectEdited(); };
    master->onInsertDoubleClicked = [this] (int slotIndex, juce::String stripName)
    {
        showInsertEditor (ChannelStripComponent::Kind::master, 0, slotIndex, stripName);
    };
    stripHolder.addAndMakeVisible (*master);
    strips.push_back (std::move (master));

    titleLabel.setText ("MIXER  -  " + juce::String (state.tracks.size()) + " TRACKS, "
                        + juce::String (constants::numMixerBuses) + " BUSES",
                        juce::dontSendNotification);

    resized();
    repaint();
}

void MixerView::resized()
{
    auto area = getLocalBounds();
    titleLabel.setBounds (area.removeFromTop (26).reduced (10, 4));

    viewport.setBounds (area);

    const auto stripWidth = theme::metrics::mixerStripWidth;
    const auto stripHeight = juce::jmax (300, area.getHeight() - 16);

    stripHolder.setSize (juce::jmax ((int) strips.size() * stripWidth + 8, area.getWidth()), stripHeight);

    for (size_t i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (4 + (int) i * stripWidth, 4, stripWidth - 4, stripHeight - 8);

    if (insertEditor != nullptr)
    {
        // Floats over the lower right of the mixer, above the strips but inside the view.
        const auto width = juce::jmin (300, juce::jmax (220, area.getWidth() / 3));
        const auto height = juce::jmin (190, juce::jmax (150, area.getHeight() / 2));

        insertEditor->setBounds (area.getRight() - width - 12,
                                 area.getBottom() - height - 12,
                                 width, height);
        insertEditor->toFront (false);
    }
}

void MixerView::timerCallback()
{
    for (auto& strip : strips)
        strip->refreshLevels();

    // If the edited insert changed type (a plugin loaded, the slot cleared, an undo)
    // the editor has to follow, otherwise its knobs describe the wrong effect.
    if (insertEditor != nullptr)
    {
        auto& state = document.getState();
        const InsertSlot* slot = nullptr;

        if (editorKind == ChannelStripComponent::Kind::track
            && juce::isPositiveAndBelow (editorStripIndex, (int) state.tracks.size()))
            slot = &state.tracks[(size_t) editorStripIndex].inserts[(size_t) editorSlotIndex];
        else if (editorKind == ChannelStripComponent::Kind::bus
                 && juce::isPositiveAndBelow (editorSlotIndex, constants::maxBusInserts))
            slot = &state.buses[(size_t) editorStripIndex].inserts[(size_t) editorSlotIndex];
        else
            slot = &state.master.inserts[(size_t) editorSlotIndex];

        // refreshFromState() is cheap when nothing changed: it only rebuilds the
        // controls when the effect type or the knob count differs.
        insertEditor->refreshFromState();
        juce::ignoreUnused (slot);
    }
}

void MixerView::paint (juce::Graphics& g)
{
    g.setColour (theme::windowBg);
    g.fillRect (getLocalBounds());

    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (26));
}

//==============================================================================
void MixerView::showInsertEditor (ChannelStripComponent::Kind kind, int stripIndex, int slotIndex,
                                  const juce::String& stripName)
{
    const auto engineKind = kind == ChannelStripComponent::Kind::track ? AudioEngine::StripKind::track
                          : kind == ChannelStripComponent::Kind::bus   ? AudioEngine::StripKind::bus
                                                                       : AudioEngine::StripKind::master;

    InsertEditorPanel::Target target;
    target.kind = engineKind;
    target.stripIndex = stripIndex;
    target.slotIndex = slotIndex;
    target.stripName = stripName;

    editorKind = kind;
    editorStripIndex = stripIndex;
    editorSlotIndex = slotIndex;

    insertEditor = std::make_unique<InsertEditorPanel> (document, engine, target);
    insertEditor->onEdited = [this] { if (onProjectEdited) onProjectEdited(); };
    addAndMakeVisible (*insertEditor);

    resized();
    repaint();
}

void MixerView::hideInsertEditor()
{
    insertEditor.reset();
    editorSlotIndex = -1;
    resized();
    repaint();
}

ChannelStripComponent* MixerView::getEditorSourceStrip() const
{
    if (editorSlotIndex < 0)
        return nullptr;

    const auto isTrack = editorKind == ChannelStripComponent::Kind::track;
    const auto wanted = isTrack ? editorStripIndex : editorStripIndex + (int) document.getState().tracks.size();

    if (! juce::isPositiveAndBelow (wanted, (int) strips.size()))
        return nullptr;

    return strips[(size_t) wanted].get();
}
}
