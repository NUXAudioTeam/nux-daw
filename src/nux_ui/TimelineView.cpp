#include "TimelineView.h"

namespace nux
{
//==============================================================================
SequencerGrid::SequencerGrid (ProjectDocument& d, AudioEngine& e)
    : document (d), engine (e)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SequencerGrid::resized()
{
    rowHeight = juce::jlimit (38, 68, getHeight() / juce::jmax (1, (int) document.getState().tracks.size()));
    headerWidth = theme::metrics::trackHeaderWidth;
}

void SequencerGrid::setSelectedTrack (int index)
{
    selectedTrack = index;
    repaint();
}

juce::Rectangle<float> SequencerGrid::getCellBounds (int track, int step) const
{
    const auto numSteps = juce::jmax (1, document.getState().patternLengthSteps);
    const auto gridWidth = (float) juce::jmax (1, getWidth() - headerWidth);
    const auto cellWidth = gridWidth / (float) numSteps;

    return { (float) headerWidth + (float) step * cellWidth,
             (float) rulerHeight + (float) track * (float) rowHeight,
             cellWidth, (float) rowHeight };
}

bool SequencerGrid::hitTestCell (juce::Point<int> position, int& track, int& step) const
{
    if (position.x < headerWidth || position.y < rulerHeight)
        return false;

    const auto numSteps = juce::jmax (1, document.getState().patternLengthSteps);
    const auto gridWidth = (float) juce::jmax (1, getWidth() - headerWidth);
    const auto cellWidth = gridWidth / (float) numSteps;

    track = (position.y - rulerHeight) / rowHeight;
    step  = (int) (((float) position.x - (float) headerWidth) / cellWidth);

    return juce::isPositiveAndBelow (track, (int) document.getState().tracks.size())
        && juce::isPositiveAndBelow (step, numSteps);
}

void SequencerGrid::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto& state = document.getState();

    const auto numSteps = juce::jmax (1, state.patternLengthSteps);
    const auto numTracks = (int) state.tracks.size();
    const auto gridWidth = (float) juce::jmax (1, getWidth() - headerWidth);
    const auto cellWidth = gridWidth / (float) numSteps;

    // Background. The column left of headerWidth belongs to the parent's track
    // headers, so it must stay untouched here.
    const auto gridLeft = (float) headerWidth;
    g.setColour (theme::windowBg);
    g.fillRect (juce::Rectangle<float> (gridLeft, 0.0f, bounds.getRight() - gridLeft, bounds.getHeight()));

    // Ruler.
    auto ruler = bounds.removeFromTop ((float) rulerHeight);
    g.setColour (theme::panelRaised);
    g.fillRect (ruler.withTrimmedLeft (gridLeft));

    for (int step = 0; step < numSteps; ++step)
    {
        const auto x = (float) headerWidth + (float) step * cellWidth;
        const auto isBeat = step % constants::stepsPerBeat == 0;
        const auto isBar = step % (constants::stepsPerBeat * 4) == 0;

        if (isBar)
        {
            g.setColour (theme::textDim);
            g.setFont (theme::font::bold (10.0f));
            g.drawText ("BAR " + juce::String (step / (constants::stepsPerBeat * 4) + 1),
                        juce::Rectangle<float> (x + 4.0f, ruler.getY(), cellWidth * 4.0f, ruler.getHeight()).toNearestInt(),
                        juce::Justification::centredLeft);
        }
        else if (isBeat)
        {
            g.setColour (theme::textFaint);
            g.setFont (theme::font::ui (9.5f));
            g.drawText (juce::String (step / constants::stepsPerBeat + 1),
                        juce::Rectangle<float> (x + 3.0f, ruler.getY(), cellWidth, ruler.getHeight()).toNearestInt(),
                        juce::Justification::centredLeft);
        }
    }

    // Step columns.
    for (int step = 0; step < numSteps; ++step)
    {
        const auto x = (float) headerWidth + (float) step * cellWidth;
        const auto isBeat = step % constants::stepsPerBeat == 0;

        g.setColour (isBeat ? theme::gridLineStrong : theme::gridLine);
        g.drawVerticalLine ((int) x, (float) rulerHeight, (float) getHeight());
    }

