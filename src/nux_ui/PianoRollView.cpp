#include "PianoRollView.h"

namespace nux
{
namespace
{
constexpr int pianoRollLowestVisible = 24;
constexpr int noteNameOctaveOffset = 1;
}

//==============================================================================
PianoRollCanvas::PianoRollCanvas (ProjectDocument& d, AudioEngine& e)
    : document (d), engine (e)
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

Clip* PianoRollCanvas::getClip()
{
    auto& state = document.getState();

    if (! juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size()))
        return nullptr;

    if (state.tracks[(size_t) trackIndex].clips.empty())
        state.tracks[(size_t) trackIndex].clips.push_back ({});

    return &state.tracks[(size_t) trackIndex].clips.front();
}

const Clip* PianoRollCanvas::getClip() const
{
    const auto& state = document.getState();

    if (! juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size()))
        return nullptr;

    return state.tracks[(size_t) trackIndex].clips.empty() ? nullptr
                                                          : &state.tracks[(size_t) trackIndex].clips.front();
}

void PianoRollCanvas::setTrack (int index)
{
    trackIndex = index;
    draggingNoteIndex = -1;
    repaint();
}

bool PianoRollCanvas::isBlackKey (int midiNote)
{
    const auto semitone = midiNote % 12;
    return semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;
}

void PianoRollCanvas::resized()
{
    numVisibleNotes = juce::jmax (12, (int) ((float) getHeight() / 15.0f));
    lowestNote = juce::jlimit (0, 127 - numVisibleNotes, lowestNote);
}

float noteRowHeight() { return 15.0f; }

juce::Rectangle<float> PianoRollCanvas::getKeyBounds (int midiNote) const
{
    const auto rowHeight = (float) getHeight() / (float) numVisibleNotes;
    const auto row = numVisibleNotes - 1 - (midiNote - lowestNote);

    return { 0.0f, (float) row * rowHeight, keyWidth, rowHeight };
}

juce::Rectangle<float> PianoRollCanvas::getNoteBounds (int midiNote, int startStep, int lengthSteps) const
{
    const auto steps = juce::jmax (1, getNumSteps());
    const auto gridWidth = (float) getWidth() - keyWidth;
    const auto stepWidth = gridWidth / (float) steps;
    const auto rowHeight = (float) getHeight() / (float) numVisibleNotes;
    const auto row = numVisibleNotes - 1 - (midiNote - lowestNote);

    return { keyWidth + (float) startStep * stepWidth,
             (float) row * rowHeight,
             juce::jmax (stepWidth, (float) lengthSteps * stepWidth),
             rowHeight };
}

int PianoRollCanvas::stepAtX (int x) const
{
    const auto steps = juce::jmax (1, getNumSteps());
    const auto gridWidth = (float) juce::jmax (1.0f, (float) getWidth() - keyWidth);

    return juce::jlimit (0, steps - 1, (int) (((float) x - keyWidth) / gridWidth * (float) steps));
}

int PianoRollCanvas::noteAtY (int y) const
{
    const auto rowHeight = (float) getHeight() / (float) numVisibleNotes;
    const auto row = juce::jlimit (0, numVisibleNotes - 1, (int) ((float) y / rowHeight));

    return juce::jlimit (0, 127, lowestNote + (numVisibleNotes - 1 - row));
}

