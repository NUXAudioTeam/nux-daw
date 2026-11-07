/* NUX DAW - deferred destruction of hosted plugins.

   Loading or unloading a plugin while an audio block is in flight is a classic
   way to crash a DAW: the block is inside plugin->processBlock() while the
   message thread frees the instance underneath it.

   NUX edits the graph with the device stopped, which already removes the race for
   the normal paths. This queue is the belt-and-braces layer: retired instances are
   parked here and destroyed on a timer once a few hundred milliseconds have
   passed, so even a block that was somehow still running can finish first.

   Instances are always destroyed on the message thread, which is also where VST3
   expects its objects to die. */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

namespace nux
{
class PluginSlot;

class PluginRetirementQueue final : private juce::Timer,
                                   public juce::DeletedAtShutdown
{
    // DeletedAtShutdown (not a deliberately leaked static): JUCE deletes it after the
    // message loop has stopped, which is late enough for window teardown but early
    // enough that the Debug leak detector stays quiet. A leaked singleton tripped the
    // leak detector on every Debug run of the test suite.

public:
    /** Creates the shared queue on first use. Ownership is handled by
        DeletedAtShutdown, so the Debug leak detector stays quiet. */
    static PluginRetirementQueue& getInstance();

    /** Takes ownership of a plugin slot and destroys it later. Message thread only. */
    void retire (std::unique_ptr<PluginSlot> slot);

    /** Destroys everything waiting now. Call before the message loop goes away. */
    void flush();

    int getPendingCount() const noexcept { return (int) pending.size(); }

private:
    PluginRetirementQueue();
    ~PluginRetirementQueue() override;

    void timerCallback() override;

    struct Entry
    {
        std::unique_ptr<PluginSlot> slot;
        juce::int64 retireTimeMs = 0;
    };

    static constexpr juce::int64 graceMilliseconds = 400;

    std::vector<Entry> pending;
    bool timerRunning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginRetirementQueue)
};
}
