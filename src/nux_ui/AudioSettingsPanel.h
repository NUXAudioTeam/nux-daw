/* NUX DAW - audio device configuration.

   Three things live here:

   * the device selector (driver, input, output, sample rate, buffer size),
   * a test tone, so the user can prove audio reaches the chosen output, and
   * input / output level meters, refreshed from the engine's own atomics.

   The panel only reads engine state and calls engine setters; it never touches the
   audio thread directly. */
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../nux_engine/AudioEngine.h"
#include "Theme.h"

namespace nux
{
class AudioSettingsPanel final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit AudioSettingsPanel (AudioEngine& engine);
    ~AudioSettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

    std::function<void()> onSettingsChanged;

private:
    void timerCallback() override;
    void drawMeter (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                    float peak, juce::Colour meterColour, bool showZeroWarning);
    void toggleTestTone();
    void updateTestToneButton();

    AudioEngine& engine;

    juce::AudioDeviceSelectorComponent selector;

    juce::Label title;
    juce::Label status;
    juce::Label hint;

    juce::TextButton testToneButton { "TEST TONE" };
    juce::Slider testToneLevel { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label testToneLabel;

    juce::Label deviceWarning;

    juce::Rectangle<int> inputMeterArea;
    juce::Rectangle<int> outputMeterArea;

    float smoothedInput = 0.0f;
    float smoothedOutput = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsPanel)
};
}
