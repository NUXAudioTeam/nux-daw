#include "AudioSettingsPanel.h"

namespace nux
{
AudioSettingsPanel::AudioSettingsPanel (AudioEngine& e)
    : engine (e),
      selector (e.getDeviceManager(),
                0, 2,      // input channels: recording is MIDI-only for now
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

    addAndMakeVisible (hint);
    hint.setText ("Choose a WASAPI or ASIO driver for the lowest latency. "
                  "NUX asks the device for a 256 sample buffer at the highest sample rate the driver supports.",
                  juce::dontSendNotification);
    hint.setFont (theme::font::ui (11.0f));
    hint.setColour (juce::Label::textColourId, theme::textFaint);

    selector.setLookAndFeel (&getLookAndFeel());
}

AudioSettingsPanel::~AudioSettingsPanel() = default;

void AudioSettingsPanel::refresh()
{
    // The selector watches the device manager for changes and repaints itself.
    selector.repaint();
    status.setText (engine.getAudioStatusMessage(), juce::dontSendNotification);
}

void AudioSettingsPanel::applySetup()
{
    engine.applyPreferredDeviceSetup();

    if (onSettingsChanged)
        onSettingsChanged();
}

void AudioSettingsPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds());

    g.setColour (theme::outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 1.0f);
}

void AudioSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (12);

    title.setBounds (area.removeFromTop (24));
    status.setBounds (area.removeFromTop (20));
    hint.setBounds (area.removeFromBottom (40).reduced (0, 4));

    selector.setBounds (area.reduced (0, 6));
}
}
