#pragma once

#include <juce_events/juce_events.h>
#include "../Source/stage/HeadTrackingFile.h"
#include "engines/Decode.h" // wrap

// The tracker's output to Composer, on its own timer thread (every 16 ms): Invert, Recenter and Pause
// on each frame's reading as it is (no smoothing for now), and the head-tracking file.
// Nothing here waits for the page, so Composer keeps hearing the head while the window is
// hidden or the page is busy; the page only shows `current()`.
class PoseOutput final : private juce::HighResolutionTimer
{
public:
    explicit PoseOutput (const juce::File& file) : writer (file) { startTimer (1000 / 60); }

    ~PoseOutput() override
    {
        stopTimer();
        writer.write ({}); // no face: Composer eases back to straight ahead
    }

    // The engine's reading for one frame (engine thread). `found` false = no head.
    void frame (bool found, float yaw, float newPitch, float newRoll)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        lastFrame = now();
        hasHead = found;
        if (found)
            heading = yaw, pitch = newPitch, roll = newRoll;
    }

    // Message thread (the page's buttons).
    void setInvert (bool on)  { const juce::SpinLock::ScopedLockType sl (lock); invert = on; }
    void setPaused (bool on)  { const juce::SpinLock::ScopedLockType sl (lock); paused = on; }
    void recenter()           { const juce::SpinLock::ScopedLockType sl (lock); centre = (invert ? -1.0f : 1.0f) * heading; }

    // What Composer hears now, and whether it is live (any thread).
    std::pair<float, bool> current() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return { listenerYaw, live };
    }

private:
    // Frames older than this mean the engine has stalled: not live (Composer's limit is 0.5 s).
    static constexpr double staleSeconds = 0.4;

    static double now() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

    void hiResTimerCallback() override
    {
        headtracking::Pose pose;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            live = ! paused && hasHead && now() - lastFrame < staleSeconds;
            if (live)
            {
                listenerYaw = engines::wrap ((invert ? -1.0f : 1.0f) * heading - centre);
                pose = { listenerYaw, pitch, roll, (std::uint64_t) juce::Time::currentTimeMillis(), true };
            }
        }
        writer.write (pose); // only this thread writes (and the destructor, after stopTimer)
    }

    headtracking::Writer writer;
    mutable juce::SpinLock lock;
    double lastFrame = 0;
    float pitch = 0, roll = 0, heading = 0, listenerYaw = 0, centre = 0;
    bool invert = false, paused = false, live = false, hasHead = false;
};
