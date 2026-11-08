#include "PluginRetirementQueue.h"
#include "PluginSlot.h"

namespace nux
{
PluginRetirementQueue& PluginRetirementQueue::getInstance()
{
    // Created on first use and destroyed by DeletedAtShutdown after the message loop
    // stops, so it is safe during window teardown and still cleaned up properly.
    static PluginRetirementQueue* instance = new PluginRetirementQueue();
    return *instance;
}

PluginRetirementQueue::PluginRetirementQueue()
{
    jassert (juce::MessageManager::existsAndIsCurrentThread());
}

PluginRetirementQueue::~PluginRetirementQueue()
{
    stopTimer();
    pending.clear();
}

void PluginRetirementQueue::retire (std::unique_ptr<PluginSlot> slot)
{
    if (slot == nullptr)
        return;

    pending.push_back ({ std::move (slot), juce::Time::currentTimeMillis() });

    if (! timerRunning)
    {
        timerRunning = true;
        startTimer (150);
    }
}

void PluginRetirementQueue::flush()
{
    stopTimer();
    timerRunning = false;
    pending.clear();
}

void PluginRetirementQueue::timerCallback()
{
    const auto now = juce::Time::currentTimeMillis();

    pending.erase (std::remove_if (pending.begin(), pending.end(),
                                   [now] (const Entry& entry)
                                   {
                                       return now - entry.retireTimeMs >= graceMilliseconds;
                                   }),
                   pending.end());

    if (pending.empty())
    {
        stopTimer();
        timerRunning = false;
    }
}
}
