/* NUX DAW - the Piano Roll view: note editing on a piano keyboard grid. */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../nux_core/ProjectDocument.h"
#include "../nux_engine/AudioEngine.h"
#include "Widgets.h"

namespace nux
{
class PianoRollCanvas final : public juce::Component
{
public:
    PianoRollCanvas (ProjectDocument& document, AudioEngine& engine);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setTrack (int trackIndex);
    int getTrack() const noexcept { return trackIndex; }
    void setNumBars (int bars) { numBars = juce::jlimit (1, 16, bars); repaint(); }
    int getNumBars() const noexcept { return numBars; }

    void refreshPlayhead();

    /** Longer patterns can be trimmed from here. */
    int getNumSteps() const noexcept { return numBars * constants::stepsPerBeat * 4; }

    std::function<void()> onNotesChanged;
    std::function<void (int, float)> onPreviewNote;

private:
    juce::Rectangle<float> getKeyBounds (int midiNote) const;
    juce::Rectangle<float> getNoteBounds (int midiNote, int startStep, int lengthSteps) const;
    int stepAtX (int x) const;
    int noteAtY (int y) const;
    static bool isBlackKey (int midiNote);
    Clip* getClip();
    const Clip* getClip() const;

    ProjectDocument& document;
    AudioEngine& engine;
    int trackIndex = 0;
    int numBars = 1;
    int lowestNote = 36;
    int numVisibleNotes = 25;
    float keyWidth = 56.0f;

    int draggingNoteIndex = -1;
    int dragStartStep = 0;
    int dragOriginalStep = 0;
    int hoverNote = -1;
    int hoverStep = -1;
    double lastPlayhead = -1.0;
};

//==============================================================================
/** Wraps the canvas with a toolbar: preset, bar count, velocity and a preview pad. */
class PianoRollView final : public juce::Component,
                            private juce::Timer
{
public:
    PianoRollView (ProjectDocument& document, AudioEngine& engine);
    ~PianoRollView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setTrack (int trackIndex);

    std::function<void()> onProjectEdited;

private:
    void timerCallback() override;

    ProjectDocument& document;
    AudioEngine& engine;

    PianoRollCanvas canvas;
    juce::ComboBox presetChooser;
    juce::Slider barCountSlider;
    juce::Label barCountLabel;
    NuxKnob velocityKnob { "Velocity", 0.1, 1.0, 0.01, 0.85 };
    juce::TextButton clearButton { "CLEAR" };
    juce::TextButton octaveUpButton { "OCT +" };
    juce::TextButton octaveDownButton { "OCT -" };
    juce::Label titleLabel;
    int octaveOffset = 0;
};
}