    // Track rows with step cells.
    for (int track = 0; track < numTracks; ++track)
    {
        const auto& trackState = state.tracks[(size_t) track];
        const auto rowY = (float) rulerHeight + (float) track * (float) rowHeight;
        const auto isSelected = track == selectedTrack;

        if (isSelected)
        {
            g.setColour (theme::accent.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> ((float) headerWidth, rowY, gridWidth, (float) rowHeight));
        }

        for (int step = 0; step < numSteps; ++step)
        {
            auto cell = getCellBounds (track, step).reduced (cellWidth < 26.0f ? 2.0f : 4.0f, 5.0f);
            const auto isBeat = step % constants::stepsPerBeat == 0;
            const auto enabled = trackState.type == TrackType::sampler
                                 && step < trackState.numSteps
                                 && trackState.steps[(size_t) step].enabled;

            if (enabled)
            {
                const auto velocity = trackState.steps[(size_t) step].velocity;
                auto colour = trackState.getColour().withAlpha (0.55f + 0.45f * velocity);
                g.setColour (colour);
                g.fillRoundedRectangle (cell, 3.0f);
                g.setColour (colour.brighter (0.4f));
                g.drawRoundedRectangle (cell.reduced (0.5f), 3.0f, 1.0f);
            }
            else
            {
                g.setColour (isBeat ? theme::surface.withAlpha (0.75f) : theme::surface.withAlpha (0.4f));
                g.fillRoundedRectangle (cell, 3.0f);
            }
        }

        g.setColour (theme::outline.withAlpha (0.6f));
        g.drawHorizontalLine ((int) (rowY + (float) rowHeight), (float) headerWidth, bounds.getRight());
    }

    // Empty-area shading under the last track.
    const auto usedHeight = (float) rulerHeight + (float) numTracks * (float) rowHeight;

    if (usedHeight < bounds.getBottom())
    {
        g.setColour (theme::windowBg.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> ((float) headerWidth, usedHeight,
                                            gridWidth, bounds.getBottom() - usedHeight));
    }

    // Splitter between headers and grid.
    g.setColour (theme::outline);
    g.drawVerticalLine (headerWidth, 0.0f, (float) getHeight());

    // Playhead.
    const auto position = engine.getTransport().getPositionSteps();
    lastPlayhead = position;

    if (engine.getTransport().isPlaying() || position > 0.0)
    {
        const auto playheadStep = position;
        const auto x = (float) headerWidth + (float) (playheadStep / (double) numSteps) * gridWidth;

        if (x >= (float) headerWidth && x <= (float) getWidth())
        {
            g.setColour (theme::accent.withAlpha (0.9f));
            g.drawVerticalLine ((int) x, (float) rulerHeight - 4.0f, (float) getHeight());

            g.setColour (theme::accent);
            g.fillRoundedRectangle (x - 4.0f, (float) rulerHeight - 6.0f, 8.0f, 8.0f, 2.0f);
        }
    }
}

void SequencerGrid::mouseDown (const juce::MouseEvent& e)
{
    int track = 0, step = 0;

    if (! hitTestCell (e.getPosition(), track, step))
        return;

    auto& state = document.getState();
    auto& trackState = state.tracks[(size_t) track];

    selectedTrack = track;

    if (onTrackSelected)
        onTrackSelected (selectedTrack);

    if (trackState.type != TrackType::sampler)
    {
        repaint();
        return;
    }

    if (e.mods.isRightButtonDown())
    {
        // Right click: velocity-sensitive hit for auditioning.
        if (onPreviewNote)
            onPreviewNote (track, trackState.rootNote);

        repaint();
        return;
    }

    trackState.steps[(size_t) step].enabled = ! trackState.steps[(size_t) step].enabled;

    if (trackState.steps[(size_t) step].enabled)
    {
        trackState.steps[(size_t) step].velocity = 0.9f;

        if (onPreviewNote)
            onPreviewNote (track, trackState.rootNote);
    }

    lastClickStep = step;
    lastClickTrack = track;

    engine.syncSequencerFromState();
    document.markDirty();

    if (onPatternEdited)
        onPatternEdited();

    repaint();
}

void SequencerGrid::mouseDrag (const juce::MouseEvent& e)
{
    int track = 0, step = 0;

    if (! hitTestCell (e.getPosition(), track, step))
        return;

    if (track != lastClickTrack || step == lastClickStep)
        return;

    auto& trackState = document.getState().tracks[(size_t) track];

    if (trackState.type != TrackType::sampler)
        return;

    trackState.steps[(size_t) step].enabled = true;
    trackState.steps[(size_t) step].velocity = 0.9f;

    lastClickStep = step;
    lastClickTrack = track;

    engine.syncSequencerFromState();
    document.markDirty();
    repaint();
}