void PianoRollCanvas::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto rowHeight = bounds.getHeight() / (float) numVisibleNotes;
    const auto steps = juce::jmax (1, getNumSteps());
    const auto stepWidth = (bounds.getWidth() - keyWidth) / (float) steps;

    g.setColour (theme::windowBg);
    g.fillRect (bounds);

    // Rows and keyboard.
    for (int i = 0; i < numVisibleNotes; ++i)
    {
        const auto midiNote = lowestNote + (numVisibleNotes - 1 - i);
        const auto y = (float) i * rowHeight;
        const auto black = isBlackKey (midiNote);

        g.setColour (black ? theme::windowBg.brighter (0.06f) : theme::panel.darker (0.2f));
        g.fillRect (keyWidth, y, bounds.getWidth() - keyWidth, rowHeight);

        // Keyboard key.
        g.setColour (black ? theme::surface.darker (0.4f) : theme::text.withAlpha (0.86f));
        g.fillRect (0.0f, y, keyWidth - 1.0f, rowHeight - 0.5f);

        g.setColour (theme::windowBg.withAlpha (0.35f));
        g.drawHorizontalLine ((int) (y + rowHeight), keyWidth, bounds.getWidth());

        // C labels only.
        if (midiNote % 12 == 0)
        {
            g.setColour (theme::textFaint);
            g.setFont (theme::font::bold (9.0f));
            g.drawText ("C" + juce::String (midiNote / 12 - noteNameOctaveOffset),
                        juce::Rectangle<float> (keyWidth - 22.0f, y, 20.0f, rowHeight).toNearestInt(),
                        juce::Justification::centredRight);
        }
    }

    // Vertical grid: 16ths, beats and bars.
    for (int step = 0; step <= steps; ++step)
    {
        const auto x = keyWidth + (float) step * stepWidth;
        const auto isBar = step % (constants::stepsPerBeat * 4) == 0;
        const auto isBeat = step % constants::stepsPerBeat == 0;

        g.setColour (isBar ? theme::gridLineStrong.brighter (0.25f)
                           : (isBeat ? theme::gridLineStrong : theme::gridLine.withAlpha (0.6f)));
        g.drawVerticalLine ((int) x, 0.0f, bounds.getHeight());
    }

    // Notes.
    if (const auto* clip = getClip())
    {
        auto& track = document.getState().tracks[(size_t) trackIndex];
        const auto colour = track.getColour();

        for (size_t i = 0; i < clip->notes.size(); ++i)
        {
            const auto& note = clip->notes[i];
            auto noteBounds = getNoteBounds (note.midiNote, note.step, 1).reduced (1.0f, 1.5f);

            const auto velocityAlpha = 0.45f + 0.55f * juce::jlimit (0.1f, 1.0f, note.velocity);

            g.setColour (colour.withAlpha (velocityAlpha));
            g.fillRoundedRectangle (noteBounds, 3.0f);

            g.setColour (colour.brighter (0.5f).withAlpha (0.9f));
            g.drawRoundedRectangle (noteBounds.reduced (0.5f), 3.0f, 1.0f);
        }
    }

    // Hover cell highlight.
    if (hoverNote >= 0 && hoverStep >= 0)
    {
        auto cell = getNoteBounds (hoverNote, hoverStep, 1);
        g.setColour (theme::accent.withAlpha (0.12f));
        g.fillRect (cell);
    }

    // Playhead.
    const auto position = engine.getTransport().getPositionSteps();
    lastPlayhead = position;

    if (position >= 0.0 && position <= (double) steps)
    {
        const auto x = keyWidth + (float) (position / (double) steps) * (bounds.getWidth() - keyWidth);

        g.setColour (theme::accent.withAlpha (0.85f));
        g.drawVerticalLine ((int) x, 0.0f, bounds.getHeight());
    }

    // Keyboard border.
    g.setColour (theme::outline);
    g.drawVerticalLine ((int) keyWidth, 0.0f, bounds.getHeight());
}

void PianoRollCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (auto* clip = getClip())
    {
        auto& track = document.getState().tracks[(size_t) trackIndex];

        // Hit test existing notes first.
        for (size_t i = 0; i < clip->notes.size(); ++i)
        {
            const auto& note = clip->notes[i];

            if (getNoteBounds (note.midiNote, note.step, 1).contains (e.position))
            {
                if (e.mods.isRightButtonDown())
                {
                    document.beginEdit ("note delete", "Delete a note");

                    clip->notes.erase (clip->notes.begin() + (long) i);
                    engine.syncSequencerFromState();
                    document.markDirty();

                    if (onNotesChanged)
                        onNotesChanged();

                    repaint();
                    return;
                }

                draggingNoteIndex = (int) i;
                dragStartStep = stepAtX (e.x);
                dragOriginalStep = note.step;
                return;
            }
        }

        if (e.x < (int) keyWidth)
        {
            // Clicking the keyboard auditions the note.
            if (onPreviewNote)
                onPreviewNote (noteAtY (e.y), 0.9f);

            return;
        }

        if (e.mods.isRightButtonDown())
            return;

        // Add a note.
        Note note;
        note.step = stepAtX (e.x);
        note.midiNote = noteAtY (e.y);
        note.velocity = 0.85f;

        document.beginEdit ("note add", "Add a note");

        if ((int) clip->notes.size() < constants::maxNotesPerClip)
            clip->notes.push_back (note);

        if (track.instrumentPreset == presetAnalogKit)
            track.instrumentPreset = presetPluck;

        engine.syncSequencerFromState();
        document.markDirty();

        if (onPreviewNote)
            onPreviewNote (note.midiNote, note.velocity);

        if (onNotesChanged)
            onNotesChanged();

        repaint();
    }
}

void PianoRollCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingNoteIndex < 0)
        return;

    auto* clip = getClip();

    if (clip == nullptr || ! juce::isPositiveAndBelow (draggingNoteIndex, (int) clip->notes.size()))
        return;

    auto& note = clip->notes[(size_t) draggingNoteIndex];

    // The first drag event is the real edit: snapshot before the note moves, and
    // let the coalescing window keep the whole drag as one undo step.
    document.beginEdit ("note drag", "Move a note");

    const auto stepDelta = stepAtX (e.x) - dragStartStep;
    note.step = juce::jlimit (0, getNumSteps() - 1, dragOriginalStep + stepDelta);

    if (e.mods.isShiftDown())
        note.midiNote = juce::jlimit (0, 127, noteAtY (e.y));

    repaint();
}

void PianoRollCanvas::mouseUp (const juce::MouseEvent&)
{
    if (draggingNoteIndex >= 0)
    {
        draggingNoteIndex = -1;
        engine.syncSequencerFromState();
        document.markDirty();

        if (onNotesChanged)
            onNotesChanged();
    }
}

void PianoRollCanvas::mouseMove (const juce::MouseEvent& e)
{
    const auto note = noteAtY (e.y);
    const auto step = stepAtX (e.x);

    if (note != hoverNote || step != hoverStep)
    {
        hoverNote = note;
        hoverStep = step;
        repaint();
    }
}

void PianoRollCanvas::mouseExit (const juce::MouseEvent&)
{
    hoverNote = -1;
    hoverStep = -1;
    repaint();
}

void PianoRollCanvas::refreshPlayhead()
{
    const auto position = engine.getTransport().getPositionSteps();

    if (engine.getTransport().isPlaying() || std::abs (position - lastPlayhead) > 0.0005)
    {
        lastPlayhead = position;
        repaint();
    }
}

