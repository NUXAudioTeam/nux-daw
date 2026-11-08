#include "Synth.h"

#include <cmath>

namespace nux
{
juce::StringArray getInstrumentPresetNames()
{
    return { "Analog Kit", "Sub Bass", "Pluck", "Warm Pad", "Electric Keys" };
}

namespace
{
/*  Drum sounds in the Analog Kit are selected by pitch, so a piano roll pattern
    can program kick, snare and hats without any extra plumbing. */
int drumSoundForNote (int note)
{
    if (note <= 37) return 0;   // kick
    if (note <= 40) return 1;   // snare
    if (note <= 44) return 2;   // closed hat
    if (note <= 47) return 3;   // open hat
    if (note <= 52) return 4;   // tom
    return 5;                   // perc / rim
}

juce::ADSR::Parameters envelopeFor (int preset)
{
    switch (preset)
    {
        case presetSubBass: return { 0.005f, 0.08f, 0.85f, 0.12f };
        case presetPluck:   return { 0.002f, 0.22f, 0.0f,  0.10f };
        case presetPad:     return { 0.45f,  0.60f, 0.75f, 0.80f };
        case presetKeys:    return { 0.004f, 0.55f, 0.25f, 0.25f };
        case presetAnalogKit:
        default:            return { 0.001f, 0.18f, 0.0f,  0.05f };
    }
}

/** Adds a little saturation so the built-in instruments glue together. */
inline float softClip (float x)
{
    return std::tanh (x);
}
}

SynthVoice::SynthVoice (SynthParameters& p) : params (p)
{
    osc1.initialise ([] (float phase) { return std::sin (phase * juce::MathConstants<float>::twoPi); }, 512);
    osc2.initialise ([] (float phase) { return 2.0f * phase - 1.0f; }, 512);   // saw
}

bool SynthVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<juce::SynthesiserSound*> (sound) != nullptr;
}

void SynthVoice::startNote (int midiNoteNumber, float velocity,
                            juce::SynthesiserSound*, int)
{
    preset = params.preset.load (std::memory_order_relaxed);
    drumSound = drumSoundForNote (midiNoteNumber);

    if (sampleRate <= 0.0) sampleRate = 44100.0;

    frequency = (float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
    velocityGain = juce::jmax (0.05f, velocity);

    auto envParams = envelopeFor (preset);

    if (preset == presetAnalogKit)
    {
        // Percussive one-shots with per-drum pitch and decay.
        switch (drumSound)
        {
            case 0: envParams = { 0.001f, 0.11f, 0.0f, 0.05f }; break;
            case 1: envParams = { 0.001f, 0.10f, 0.0f, 0.04f }; break;
            case 2: envParams = { 0.001f, 0.045f, 0.0f, 0.02f }; break;
            case 3: envParams = { 0.001f, 0.22f, 0.0f, 0.12f }; break;
            case 4: envParams = { 0.001f, 0.16f, 0.0f, 0.06f }; break;
            default: envParams = { 0.001f, 0.05f, 0.0f, 0.03f }; break;
        }
    }

    ampEnv.setSampleRate (sampleRate);
    ampEnv.setParameters (envParams);
    ampEnv.noteOn();

    filterEnv.setSampleRate (sampleRate);
    filterEnv.setParameters ({ 0.001f, 0.25f, 0.35f, 0.2f });
    filterEnv.noteOn();

    // Pitch envelope: a fast downward sweep gives drums their punch.
    pitchEnvAmount = 0.0f;
    pitchEnvDecay = 0.0;

    if (preset == presetAnalogKit)
    {
        if (drumSound == 0)      { pitchEnvAmount = 1.6f;  pitchEnvDecay = 0.055; }
        else if (drumSound == 4) { pitchEnvAmount = 0.55f; pitchEnvDecay = 0.04; }
        else if (drumSound == 1) { pitchEnvAmount = 0.20f; pitchEnvDecay = 0.03; }
    }
    else if (preset == presetPluck)
    {
        pitchEnvAmount = 0.06f;
        pitchEnvDecay = 0.01;
    }

    pitchEnvValue = pitchEnvAmount;

    osc1.setFrequency (frequency, true);
    osc2.setFrequency (preset == presetPad ? frequency * 1.005f : frequency, true);

    filter.prepare ({ sampleRate, 2048, 2 });
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    filter.setResonance (juce::jlimit (0.5f, 8.0f, params.resonance.load (std::memory_order_relaxed)));
    updateFilter();

    // Reset oscillator phases so repeated hits sound identical.
    osc1.reset();
    osc2.reset();
    filter.reset();
}

void SynthVoice::updateFilter()
{
    float baseCutoff = params.cutoffHz.load (std::memory_order_relaxed);

    if (preset == presetAnalogKit)
    {
        switch (drumSound)
        {
            case 0: baseCutoff = juce::jmin (baseCutoff, 260.0f); break;
            case 1: baseCutoff = juce::jmin (baseCutoff, 6500.0f); break;
            case 2: baseCutoff = juce::jmin (baseCutoff, 11000.0f); break;
            case 3: baseCutoff = juce::jmin (baseCutoff, 9000.0f); break;
            default: baseCutoff = juce::jmin (baseCutoff, 4200.0f); break;
        }
    }

    const auto nyquist = (float) (sampleRate * 0.45);
    const auto env = filterEnv.isActive() ? filterEnv.getNextSample() : 0.0f;
    const auto modded = baseCutoff * (1.0f + env * 1.5f);

    filter.setCutoffFrequency (juce::jlimit (40.0f, nyquist, modded));
}

void SynthVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        ampEnv.noteOff();
        filterEnv.noteOff();
    }
    else
    {
        ampEnv.reset();
        filterEnv.reset();
        clearCurrentNote();
    }
}

