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

    addAndMakeVisible (labTitle);
    labTitle.setText ("VIRTUAL AUDIO LAB", juce::dontSendNotification);
    labTitle.setFont (theme::font::bold (11.0f));
    labTitle.setColour (juce::Label::textColourId, theme::accent);

    addAndMakeVisible (labHint);
    labHint.setFont (theme::font::ui (10.0f));
    labHint.setColour (juce::Label::textColourId, theme::textFaint);
    labHint.setText ("Pick what plays and what records. A round trip proves the routing and "
                     "the engine; a virtual cable result is never a hardware result.",
                     juce::dontSendNotification);

    for (auto* box : { &outputChooser, &inputChooser })
    {
        addAndMakeVisible (*box);
        box->setColour (juce::ComboBox::backgroundColourId, theme::windowBg);
        box->setColour (juce::ComboBox::textColourId, theme::text);
        box->setColour (juce::ComboBox::outlineColourId, theme::outline);
        box->setColour (juce::ComboBox::arrowColourId, theme::accent);
    }

    outputChooser.setTextWhenNothingSelected ("output device");
    inputChooser.setTextWhenNothingSelected ("input device");

    addAndMakeVisible (openDeviceButton);
    openDeviceButton.onClick = [this] { openSelectedDevice(); };

    addAndMakeVisible (roundTripButton);
    roundTripButton.onClick = [this] { runRoundTrip(); };

    addAndMakeVisible (labResult);
    labResult.setFont (theme::font::mono (10.0f));
    labResult.setColour (juce::Label::textColourId, theme::textFaint);
    labResult.setJustificationType (juce::Justification::topLeft);
    labResult.setMinimumHorizontalScale (0.85f);
    labResult.setText ("No round trip has been run in this session.", juce::dontSendNotification);

    refreshLabDevices();

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

void AudioSettingsPanel::refreshLabDevices()
{
    labOptions = listDeviceOptions (engine.getDeviceManager());

    outputChooser.clear (juce::dontSendNotification);
    inputChooser.clear (juce::dontSendNotification);

    int id = 1;

    // Menu IDs index into labOptions, so a choice can always be turned back into a device
    // name without parsing the label.
    auto optionIds = std::vector<int> (labOptions.size(), 0);

    for (size_t i = 0; i < labOptions.size(); ++i)
    {
        optionIds[i] = id;

        if (labOptions[i].canOutput)
            outputChooser.addItem (labOptions[i].getLabel(), id);

        if (labOptions[i].canInput)
            inputChooser.addItem (labOptions[i].getLabel(), id);

        ++id;
    }

    // Pre-select a virtual cable when one is installed, because that is the only pairing
    // that can run a round trip on a machine without hardware.
    const auto pair = findVirtualCablePair (labOptions);

    if (pair.found)
    {
        for (size_t i = 0; i < labOptions.size(); ++i)
        {
            if (labOptions[i].deviceName == pair.outputName)
                outputChooser.setSelectedId (optionIds[i], juce::dontSendNotification);

            if (labOptions[i].deviceName == pair.inputName)
                inputChooser.setSelectedId (optionIds[i], juce::dontSendNotification);
        }

        labHint.setText ("Virtual cable detected. A clean result proves the routing and the "
                         "engine path, and nothing about a converter or a speaker.",
                         juce::dontSendNotification);
    }
    else if (labOptions.empty())
    {
        labHint.setText ("No audio devices at all. The round trip stays SKIPPED - that is not "
                         "a pass. Install a virtual cable or connect an interface to change it.",
                         juce::dontSendNotification);
    }
    else
    {
        labHint.setText ("No virtual cable installed. A round trip needs a playback device and "
                         "a matching capture device; without one it reports SKIPPED.",
                         juce::dontSendNotification);
    }
}