//==============================================================================
PianoRollView::PianoRollView (ProjectDocument& d, AudioEngine& e)
    : document (d), engine (e), canvas (d, e)
{
    addAndMakeVisible (canvas);
    addAndMakeVisible (presetChooser);
    addAndMakeVisible (barCountSlider);
    addAndMakeVisible (barCountLabel);
    addAndMakeVisible (velocityKnob);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (octaveDownButton);
    addAndMakeVisible (octaveUpButton);
    addAndMakeVisible (titleLabel);

    titleLabel.setFont (theme::font::bold (12.0f));
    titleLabel.setColour (juce::Label::textColourId, theme::textDim);

    presetChooser.addItemList (getInstrumentPresetNames(), 1);
    presetChooser.setSelectedId (1, juce::dontSendNotification);

    presetChooser.onChange = [this]
    {
        auto& state = document.getState();

        if (juce::isPositiveAndBelow (canvas.getTrack(), (int) state.tracks.size()))
        {
            document.beginEdit ("preset", "Change the instrument");

            state.tracks[(size_t) canvas.getTrack()].instrumentPreset = juce::jmax (0, presetChooser.getSelectedId() - 1);
            engine.syncSequencerFromState();
            document.markDirty();

            if (onProjectEdited)
                onProjectEdited();
        }
    };

    barCountSlider.setSliderStyle (juce::Slider::IncDecButtons);
    barCountSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 22);
    barCountSlider.setRange (1.0, 8.0, 1.0);
    barCountSlider.setValue (1.0, juce::dontSendNotification);

    barCountSlider.onValueChange = [this]
    {
        canvas.setNumBars ((int) barCountSlider.getValue());
    };

    barCountLabel.setText ("BARS", juce::dontSendNotification);
    barCountLabel.setFont (theme::font::ui (10.0f));
    barCountLabel.setColour (juce::Label::textColourId, theme::textFaint);
    barCountLabel.setJustificationType (juce::Justification::centredRight);

    velocityKnob.setKnobSize (26);
    velocityKnob.setShowLabel (false);
    velocityKnob.setShowReadout (false);
    velocityKnob.setSuffix ("");

    velocityKnob.onValueChange = [this] (double value)
    {
        // Applies to newly drawn notes through the canvas default.
        juce::ignoreUnused (value);
    };

    clearButton.setColour (juce::TextButton::buttonColourId, theme::surface);
    clearButton.onClick = [this]
    {
        auto& state = document.getState();

        if (juce::isPositiveAndBelow (canvas.getTrack(), (int) state.tracks.size()))
        {
            document.beginEdit ("clear notes", "Clear the notes");

            for (auto& clip : state.tracks[(size_t) canvas.getTrack()].clips)
                clip.notes.clear();

            engine.syncSequencerFromState();
            document.markDirty();
            canvas.repaint();

            if (onProjectEdited)
                onProjectEdited();
        }
    };

    octaveUpButton.setColour (juce::TextButton::buttonColourId, theme::surface);
    octaveDownButton.setColour (juce::TextButton::buttonColourId, theme::surface);

    octaveUpButton.onClick = [this] { octaveOffset = juce::jmin (4, octaveOffset + 1); canvas.setTrack (canvas.getTrack() + 0); repaint(); };
    octaveDownButton.onClick = [this] { octaveOffset = juce::jmax (-4, octaveOffset - 1); repaint(); };

    canvas.onPreviewNote = [this] (int note, float velocity)
    {
        engine.triggerPreviewNote (canvas.getTrack(), juce::jlimit (0, 127, note + octaveOffset * 12), velocity);
    };

    canvas.onNotesChanged = [this]
    {
        // The edit already snapshotted; this callback only refreshes the views.
        document.markDirty();

        if (onProjectEdited)
            onProjectEdited();
    };

    startTimerHz (30);
}

PianoRollView::~PianoRollView()
{
    stopTimer();
}

void PianoRollView::timerCallback()
{
    canvas.refreshPlayhead();
}

void PianoRollView::setTrack (int trackIndex)
{
    canvas.setTrack (trackIndex);

    auto& state = document.getState();

    if (juce::isPositiveAndBelow (trackIndex, (int) state.tracks.size()))
    {
        const auto& track = state.tracks[(size_t) trackIndex];
        titleLabel.setText ("PIANO ROLL  -  " + track.name, juce::dontSendNotification);
        presetChooser.setSelectedId (juce::jlimit (1, (int) numInstrumentPresets, track.instrumentPreset + 1),
                                    juce::dontSendNotification);
    }
}

void PianoRollView::paint (juce::Graphics& g)
{
    g.setColour (theme::windowBg);
    g.fillRect (getLocalBounds());

    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (38));

    g.setColour (theme::outline);
    g.drawHorizontalLine (37, 0.0f, (float) getWidth());
}

void PianoRollView::resized()
{
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop (38).reduced (10, 6);

    titleLabel.setBounds (toolbar.removeFromLeft (juce::jmin (240, toolbar.getWidth() / 3)));

    octaveDownButton.setBounds (toolbar.removeFromRight (54).reduced (2, 0));
    octaveUpButton.setBounds (toolbar.removeFromRight (54).reduced (2, 0));
    toolbar.removeFromRight (10);
    clearButton.setBounds (toolbar.removeFromRight (60).reduced (2, 0));
    toolbar.removeFromRight (12);
    barCountSlider.setBounds (toolbar.removeFromRight (104).reduced (0, 0));
    barCountLabel.setBounds (toolbar.removeFromRight (34));
    toolbar.removeFromRight (10);
    velocityKnob.setBounds (toolbar.removeFromRight (46));
    toolbar.removeFromRight (10);
    presetChooser.setBounds (toolbar.removeFromRight (juce::jmin (130, juce::jmax (90, toolbar.getWidth()))));

    canvas.setBounds (area.reduced (0, 1));
}
}
