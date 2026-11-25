#include "HeadTracking.h"
#include "../PluginProcessor.h"

HeadTracking::HeadTracking()
{
    startTimerHz (2); // idle: just notice when an instance wants tracking
}

HeadTracking::~HeadTracking()
{
    stopTimer();
}

void HeadTracking::timerCallback()
{
    pollNow();
}

void HeadTracking::pollNow()
{
    bool wanted = false;
    registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
            wanted = wanted || p->getParam (params::outputMode) > 0.5f;
    });

    if (getTimerInterval() != (wanted ? 10 : 500))
        startTimer (wanted ? 10 : 500);

    const auto pose = wanted ? reader.read() : std::nullopt;
    const auto now = (std::uint64_t) juce::Time::currentTimeMillis();
    const auto isLive = pose.has_value() && pose->face && now - pose->timestampMs < headtracking::staleAfterMs;

    live.store (isLive, std::memory_order_relaxed);

    // Live: follow the tracker as it is. Otherwise ease back to straight ahead.
    const auto current = yawDegrees.load (std::memory_order_relaxed);
    yawDegrees.store (isLive ? pose->yaw : current * 0.9f, std::memory_order_relaxed);
}

bool HeadTracking::startInstalledTracker()
{
   #if JUCE_MAC
    // The per-user install first: a build folder may hold another copy with the same identity.
    const auto installed = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                               .getChildFile ("Applications/Agogica Tracker.app");
    return installed.exists() && installed.startAsProcess();
   #else
    return false; // ponytail: the tracker isn't installed on Windows yet
   #endif
}
