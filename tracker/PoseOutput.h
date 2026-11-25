#pragma once

#include <juce_events/juce_events.h>
#include "../Source/stage/HeadTrackingFile.h"
#include "ListenerPose.h"

// The tracker's output to Composer, on its own timer thread (every 16 ms): what ListenerPose
// makes of each frame's reading, written to the head-tracking file.
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

    // The estimator's reading for one frame (engine thread); nothing = no head.
    void observe (std::optional<tracker::HeadOrientation> head) { locked ([&] { pose.observe (head, now()); }); }

    // Message thread (the page's buttons).
    void setInverted (bool on) { locked ([&] { pose.setInverted (on); }); }
    void setPaused (bool on)   { locked ([&] { pose.setPaused (on); }); }
    void recenter()            { locked ([&] { pose.recenter(); }); }

    // What Composer hears now (any thread).
    tracker::ListenerPose::Reading current() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return pose.at (now());
    }

private:
    static double now() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

    template <typename Change>
    void locked (Change&& change)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        change();
    }

    void hiResTimerCallback() override
    {
        const auto heard = current();
        writer.write (heard.live ? headtracking::Pose { heard.listenerHead.yawDegrees, heard.listenerHead.pitchDegrees,
                                                        heard.listenerHead.rollDegrees,
                                                        (std::uint64_t) juce::Time::currentTimeMillis(), true }
                                 : headtracking::Pose {}); // only this thread writes (and the destructor, after stopTimer)
    }

    headtracking::Writer writer;
    mutable juce::SpinLock lock;
    tracker::ListenerPose pose;
};
