/* NUX DAW - the Main view: track headers plus the step-sequencer / timeline grid. */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../nux_core/ProjectDocument.h"
#include "../nux_engine/AudioEngine.h"
#include "Widgets.h"

namespace nux
{
/** The scrolling grid: 16th-note steps across, tracks down. Clicking a cell
    toggles that step; the playhead follows the transport. */
class SequencerGrid final : public juce::Component,
                            public juce::FileDragAndDropTarget
{
public:
    SequencerGrid (ProjectDocument& document, AudioEngine& engine);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Drag and drop audio files straight onto a track lane. */
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;

    void setSelectedTrack (int index);
    int getSelectedTrack() const noexcept { return selectedTrack; }

    void refreshPlayhead();

    std::function<void (int)> onTrackSelected;
    std::function<void()> onPatternEdited;
    std::function<void (int, int)> onPreviewNote;   // track, midi note
    std::function<void (int)> onStepCountRequested; // track index
    std::function<void (const juce::File&, int)> onAudioFileDropped;   // file, track
    std::function<void (int, int)> onAudioClipClicked;                 // track, clip
    std::function<void (int, int)> onAudioClipMenu;                    // track, clip

private:
    juce::Rectangle<float> getCellBounds (int track, int step) const;
    bool hitTestCell (juce::Point<int> position, int& track, int& step) const;

    ProjectDocument& document;
    AudioEngine& engine;

    int selectedTrack = 0;
    int rowHeight = 54;
    int headerWidth = theme::metrics::trackHeaderWidth;
    int rulerHeight = theme::metrics::rulerHeight;
    double lastPlayhead = -1.0;

    juce::int64 lastClickStep = -1;
    int lastClickTrack = -1;

    /** Audio clip geometry helpers. */
    struct ClipHit { int track = -1; int clip = -1; };
    ClipHit hitTestAudioClip (juce::Point<int> position) const;
    juce::Rectangle<float> getAudioClipBounds (int track, int clipIndex) const;

    int hoveredAudioTrack = -1;
    int hoveredAudioClip = -1;
    int pendingDropTrack = -1;

    friend class TimelineView;
};

//==============================================================================
/** The Main view: track list on the left, sequencer grid on the right. */
class TimelineView final : public juce::Component,
                           private juce::Timer
{
public:
    TimelineView (ProjectDocument& document, AudioEngine& engine);
    ~TimelineView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void rebuild();
    void refreshAudioClips();
    void setSelectedTrack (int index);
    int getSelectedTrack() const noexcept { return selectedTrack; }

    std::function<void()> onProjectEdited;
    std::function<void()> onPreviewNote;
    std::function<void (const juce::File&, int)> onAudioFileDropped;
    std::function<void (int)> onInstrumentPluginRequested;   // track
    std::function<void (int)> onClearInstrumentPlugin;       // track

private:
    void timerCallback() override;
    void showTrackMenu (int trackIndex);
    void drawTrackHeader (juce::Graphics&, juce::Rectangle<int> bounds, int trackIndex, bool selected);

    ProjectDocument& document;
    AudioEngine& engine;

    SequencerGrid grid;
    std::vector<std::unique_ptr<ChipButton>> muteButtons;
    std::vector<std::unique_ptr<ChipButton>> soloButtons;
    std::vector<std::unique_ptr<ChipButton>> armButtons;
    std::vector<std::unique_ptr<NuxKnob>> volumeKnobs;
    std::vector<std::unique_ptr<LevelMeter>> meters;

    juce::TextButton addTrackButton { "+ TRACK" };
    juce::TextButton addPatternButton { "+ PATTERN" };
    juce::ComboBox presetChooser;
    LevelMeter masterMeter { true };

    juce::Rectangle<int> headerArea;
    int selectedTrack = 0;
};
}