void AudioSettingsPanel::openSelectedDevice()
{
    const auto optionFor = [this] (int selectedId) -> const DeviceOption*
    {
        const auto index = (size_t) (selectedId - 1);

        if (selectedId <= 0 || index >= labOptions.size())
            return nullptr;

        return &labOptions[index];
    };

    const auto* output = optionFor (outputChooser.getSelectedId());
    const auto* input  = optionFor (inputChooser.getSelectedId());

    if (output == nullptr)
    {
        labResult.setColour (juce::Label::textColourId, theme::warn);
        labResult.setText ("Choose an output device first.", juce::dontSendNotification);
        return;
    }

    const auto error = engine.applyDeviceChoice (output->typeName,
                                                 output->deviceName,
                                                 input != nullptr ? input->deviceName
                                                                  : juce::String());

    if (error.isEmpty())
    {
        labResult.setColour (juce::Label::textColourId, theme::accent);
        labResult.setText ("Opened " + output->deviceName
                             + (input != nullptr ? " / " + input->deviceName : juce::String()) + ".",
                           juce::dontSendNotification);
    }
    else
    {
        labResult.setColour (juce::Label::textColourId, theme::warn);
        labResult.setText ("Could not open that pair: " + error, juce::dontSendNotification);
    }

    refresh();
}

void AudioSettingsPanel::runRoundTrip()
{
    const auto toIndex = [] (int selectedId) { return (size_t) juce::jmax (0, selectedId - 1); };

    nux::lab::LoopbackRequest request;
    request.spec.type = nux::lab::SignalType::sine;
    request.spec.frequency = 1000.0;
    request.spec.amplitude = 0.25f;
    request.seconds = 1.5;

    const auto outputId = outputChooser.getSelectedId();
    const auto inputId = inputChooser.getSelectedId();

    if (outputId > 0 && toIndex (outputId) < labOptions.size())
    {
        request.choice.typeName = labOptions[toIndex (outputId)].typeName;
        request.choice.outputName = labOptions[toIndex (outputId)].deviceName;
    }

    if (inputId > 0 && toIndex (inputId) < labOptions.size())
        request.choice.inputName = labOptions[toIndex (inputId)].deviceName;

    roundTripButton.setEnabled (false);
    labResult.setColour (juce::Label::textColourId, theme::text);
    labResult.setText ("Running... the panel stays blocked for about two seconds.",
                       juce::dontSendNotification);

    // Deliberately synchronous and short: the run owns the device, and pretending it is
    // asynchronous while sharing a device manager would be a lie the user pays for later.
    const auto outcome = nux::lab::runDeviceLoopback (request);

    const auto level = juce::String (nux::lab::getVerificationName (outcome.result.verification));

    auto text = juce::String ("Result: ") + level.toUpperCase()
              + (outcome.verdict.isEmpty() ? juce::String (" (judged clean)")
                                           : " - " + outcome.verdict)
              + "\n" + outcome.result.summary;

    if (outcome.result.captured)
        text += "\nCaptured " + juce::String (outcome.result.numSamplesCaptured)
              + " samples, " + juce::String (outcome.result.received.peakDb, 1) + " dBFS peak, "
              + juce::String (outcome.result.gainDb, 2) + " dB gain change.";

    labResult.setColour (juce::Label::textColourId,
                         outcome.result.verification == nux::lab::Verification::skipped ? theme::warn
                                                                                        : theme::text);
    labResult.setText (text, juce::dontSendNotification);

    roundTripButton.setEnabled (true);
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

    // The lab needs two choosers, two buttons and two lines of result.
    auto labArea = area.removeFromBottom (108).reduced (0, 4);

    labTitle.setBounds (labArea.removeFromTop (14));
    labHint.setBounds (labArea.removeFromTop (26));

    auto labRow = labArea.removeFromTop (24);
    outputChooser.setBounds (labRow.removeFromLeft (labRow.getWidth() / 2 - 4));
    labRow.removeFromLeft (8);
    inputChooser.setBounds (labRow.removeFromLeft (labRow.getWidth() - 190));
    labRow.removeFromLeft (8);
    openDeviceButton.setBounds (labRow.removeFromLeft (86));
    labRow.removeFromLeft (6);
    roundTripButton.setBounds (labRow);

    labResult.setBounds (labArea);

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
