#include "MixerView.h"

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

        if (kind == Kind::track && juce::isPositiveAndBelow (index, (int) state.tracks.size()))
            state.tracks[(size_t) index].name = nameLabel.getText();
        else if (kind == Kind::bus)
            state.buses[(size_t) index].name = nameLabel.getText();
        else
            return;

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
        auto button = std::make_unique<juce::TextButton>();
        button->setColour (juce::TextButton::buttonColourId, theme::surface);
        button->onClick = [this, slot] { showInsertMenu (slot); };
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

juce::String ChannelStripComponent::describeInsert (const InsertSlot& slot) const
{
    if (slot.type == InsertType::none)
        return "+ INS";

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

        if (kind == Kind::track && juce::isPositiveAndBelow (index, (int) state.tracks.size()))
            slot = &state.tracks[(size_t) index].inserts[(size_t) slotIndex];
        else if (kind == Kind::bus && juce::isPositiveAndBelow (slotIndex, constants::maxBusInserts))
            slot = &state.buses[(size_t) index].inserts[(size_t) slotIndex];
        else
            slot = &state.master.inserts[(size_t) slotIndex];

        switch (result)
        {
            case 1: slot->type = InsertType::eq; slot->bypassed = false; slot->p1 = 0.0f; slot->p2 = 1000.0f; slot->p3 = 0.0f; break;
            case 2: slot->type = InsertType::compressor; slot->bypassed = false; slot->p1 = -18.0f; slot->p2 = 3.0f; slot->p3 = 12.0f; break;
            case 3: slot->type = InsertType::delay; slot->bypassed = false; slot->p1 = 350.0f; slot->p2 = 0.35f; slot->p3 = 0.25f; break;
            case 4: slot->type = InsertType::reverb; slot->bypassed = false; slot->p1 = 0.7f; slot->p2 = 0.45f; slot->p3 = 0.22f; break;
            case 5: slot->bypassed = ! slot->bypassed; break;
            case 6: *slot = InsertSlot {}; break;
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
        stripHolder.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }

    for (int i = 0; i < constants::numMixerBuses; ++i)
    {
        auto strip = std::make_unique<ChannelStripComponent> (document, engine,
                                                              ChannelStripComponent::Kind::bus, i);
        strip->onEdited = [this] { if (onProjectEdited) onProjectEdited(); };
        stripHolder.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }

    auto master = std::make_unique<ChannelStripComponent> (document, engine,
                                                           ChannelStripComponent::Kind::master, 0);
    master->onEdited = [this] { if (onProjectEdited) onProjectEdited(); };
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
}

void MixerView::timerCallback()
{
    for (auto& strip : strips)
        strip->refreshLevels();
}

void MixerView::paint (juce::Graphics& g)
{
    g.setColour (theme::windowBg);
    g.fillRect (getLocalBounds());

    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (26));
}
}
