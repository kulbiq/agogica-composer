#pragma once

#include <juce_events/juce_events.h>
#include "StageRegistry.h"

#include "HeadTrackingFile.h"


// Per process: polls the file every 10 ms while some Composer instance is in Binaural
// (checked on the registry every tick; otherwise it idles at 2 Hz),
// and gives the audio threads a smoothed yaw. Data older than 0.5 s or no face: not live,
// and the yaw eases back to straight ahead.
class HeadTracking : private juce::Timer
{
public:
    HeadTracking();
    ~HeadTracking() override;

    float yaw() const noexcept   { return yawDegrees.load (std::memory_order_relaxed); }
    bool isLive() const noexcept { return live.load (std::memory_order_relaxed); }

    // One poll (the timer calls this; tests too, having no message loop).
    void pollNow();

private:
    void timerCallback() override;

    juce::SharedResourcePointer<StageRegistry> registry;
    headtracking::Reader reader { headtracking::defaultFile() };
    std::atomic<float> yawDegrees { 0.0f };
    std::atomic<bool> live { false };
};
