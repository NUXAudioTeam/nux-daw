/* NUX DAW - built-in virtual instrument.
   A small polyphonic synth whose character is selected by the track preset:
   drum kit, sub bass, pluck, pad and electric keys. It exists so that NUX makes
   sound out of the box before VST3 instruments are wired into the graph. */
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace nux
{
enum InstrumentPreset
{
    presetAnalogKit = 0,
    presetSubBass   = 1,
    presetPluck     = 2,
    presetPad       = 3,
    presetKeys      = 4,
    numInstrumentPresets = 5
};

juce::StringArray getInstrumentPresetNames();

/** Every NUX track plays every note on every channel. */
class NuxSynthSound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

/** Parameters shared between the message thread and the voices. */
struct SynthParameters
{
    std::atomic<int>   preset    { presetAnalogKit };
    std::atomic<float> cutoffHz  { 12000.0f };
    std::atomic<float> resonance { 0.7f };
};

/** One voice. Waveform, envelope and filtering are all derived from the preset
    and its ratio of the voice's frequency, so the same voice class covers every
    instrument without branching inside the sample loop where it can be avoided. */
class SynthVoice final : public juce::SynthesiserVoice
{
public:
    explicit SynthVoice (SynthParameters& params);

    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

private:
    void updateFilter();
    float renderNoise (int channel);

    SynthParameters& params;

    juce::dsp::Oscillator<float> osc1, osc2;
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::ADSR ampEnv;
    juce::ADSR filterEnv;
    juce::Random random;

    double sampleRate = 44100.0;
    float frequency = 440.0f;
    float velocityGain = 0.0f;
    double pitchEnvValue = 0.0;
    double pitchEnvDecay = 0.0;
    float pitchEnvAmount = 0.0f;
    int preset = presetAnalogKit;
    int drumSound = 0;
};
}
