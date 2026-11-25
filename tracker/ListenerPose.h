#pragma once

#include <cmath>
#include <optional>

namespace tracker
{
// Where a head points, in degrees. Yaw: 0 = straight ahead, + = turned to the person's right.
struct HeadOrientation
{
    float yawDegrees = 0, pitchDegrees = 0, rollDegrees = 0;
};

inline float wrapDegrees (float degrees) { return std::remainder (degrees, 360.0f); } // [-180, 180]

// Turns the head the camera sees into the head Composer hears: the one place where Invert,
// Recenter, Pause and "is tracking live" are decided. No threads, no clock of its own (the
// caller passes the time), so it is tested directly (tests/ListenerPoseTests.cpp).
class ListenerPose
{
public:
    // A camera frame older than this means tracking has stalled: not live.
    static constexpr double staleAfterSeconds = 0.4;

    // The estimator's reading for one camera frame, in the camera's frame; nothing = no head.
    void observe (std::optional<HeadOrientation> head, double nowSeconds)
    {
        lastObservedSeconds = nowSeconds;
        hasHead = head.has_value();
        if (head)
            cameraHead = *head;
    }

    // Invert: for a camera that shows the head mirrored.
    void setInverted (bool on) { inverted = on; }
    void setPaused (bool on)   { paused = on; }

    // "Where I look now is straight ahead."
    void recenter() { centreYawDegrees = yawSign() * cameraHead.yawDegrees; }

    struct Reading
    {
        HeadOrientation listenerHead; // straight ahead while not live
        bool live = false;
    };

    // What Composer hears at `nowSeconds`.
    Reading at (double nowSeconds) const
    {
        if (paused || ! hasHead || nowSeconds - lastObservedSeconds >= staleAfterSeconds)
            return {};

        return { { wrapDegrees (yawSign() * cameraHead.yawDegrees - centreYawDegrees),
                   cameraHead.pitchDegrees, cameraHead.rollDegrees },
                 true };
    }

private:
    float yawSign() const { return inverted ? -1.0f : 1.0f; }

    HeadOrientation cameraHead;    // the last head seen
    float centreYawDegrees = 0;
    double lastObservedSeconds = 0;
    bool hasHead = false, inverted = false, paused = false;
};
} // namespace tracker
