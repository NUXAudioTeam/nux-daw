/* NUX DAW - audio device configuration (WASAPI / DirectSound / ASIO where available). */
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../nux_engine/AudioEngine.h"
#include "Theme.h"

namespace nux
{
class AudioSettingsPanel final : public juce::Component
{
public:
    explicit AudioSettingsPanel (AudioEngine& engine);
    ~AudioSettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

    std::function<void()> onSettingsChanged;

private:
    void applySetup();
    void addDeviceTypeItems();

    AudioEngine& engine;
    juce::AudioDeviceSelectorComponent selector;

    juce::Label title;
    juce::Label status;
    juce::Label hint;
};
}