void SequencerGrid::refreshPlayhead()
{
    const auto position = engine.getTransport().getPositionSteps();

    if (std::abs (position - lastPlayhead) > 0.001 || engine.getTransport().isPlaying())
    {
        lastPlayhead = position;
        repaint();
    }
}

//==============================================================================
TimelineView::TimelineView (ProjectDocument& d, AudioEngine& e)
    : document (d), engine (e), grid (d, e)
{
    addAndMakeVisible (grid);
    addAndMakeVisible (addTrackButton);
    addAndMakeVisible (addPatternButton);
    addAndMakeVisible (presetChooser);
    addAndMakeVisible (masterMeter);

    addTrackButton.setTooltip ("Add a new instrument track");
    addTrackButton.setColour (juce::TextButton::buttonColourId, theme::surface);
    addTrackButton.onClick = [this]
    {
        auto& state = document.getState();

        if ((int) state.tracks.size() >= constants::maxTracks)
            return;

        TrackState track;
        track.id = "trk-" + juce::String (juce::Random::getSystemRandom().nextInt (100000));
        track.name = "Track " + juce::String ((int) state.tracks.size() + 1);
        track.instrumentPreset = juce::Random::getSystemRandom().nextInt (numInstrumentPresets);
        track.setColour (juce::Colour::fromHSV (juce::Random::getSystemRandom().nextFloat(), 0.55f, 0.95f, 1.0f));

        const juce::Colour palette[] = { theme::trackDrums, theme::trackBass, theme::trackAudio,
                                         juce::Colour (0xffc792ea), juce::Colour (0xff7fdbca) };
        track.setColour (palette[(size_t) ((int) state.tracks.size() % 5)]);

        state.tracks.push_back (std::move (track));

        engine.rebuildGraph();
        document.markDirty();
        rebuild();

        if (onProjectEdited)
            onProjectEdited();
    };

    addPatternButton.setTooltip ("Extend the pattern length");
    addPatternButton.onClick = [this]
    {
        auto& state = document.getState();
        state.patternLengthSteps = state.patternLengthSteps >= 32 ? 8 : state.patternLengthSteps * 2;
        engine.getTransport().setLoopLengthSteps (state.patternLengthSteps);
        engine.rebuildGraph();
        document.markDirty();
        grid.repaint();

        if (onProjectEdited)
            onProjectEdited();
    };

    presetChooser.addItemList (getInstrumentPresetNames(), 1);
    presetChooser.setSelectedId (1, juce::dontSendNotification);
    presetChooser.onChange = [this]
    {
        auto& state = document.getState();

        if (! juce::isPositiveAndBelow (selectedTrack, (int) state.tracks.size()))
            return;

        state.tracks[(size_t) selectedTrack].instrumentPreset = juce::jmax (0, presetChooser.getSelectedId() - 1);
        engine.syncSequencerFromState();
        document.markDirty();

        if (onProjectEdited)
            onProjectEdited();
    };

    grid.onTrackSelected = [this] (int track)
    {
        setSelectedTrack (track);
    };

    grid.onPatternEdited = [this]
    {
        document.markDirty();

        if (onProjectEdited)
            onProjectEdited();
    };

    grid.onPreviewNote = [this] (int track, int midiNote)
    {
        engine.triggerPreviewNote (track, midiNote, 0.9f);

        if (onPreviewNote)
            onPreviewNote();
    };

    startTimerHz (30);
    rebuild();
}

TimelineView::~TimelineView()
{
    stopTimer();
}

void TimelineView::timerCallback()
{
    grid.refreshPlayhead();

    for (size_t i = 0; i < meters.size(); ++i)
        if (meters[i] != nullptr)
            meters[i]->setLevel (engine.getTrackPeak ((int) i, 0), engine.getTrackPeak ((int) i, 1));

    masterMeter.setLevel (engine.getMasterPeak (0), engine.getMasterPeak (1));
}

void TimelineView::setSelectedTrack (int index)
{
    selectedTrack = juce::jlimit (0, juce::jmax (0, (int) document.getState().tracks.size() - 1), index);
    grid.setSelectedTrack (selectedTrack);

    const auto preset = document.getState().tracks[(size_t) selectedTrack].instrumentPreset;
    presetChooser.setSelectedId (juce::jlimit (1, (int) numInstrumentPresets, preset + 1), juce::dontSendNotification);

    repaint();
}

