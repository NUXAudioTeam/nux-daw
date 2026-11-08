/* NUX DAW - the transport bar: wordmark, play controls, tempo, position, meters. */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../nux_engine/AudioEngine.h"
#include "Widgets.h"

namespace nux
{
class TransportBar final : public juce::Component,
                           private juce::Timer
{
public:
    explicit TransportBar (AudioEngine& engine);
    ~TransportBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Refreshes the position readout and meter levels. Call from a 30 Hz timer. */
    void refresh();

    NuxKnob& getTempoKnob() noexcept { return tempoKnob; }

    /** Shows a short-lived message in the status area of the bar. */
    void setStatus (const juce::String& message);

    std::function<void()> onTempoChanged;
    std::function<void()> onAudioSettingsRequested;
    std::function<void()> onSaveRequested;
    /** The record button; the host decides what recording means. */
    std::function<void()> onRecordToggled;

private:
    void timerCallback() override;
    void updatePlayButton();
    juce::String formatPosition (double steps) const;

    AudioEngine& engine;

    IconButton playButton { IconButton::Icon::play, "Play / Stop (Space)" };
    IconButton stopButton { IconButton::Icon::stop, "Rewind to start" };
    IconButton recordButton { IconButton::Icon::record, "Record" };
    IconButton loopButton { IconButton::Icon::loop, "Loop pattern" };
    IconButton rewindButton { IconButton::Icon::back, "Previous bar" };
    IconButton forwardButton { IconButton::Icon::forward, "Next bar" };
    IconButton saveButton { IconButton::Icon::save, "Save project (Ctrl+S)" };
    juce::TextButton audioButton { "AUDIO" };

    NuxKnob tempoKnob { "Tempo", constants::minTempo, constants::maxTempo, 1.0, 120.0 };
    ScopeView scope;
    LevelMeter masterMeter { true };

    juce::Label positionLabel;
    juce::Label statusLabel;

    juce::String statusMessage;
    int statusCountdown = 0;
    juce::String lastPositionText;
    bool lastPlayingState = false;
};
}
