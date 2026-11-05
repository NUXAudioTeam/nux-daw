#include "TransportBar.h"

namespace nux
{
TransportBar::TransportBar (AudioEngine& e) : engine (e)
{
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (loopButton);
    addAndMakeVisible (rewindButton);
    addAndMakeVisible (forwardButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (audioButton);
    addAndMakeVisible (tempoKnob);
    addAndMakeVisible (scope);
    addAndMakeVisible (masterMeter);
    addAndMakeVisible (positionLabel);
    addAndMakeVisible (statusLabel);

    recordButton.setIconColour (theme::recordRed);
    playButton.setIconColour (theme::playGreen);

    tempoKnob.setKnobSize (40);
    tempoKnob.setSuffix (" BPM");
    tempoKnob.setValue (engine.getTransport().getTempo(), juce::dontSendNotification);

    tempoKnob.onValueChange = [this] (double value)
    {
        engine.getTransport().setTempo (value);

        if (engine.getProjectState() != nullptr)
            engine.getProjectState()->tempo = value;

        if (onTempoChanged)
            onTempoChanged();
    };

    loopButton.setToggleState (engine.getTransport().isLoopEnabled(), juce::dontSendNotification);

    playButton.onClick = [this]
    {
        engine.togglePlay();
        updatePlayButton();
    };

    stopButton.onClick = [this]
    {
        engine.stop();
        engine.getTransport().setPositionSteps (0.0);
        updatePlayButton();
        refresh();
    };

    recordButton.onClick = [this]
    {
        if (engine.getTransport().isRecording())
            engine.stopRecording();
        else
            engine.startRecording();

        recordButton.setToggleState (engine.getTransport().isRecording(), juce::dontSendNotification);
    };

    loopButton.onClick = [this]
    {
        const auto enabled = loopButton.getToggleState();
        engine.getTransport().setLoopEnabled (enabled);

        if (engine.getProjectState() != nullptr)
            engine.getProjectState()->loopEnabled = enabled;
    };

    rewindButton.onClick = [this]
    {
        const auto barLength = juce::jmax (4, engine.getTransport().getLoopLengthSteps());
        const auto position = engine.getTransport().getPositionSteps();
        engine.getTransport().setPositionSteps (juce::jmax (0.0, std::floor (position / barLength) * barLength - barLength));
        refresh();
    };

    forwardButton.onClick = [this]
    {
        const auto barLength = juce::jmax (4, engine.getTransport().getLoopLengthSteps());
        const auto position = engine.getTransport().getPositionSteps();
        const auto next = std::floor (position / barLength) * barLength + barLength;

        if (engine.getTransport().isLoopEnabled())
            engine.getTransport().setPositionSteps (std::fmod (next, (double) barLength));
        else
            engine.getTransport().setPositionSteps (next);

        refresh();
    };

    saveButton.onClick = [this]
    {
        if (onSaveRequested)
            onSaveRequested();
    };

    audioButton.onClick = [this]
    {
        if (onAudioSettingsRequested)
            onAudioSettingsRequested();
    };

    positionLabel.setFont (theme::font::mono (18.0f));
    positionLabel.setColour (juce::Label::textColourId, theme::accent);
    positionLabel.setJustificationType (juce::Justification::centredLeft);

    statusLabel.setFont (theme::font::ui (11.0f));
    statusLabel.setColour (juce::Label::textColourId, theme::textFaint);
    statusLabel.setJustificationType (juce::Justification::centredRight);

    startTimerHz (30);
    refresh();
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::timerCallback()
{
    refresh();
}

void TransportBar::refresh()
{
    const auto& transport = engine.getTransport();

    auto position = formatPosition (transport.getPositionSteps());

    if (position != lastPositionText)
    {
        lastPositionText = position;
        positionLabel.setText (position, juce::dontSendNotification);
    }

    if (transport.isPlaying() != lastPlayingState)
        updatePlayButton();

    recordButton.setToggleState (transport.isRecording(), juce::dontSendNotification);

    masterMeter.setLevel (engine.getMasterPeak (0), engine.getMasterPeak (1));

    float samples[512];
    const auto count = engine.readScope (samples, 256);
    scope.setSamples (samples, count);

    if (statusCountdown > 0)
    {
        --statusCountdown;

        if (statusCountdown == 0)
        {
            statusMessage.clear();
            statusLabel.setText (engine.getAudioStatusMessage(), juce::dontSendNotification);
        }
    }
}

void TransportBar::updatePlayButton()
{
    lastPlayingState = engine.getTransport().isPlaying();
    playButton.setToggleState (lastPlayingState, juce::dontSendNotification);
    playButton.setTooltip (lastPlayingState ? "Stop (Space)" : "Play (Space)");
}

void TransportBar::setStatus (const juce::String& message)
{
    statusMessage = message;
    statusCountdown = 30 * 4;
    statusLabel.setText (message, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, theme::accent);
}

juce::String TransportBar::formatPosition (double steps) const
{
    const auto beatsPerBar = juce::jmax (1, engine.getTransport().getLoopLengthSteps() / constants::stepsPerBeat);
    const auto totalBeats = steps / (double) constants::stepsPerBeat;
    const auto bar = (int) (totalBeats / (double) beatsPerBar) + 1;
    const auto beat = (int) std::fmod (totalBeats, (double) beatsPerBar) + 1;
    const auto sixteenth = (int) std::floor (std::fmod (steps, (double) constants::stepsPerBeat));

    return juce::String (bar).paddedLeft ('0', 3) + " : "
         + juce::String (beat).paddedLeft ('0', 2) + " : "
         + juce::String (sixteenth + 1).paddedLeft ('0', 2);
}

void TransportBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (theme::panelRaised, bounds.getTopLeft(),
                                             theme::panel, bounds.getBottomLeft(), false));
    g.fillRect (bounds);

    g.setColour (theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, bounds.getRight());

    // Wordmark block.
    drawNuxWordmark (g, juce::Rectangle<float> (10.0f, 8.0f, 96.0f, (float) getHeight() - 16.0f),
                     theme::accent);

    // Position readout background.
    g.setColour (theme::windowBg.withAlpha (0.6f));
    g.fillRoundedRectangle (positionLabel.getBounds().toFloat().expanded (4.0f, 2.0f), 4.0f);
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (8, 8);

    // ---- Right hand side first: meter, audio button, scope, status ----
    masterMeter.setBounds (area.removeFromRight (26).reduced (4, 6));
    area.removeFromRight (6);

    audioButton.setBounds (area.removeFromRight (70).withSizeKeepingCentre (68, 26));
    area.removeFromRight (8);

    scope.setBounds (area.removeFromRight (juce::jmin (170, juce::jmax (70, area.getWidth() / 4))));
    area.removeFromRight (8);

    statusLabel.setBounds (area.removeFromRight (juce::jmin (320, juce::jmax (120, area.getWidth() / 3))));

    // ---- Left: wordmark, transport buttons, position, tempo, save ----
    area.removeFromLeft (104);   // reserved for the wordmark drawn in paint()

    auto buttonRow = area.removeFromLeft (250).withSizeKeepingCentre (250, 32);

    rewindButton.setBounds (buttonRow.removeFromLeft (32));
    buttonRow.removeFromLeft (4);
    playButton.setBounds (buttonRow.removeFromLeft (44));
    buttonRow.removeFromLeft (4);
    stopButton.setBounds (buttonRow.removeFromLeft (32));
    buttonRow.removeFromLeft (4);
    recordButton.setBounds (buttonRow.removeFromLeft (34));
    buttonRow.removeFromLeft (8);
    loopButton.setBounds (buttonRow.removeFromLeft (32));

    area.removeFromLeft (12);
    positionLabel.setBounds (area.removeFromLeft (150).reduced (2));

    area.removeFromLeft (10);
    tempoKnob.setBounds (area.removeFromLeft (86));

    area.removeFromLeft (8);
    saveButton.setBounds (area.removeFromLeft (32).withSizeKeepingCentre (32, 30));
}
}