void TimelineView::rebuild()
{
    muteButtons.clear();
    soloButtons.clear();
    armButtons.clear();
    volumeKnobs.clear();
    meters.clear();

    const auto numTracks = (int) document.getState().tracks.size();

    for (int i = 0; i < numTracks; ++i)
    {
        auto mute = std::make_unique<ChipButton> ("M", theme::warn);
        mute->setTooltip ("Mute track");
        mute->setToggleState (document.getState().tracks[(size_t) i].muted, juce::dontSendNotification);
        mute->onClick = [this, i, button = mute.get()]
        {
            document.getState().tracks[(size_t) i].muted = button->getToggleState();
            engine.syncMixerFromState();
            document.markDirty();
        };

        auto solo = std::make_unique<ChipButton> ("S", theme::accent);
        solo->setTooltip ("Solo track");
        solo->setToggleState (document.getState().tracks[(size_t) i].soloed, juce::dontSendNotification);
        solo->onClick = [this, i, button = solo.get()]
        {
            document.getState().tracks[(size_t) i].soloed = button->getToggleState();
            engine.syncMixerFromState();
            document.markDirty();
        };

        auto arm = std::make_unique<ChipButton> ("R", theme::recordRed);
        arm->setTooltip ("Arm for recording");
        arm->setToggleState (document.getState().tracks[(size_t) i].recordArmed, juce::dontSendNotification);
        arm->onClick = [this, i, button = arm.get()]
        {
            const auto armed = button->getToggleState();

            for (auto& other : armButtons)
                other->setToggleState (false, juce::dontSendNotification);

            button->setToggleState (armed, juce::dontSendNotification);
            document.getState().tracks[(size_t) i].recordArmed = armed;
            document.markDirty();
        };

        auto knob = std::make_unique<NuxKnob> ("Vol", -60.0, 6.0, 0.5,
                                               document.getState().tracks[(size_t) i].volumeDb);
        knob->setKnobSize (34);
        knob->setSuffix (" dB");
        knob->onValueChange = [this, i] (double value)
        {
            document.getState().tracks[(size_t) i].volumeDb = (float) value;
            engine.syncMixerFromState();
            document.markDirty();
        };

        auto meter = std::make_unique<LevelMeter> (false);

        addAndMakeVisible (*mute);
        addAndMakeVisible (*solo);
        addAndMakeVisible (*arm);
        addAndMakeVisible (*knob);
        addAndMakeVisible (*meter);

        muteButtons.push_back (std::move (mute));
        soloButtons.push_back (std::move (solo));
        armButtons.push_back (std::move (arm));
        volumeKnobs.push_back (std::move (knob));
        meters.push_back (std::move (meter));
    }

    setSelectedTrack (juce::jmin (selectedTrack, juce::jmax (0, numTracks - 1)));
    resized();
    repaint();
}

void TimelineView::resized()
{
    auto area = getLocalBounds();

    // Toolbar on top of the track headers.
    auto toolbar = area.removeFromTop (34).reduced (8, 4);
    addTrackButton.setBounds (toolbar.removeFromLeft (76));
    toolbar.removeFromLeft (6);
    addPatternButton.setBounds (toolbar.removeFromLeft (84));
    toolbar.removeFromLeft (10);
    presetChooser.setBounds (toolbar.removeFromLeft (juce::jmin (150, toolbar.getWidth())));
    toolbar.removeFromLeft (8);
    masterMeter.setBounds (toolbar.removeFromLeft (46).reduced (6, 2));

    headerArea = area.removeFromLeft (theme::metrics::trackHeaderWidth);

    grid.setBounds (getLocalBounds().withTrimmedTop (34));

    // Track header rows must line up exactly with the grid rows.
    const auto numTracks = (int) muteButtons.size();
    const auto gridHeight = getHeight() - 34 - 0 - theme::metrics::rulerHeight;
    const auto rowHeight = juce::jlimit (38, 68, gridHeight / juce::jmax (1, numTracks));

    for (int i = 0; i < numTracks; ++i)
    {
        auto row = juce::Rectangle<int> (4, 34 + theme::metrics::rulerHeight + i * rowHeight,
                                         theme::metrics::trackHeaderWidth - 8, rowHeight).reduced (2);

        auto controls = row.removeFromRight (44);
        muteButtons[(size_t) i]->setBounds (controls.removeFromTop (row.getHeight() / 2).reduced (1));
        soloButtons[(size_t) i]->setBounds (controls.reduced (1));

        auto right = row.removeFromRight (30).reduced (2);
        meters[(size_t) i]->setBounds (right);

        volumeKnobs[(size_t) i]->setBounds (row.removeFromRight (46).reduced (0, 2));
    }
}

