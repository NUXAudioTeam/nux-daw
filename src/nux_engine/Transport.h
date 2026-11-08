/* NUX DAW - transport state.

   Positions are expressed in 16th-note steps (see Constants.h), which keeps the
   sequencer, the timeline and the MIDI editor on the same time base. */
#pragma once

#include <atomic>
#include "../nux_core/Constants.h"

namespace nux
{
class Transport
{
public:
    void setTempo (double bpm) noexcept
    {
        tempo.store (juce::jlimit (constants::minTempo, constants::maxTempo, bpm));
    }

    double getTempo() const noexcept { return tempo.load(); }

    /** How many steps one sample advances. */
    double stepsPerSample (double sampleRate) const noexcept
    {
        return (tempo.load() / 60.0) * constants::stepsPerBeat / juce::jmax (1.0, sampleRate);
    }

    double getPositionSteps() const noexcept { return positionSteps.load(); }
    void setPositionSteps (double steps) noexcept { positionSteps.store (juce::jmax (0.0, steps)); }

    bool isPlaying() const noexcept { return playing.load(); }
    bool isRecording() const noexcept { return recording.load(); }
    void setPlaying (bool shouldPlay) noexcept { playing.store (shouldPlay); }
    void setRecording (bool shouldRecord) noexcept { recording.store (shouldRecord); }

    bool isLoopEnabled() const noexcept { return loopEnabled.load(); }
    void setLoopEnabled (bool shouldLoop) noexcept { loopEnabled.store (shouldLoop); }
    int getLoopLengthSteps() const noexcept { return loopLengthSteps.load(); }
    void setLoopLengthSteps (int steps) noexcept
    {
        loopLengthSteps.store (juce::jlimit (constants::minNumSteps, constants::maxNumSteps, steps));
    }

    void reset() noexcept
    {
        positionSteps.store (0.0);
        playing.store (false);
        recording.store (false);
    }

private:
    std::atomic<double> tempo { 120.0 };
    std::atomic<double> positionSteps { 0.0 };
    std::atomic<bool> playing { false };
    std::atomic<bool> recording { false };
    std::atomic<bool> loopEnabled { true };
    std::atomic<int> loopLengthSteps { constants::defaultNumSteps };
};
}