float SynthVoice::renderNoise (int)
{
    return random.nextFloat() * 2.0f - 1.0f;
}

void SynthVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                                 int startSample, int numSamples)
{
    if (! ampEnv.isActive())
    {
        clearCurrentNote();
        return;
    }

    const auto presetNow = params.preset.load (std::memory_order_relaxed);

    updateFilter();

    for (int i = 0; i < numSamples; ++i)
    {
        // Pitch envelope decay drives the drum sweep.
        if (pitchEnvValue > 0.0)
        {
            pitchEnvValue *= (1.0 - pitchEnvDecay * 0.02);

            const auto pitchMul = (float) std::pow (2.0, pitchEnvValue);
            osc1.setFrequency (juce::jlimit (8.0f, 18000.0f, frequency * pitchMul), false);
            osc2.setFrequency (juce::jlimit (8.0f, 18000.0f, frequency * pitchMul), false);
        }

        const auto amp = ampEnv.getNextSample();
        float sample = 0.0f;

        switch (presetNow)
        {
            case presetAnalogKit:
                switch (drumSound)
                {
                    case 0:   // kick: sine + click
                        sample = osc1.processSample (0.0f) * 0.95f
                               + renderNoise (0) * 0.06f * amp;
                        break;
                    case 1:   // snare: noise + body
                        sample = renderNoise (0) * 0.62f + osc1.processSample (0.0f) * 0.35f;
                        break;
                    case 2:   // closed hat
                    case 3:   // open hat
                        sample = renderNoise (0) * 0.55f;
                        break;
                    case 4:   // tom
                        sample = osc1.processSample (0.0f) * 0.8f + renderNoise (0) * 0.08f;
                        break;
                    default:  // perc
                        sample = renderNoise (0) * 0.4f + osc1.processSample (0.0f) * 0.3f;
                        break;
                }
                break;

            case presetSubBass:
                sample = osc1.processSample (0.0f) * 0.9f + osc2.processSample (0.0f) * 0.12f;
                break;

            case presetPluck:
                sample = osc2.processSample (0.0f) * 0.55f + osc1.processSample (0.0f) * 0.25f;
                break;

            case presetPad:
                sample = (osc2.processSample (0.0f) + osc1.processSample (0.0f)) * 0.28f;
                break;

            case presetKeys:
            default:
                sample = (osc1.processSample (0.0f) * 0.7f + osc2.processSample (0.0f) * 0.3f) * 0.7f;
                break;
        }

        sample *= amp * velocityGain * 0.9f;
        sample = filter.processSample (0, sample);
        sample = softClip (sample);

        for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
            outputBuffer.addSample (ch, startSample + i, sample);

        if (! ampEnv.isActive())
        {
            clearCurrentNote();
            break;
        }
    }
}
}