void TimelineView::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().withHeight (34));
    g.setColour (theme::outline);
    g.drawHorizontalLine (33, 0.0f, (float) getWidth());

    auto& state = document.getState();
    const auto numTracks = (int) state.tracks.size();
    const auto gridHeight = getHeight() - 34 - theme::metrics::rulerHeight;
    const auto rowHeight = juce::jlimit (38, 68, gridHeight / juce::jmax (1, numTracks));

    // Header background.
    g.setColour (theme::panel);
    g.fillRect (juce::Rectangle<int> (0, 34, theme::metrics::trackHeaderWidth, getHeight() - 34));

    // Ruler corner.
    g.setColour (theme::panelRaised);
    g.fillRect (juce::Rectangle<int> (0, 34, theme::metrics::trackHeaderWidth, theme::metrics::rulerHeight));

    g.setFont (theme::font::bold (10.0f));
    g.setColour (theme::textFaint);
    g.drawText ("TRACKS", juce::Rectangle<int> (10, 34, 120, theme::metrics::rulerHeight),
                juce::Justification::centredLeft);

    for (int i = 0; i < numTracks; ++i)
    {
        const auto rowY = 34 + theme::metrics::rulerHeight + i * rowHeight;
        drawTrackHeader (g, { 0, rowY, theme::metrics::trackHeaderWidth, rowHeight }, i, i == selectedTrack);
    }

    // Empty area below the last track.
    const auto usedBottom = 34 + theme::metrics::rulerHeight + numTracks * rowHeight;

    if (usedBottom < getHeight())
    {
        g.setColour (theme::windowBg.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<int> (0, usedBottom, theme::metrics::trackHeaderWidth,
                                          getHeight() - usedBottom));
    }

    g.setColour (theme::outline);
    g.drawVerticalLine (theme::metrics::trackHeaderWidth - 1, 34.0f, (float) getHeight());
}

void TimelineView::drawTrackHeader (juce::Graphics& g, juce::Rectangle<int> bounds, int trackIndex, bool selected)
{
    auto& track = document.getState().tracks[(size_t) trackIndex];

    // Row background.
    g.setColour (selected ? theme::surface
                          : (trackIndex % 2 == 0 ? theme::panel : theme::panel.darker (0.15f)));
    g.fillRect (bounds);

    if (selected)
    {
        g.setColour (track.getColour().withAlpha (0.10f));
        g.fillRect (bounds);
    }

    // Colour tab on the left edge.
    g.setColour (track.getColour());
    g.fillRect (juce::Rectangle<int> (bounds.getX(), bounds.getY(), 4, bounds.getHeight()));

    g.setColour (theme::outline.withAlpha (0.7f));
    g.drawHorizontalLine (bounds.getBottom(), 4.0f, (float) bounds.getRight());

    // Name + instrument. Reserve the right hand side for the knob and the M/S buttons.
    auto textArea = bounds.withTrimmedLeft (12).withTrimmedRight (132);

    if (textArea.getWidth() > 20 && textArea.getHeight() > 16)
    {
        auto nameArea = textArea.removeFromTop (juce::jmax (14, textArea.getHeight() / 2));

        g.setFont (theme::font::bold (12.5f));
        g.setColour (theme::text);
        g.drawFittedText (track.name, nameArea, juce::Justification::centredLeft, 1, 0.7f);

        g.setFont (theme::font::ui (10.0f));
        g.setColour (theme::textFaint);

        const auto typeText = track.type == TrackType::audio
                                ? juce::String ("AUDIO")
                                : getInstrumentPresetNames()[juce::jlimit (0, (int) numInstrumentPresets - 1,
                                                                          track.instrumentPreset)];

        g.drawFittedText (typeText, textArea.removeFromTop (juce::jmax (11, textArea.getHeight() / 2)),
                          juce::Justification::centredLeft, 1, 0.7f);
    }

    // Mini step preview strip along the bottom of the header.
    if (track.type == TrackType::sampler && bounds.getHeight() >= 44)
    {
        const auto stripWidth = juce::jmax (40, bounds.getWidth() - 140);
        auto strip = juce::Rectangle<float> (12.0f, (float) bounds.getBottom() - 9.0f,
                                             (float) stripWidth, 4.0f);
        const auto numSteps = juce::jmax (1, juce::jmin (track.numSteps, 16));
        const auto cellWidth = strip.getWidth() / (float) numSteps;

        for (int s = 0; s < numSteps; ++s)
        {
            g.setColour (track.steps[(size_t) s].enabled ? track.getColour() : theme::surface);
            g.fillRect (strip.withWidth (cellWidth).translated ((float) s * cellWidth, 0.0f).reduced (0.5f, 0.0f));
        }
    }
}

