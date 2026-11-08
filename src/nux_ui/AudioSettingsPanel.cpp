#include "AudioSettingsPanel.h"

namespace nux
{
namespace
{
constexpr int meterWidth = 12;

juce::String formatPeakDb (float linear)
{
    if (linear <= 0.0001f)
        return "-inf";

    return juce::String (juce::Decibels::gainToDecibels (linear), 1);
}
}

AudioSettingsPanel::AudioSettingsPanel (AudioEngine& e)
    : engine (e),
      selector (e.getDeviceManager(),
                2, 2,      // input channels: recording needs a real input
                2, 2,      // output channels: stereo
                true,      // let the user enable MIDI input devices
                false,     // no MIDI output
                true,      // treat channels as stereo pairs
                false)     // show the advanced options straight away
{
    addAndMakeVisible (selector);

    addAndMakeVisible (title);
    title.setText ("AUDIO SETTINGS", juce::dontSendNotification);
    title.setFont (theme::font::bold (14.0f));
    title.setColour (juce::Label::textColourId, theme::accent);

    addAndMakeVisible (status);
    status.setFont (theme::font::mono (11.0f));
    status.setColour (juce::Label::textColourId, theme::accent);
    status.setText (engine.getAudioStatusMessage(), juce::dontSendNotification);

    addAndMakeVisible (deviceWarning);
    deviceWarning.setFont (theme::font::ui (11.0f));
    deviceWarning.setColour (juce::Label::textColourId, theme::warn);
    deviceWarning.setVisible (false);

    addAndMakeVisible (hint);
    hint.setText ("Choose a WASAPI or ASIO driver for the lowest latency. "
                  "NUX asks the device for a 256 sample buffer at the highest sample rate the driver supports.",
                  juce::dontSendNotification);
    hint.setFont (theme::font::ui (11.0f));
    hint.setColour (juce::Label::textColourId, theme::textFaint);

    addAndMakeVisible (testToneButton);
    testToneButton.setClickingTogglesState (true);
    testToneButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    testToneButton.setColour (juce::TextButton::textColourOnId, theme::windowBg);
    testToneButton.onClick = [this] { toggleTestTone(); };

    addAndMakeVisible (testToneLabel);
    testToneLabel.setText ("LEVEL", juce::dontSendNotification);
    testToneLabel.setFont (theme::font::ui (10.0f));
    testToneLabel.setColour (juce::Label::textColourId, theme::textFaint);

    addAndMakeVisible (testToneLevel);
    testToneLevel.setRange (0.0, 1.0, 0.01);
    testToneLevel.setValue (0.25, juce::dontSendNotification);
    testToneLevel.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
    testToneLevel.setColour (juce::Slider::trackColourId, theme::accent);
    testToneLevel.setColour (juce::Slider::textBoxTextColourId, theme::text);
    testToneLevel.setColour (juce::Slider::textBoxOutlineColourId, theme::outline);
    testToneLevel.onValueChange = [this]
    {
        engine.setTestToneLevel ((float) testToneLevel.getValue());
        engine.setTestToneEnabled (testToneButton.getToggleState());
    };

    selector.setLookAndFeel (&getLookAndFeel());

    startTimerHz (20);
}

AudioSettingsPanel::~AudioSettingsPanel()
{
    // Never leave a test tone running after the panel goes away.
    engine.setTestToneEnabled (false);
    stopTimer();
}

void AudioSettingsPanel::refresh()
{
    selector.repaint();
    status.setText (engine.getAudioStatusMessage(), juce::dontSendNotification);

    // A device that vanished has to be visible, not silent.
    const auto lost = engine.isDeviceLost() || ! engine.isAudioAvailable();

    if (lost)
    {
        deviceWarning.setText ("No working audio device. " + engine.getLastDeviceError()
                                 + "  Pick a driver above to reopen it.",
                               juce::dontSendNotification);
        deviceWarning.setVisible (true);
    }
    else
    {
        deviceWarning.setVisible (false);
    }

    updateTestToneButton();
}

void AudioSettingsPanel::toggleTestTone()
{
    const auto wantsTone = testToneButton.getToggleState();

    if (wantsTone && ! engine.isAudioAvailable())
    {
        testToneButton.setToggleState (false, juce::dontSendNotification);
        deviceWarning.setText ("The test tone needs an open audio device first.",
                               juce::dontSendNotification);
        deviceWarning.setVisible (true);
        return;
    }

    engine.setTestToneLevel ((float) testToneLevel.getValue());
    engine.setTestToneEnabled (wantsTone);
    updateTestToneButton();
}

void AudioSettingsPanel::updateTestToneButton()
{
    // The engine is the source of truth; the button follows it so the two can never
    // disagree (for example after a panic or a device change).
    const auto on = engine.isTestToneEnabled();

    if (testToneButton.getToggleState() != on)
        testToneButton.setToggleState (on, juce::dontSendNotification);

    testToneButton.setButtonText (on ? "STOP TONE" : "TEST TONE");
}

void AudioSettingsPanel::timerCallback()
{
    smoothedInput  = juce::jmax (engine.getInputPeak(), smoothedInput * 0.85f);
    smoothedOutput = juce::jmax (juce::jmax (engine.getOutputPeak (0), engine.getOutputPeak (1)),
                                 smoothedOutput * 0.85f);

    repaint (inputMeterArea);
    repaint (outputMeterArea);

    // Catch a device that disappeared or came back while the panel was open.
    if (engine.isDeviceLost())
    {
        refresh();

        if (engine.tryReopenDevice())
            refresh();
    }
}

void AudioSettingsPanel::drawMeter (juce::Graphics& g, juce::Rectangle<int> bounds,
                                    const juce::String& label, float peak,
                                    juce::Colour meterColour, bool showZeroWarning)
{
    if (bounds.isEmpty())
        return;

    g.setColour (theme::textFaint);
    g.setFont (theme::font::ui (10.0f));
    g.drawText (label, bounds.removeFromTop (14), juce::Justification::centredLeft);

    auto bar = bounds.reduced (0, 2);
    auto textArea = bar.removeFromRight (52);

    g.setColour (theme::windowBg);
    g.fillRoundedRectangle (bar.toFloat(), 3.0f);

    const auto db = juce::Decibels::gainToDecibels (juce::jmax (peak, 0.0001f));
    const auto proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

    if (proportion > 0.001f)
    {
        auto filled = bar.toFloat().withWidth (bar.getWidth() * proportion);
        g.setColour (db > -1.0f ? theme::warn : meterColour);
        g.fillRoundedRectangle (filled, 3.0f);
    }

    g.setColour (theme::outline);
    g.drawRoundedRectangle (bar.toFloat(), 3.0f, 1.0f);

    g.setColour (peak > 0.0001f ? theme::text : theme::textFaint);
    g.setFont (theme::font::mono (10.0f));
    g.drawText (formatPeakDb (peak) + " dB", textArea, juce::Justification::centredRight);

    // An input that never moves is the most common recording mistake; say so.
    if (showZeroWarning && peak <= 0.0001f)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::font::ui (9.0f));
        g.drawText ("no input signal", textArea.translated (-58, 0), juce::Justification::centredRight);
    }
}

void AudioSettingsPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds());

    g.setColour (theme::outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 1.0f);

    const auto hasInput = engine.hasAudioInput();

    drawMeter (g, inputMeterArea, hasInput ? "INPUT" : "INPUT (none)", smoothedInput,
               theme::accent, true);
    drawMeter (g, outputMeterArea, "OUTPUT", smoothedOutput, theme::accent, false);
}

void AudioSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (12);

    title.setBounds (area.removeFromTop (24));
    status.setBounds (area.removeFromTop (20));
    deviceWarning.setBounds (area.removeFromTop (18).withWidth (area.getWidth()));

    auto hintArea = area.removeFromBottom (40);
    hint.setBounds (hintArea.reduced (0, 4));

    // Test tone row, then the two meters side by side.
    auto toneRow = area.removeFromBottom (30).reduced (0, 3);
    testToneButton.setBounds (toneRow.removeFromLeft (104));
    toneRow.removeFromLeft (10);
    testToneLabel.setBounds (toneRow.removeFromLeft (40));
    testToneLevel.setBounds (toneRow);

    auto meterRow = area.removeFromBottom (44).reduced (0, 4);
    inputMeterArea  = meterRow.removeFromLeft (meterRow.getWidth() / 2).reduced (0, 0);
    meterRow.removeFromLeft (12);
    outputMeterArea = meterRow;

    selector.setBounds (area.reduced (0, 6));
}
}
