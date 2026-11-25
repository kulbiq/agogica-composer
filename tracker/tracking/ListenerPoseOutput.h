#pragma once

#include <juce_core/juce_core.h>
#include "../../Source/stage/HeadTrackingFile.h"
#include "ListenerPose.h"

namespace tracker
{
// The tracker's output to Composer: what ListenerPose makes of each frame's head, written to the
// head-tracking file the moment it changes - with a new frame (tracking thread) or a button
// (message thread). Nothing here waits for the page.
// When frames stop coming nothing is written, and Composer sees the pose go stale by itself
// (headtracking::staleAfterMs).
class ListenerPoseOutput
{
public:
    explicit ListenerPoseOutput (const juce::File& headTrackingFile) : writer (headTrackingFile) {}

    ~ListenerPoseOutput() { writer.write ({}); } // no face: Composer eases back to straight ahead

    // False when the file couldn't be created: Composer then hears nothing from this app.
    bool isWritingToFile() const { return writer.isOpen(); }

    // Each returns what Composer hears from now on.
    ListenerPose::Reading observe (std::optional<HeadOrientation> cameraHead) { return change ([&] { pose.observe (cameraHead); }); }
    ListenerPose::Reading setInverted (bool on) { return change ([&] { pose.setInverted (on); }); }
    ListenerPose::Reading setPaused (bool on)   { return change ([&] { pose.setPaused (on); }); }
    ListenerPose::Reading recenter()            { return change ([&] { pose.recenter(); }); }

private:
    template <typename Change>
    ListenerPose::Reading change (Change&& changePose)
    {
        const juce::ScopedLock sl (lock); // also: the file allows one writer at a time
        changePose();
        const auto heard = pose.current();
        writer.write (heard.live ? headtracking::Pose { heard.listenerHead.yawDegrees, heard.listenerHead.pitchDegrees,
                                                        heard.listenerHead.rollDegrees,
                                                        (std::uint64_t) juce::Time::currentTimeMillis(), true }
                                 : headtracking::Pose {});
        return heard;
    }

    juce::CriticalSection lock;
    headtracking::Writer writer;
    ListenerPose pose;
};
} // namespace tracker