void TimelineView::mouseDown (const juce::MouseEvent& e)
{
    const auto numTracks = (int) document.getState().tracks.size();
    const auto gridHeight = getHeight() - 34 - theme::metrics::rulerHeight;
    const auto rowHeight = juce::jlimit (38, 68, gridHeight / juce::jmax (1, numTracks));

    if (e.x < theme::metrics::trackHeaderWidth
        && e.y >= 34 + theme::metrics::rulerHeight)
    {
        const auto trackIndex = (e.y - 34 - theme::metrics::rulerHeight) / rowHeight;

        if (juce::isPositiveAndBelow (trackIndex, numTracks))
        {
            setSelectedTrack (trackIndex);

            if (e.mods.isRightButtonDown())
                showTrackMenu (trackIndex);
        }
    }
}

void TimelineView::showTrackMenu (int trackIndex)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Track");

    juce::PopupMenu presetMenu;

    for (int i = 0; i < numInstrumentPresets; ++i)
        presetMenu.addItem (100 + i, getInstrumentPresetNames()[i], true,
                            document.getState().tracks[(size_t) trackIndex].instrumentPreset == i);

    menu.addSubMenu ("Instrument", presetMenu);

    juce::PopupMenu busMenu;

    for (int i = 0; i < constants::numMixerBuses; ++i)
        busMenu.addItem (200 + i, "Bus " + juce::String (i + 1), true,
                         document.getState().tracks[(size_t) trackIndex].busIndex == i);

    menu.addSubMenu ("Output bus", busMenu);
    menu.addItem (1, "Rename track...");
    menu.addItem (2, "Duplicate track");
    menu.addItem (3, "Delete track");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, trackIndex] (int result)
    {
        if (result == 0 || ! juce::isPositiveAndBelow (trackIndex, (int) document.getState().tracks.size()))
            return;

        auto& tracks = document.getState().tracks;

        if (result >= 100 && result < 100 + numInstrumentPresets)
        {
            tracks[(size_t) trackIndex].instrumentPreset = result - 100;
        }
        else if (result >= 200 && result < 200 + constants::numMixerBuses)
        {
            tracks[(size_t) trackIndex].busIndex = result - 200;
        }
        else if (result == 1)
        {
            auto* aw = new juce::AlertWindow ("Rename track", "", juce::MessageBoxIconType::NoIcon);
            aw->addTextEditor ("name", tracks[(size_t) trackIndex].name, "Name");
            aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, trackIndex] (int r)
            {
                if (r == 1 && juce::isPositiveAndBelow (trackIndex, (int) document.getState().tracks.size()))
                {
                    auto name = aw->getTextEditorContents ("name");

                    if (name.isNotEmpty())
                        document.getState().tracks[(size_t) trackIndex].name = name;

                    document.markDirty();
                    repaint();
                }
            }), true);
            return;
        }
        else if (result == 2)
        {
            auto copy = tracks[(size_t) trackIndex];
            copy.id = "trk-" + juce::String (juce::Random::getSystemRandom().nextInt (100000));
            copy.name += " copy";

            if ((int) tracks.size() < constants::maxTracks)
                tracks.insert (tracks.begin() + trackIndex + 1, copy);
        }
        else if (result == 3)
        {
            if (tracks.size() > 1)
                tracks.erase (tracks.begin() + trackIndex);
        }

        engine.rebuildGraph();
        document.markDirty();
        rebuild();

        if (onProjectEdited)
            onProjectEdited();
    });
}
}
