#pragma once

#include <cmath>
#include <optional>
#include "../HeadOrientation.h"

namespace tracker
{
inline float wrapDegrees (float degrees) { return std::remainder (degrees, 360.0f); } // [-180, 180]

// Turns the head the camera sees into the head Composer hears: the one place where Invert,
// Recenter and Pause are decided. No threads, no clock (tests/ListenerPoseTests.cpp).
class ListenerPose
{
public:
    // The estimator's reading for the latest camera frame, in the camera's frame; nothing = no head.
    void observe (std::optional<HeadOrientation> head) { cameraHead = head; }

    // Invert: for a camera that shows the head mirrored (left and right swapped).
    void setInverted (bool on) { inverted = on; }
    void setPaused (bool on)   { paused = on; }

    // "Where I look now is straight ahead." Needs a head: false, and nothing changes, without one.
    bool recenter()
    {
        if (cameraHead)
            centreCameraYawDegrees = cameraHead->yawDegrees;
        return cameraHead.has_value();
    }

    struct Reading
    {
        HeadOrientation listenerHead; // straight ahead while not live
        bool live = false;
    };

    // What Composer hears. The centre is kept in the camera's frame and the mirror applied last,
    // so Invert and Recenter give the same result in either order. A mirror swaps the sides the
    // head turns (yaw) and tilts (roll) to; nodding (pitch) looks the same in it.
    Reading current() const
    {
        if (paused || ! cameraHead)
            return {};

        const auto mirror = inverted ? -1.0f : 1.0f;
        return { { mirror * wrapDegrees (cameraHead->yawDegrees - centreCameraYawDegrees),
                   cameraHead->pitchDegrees,
                   mirror * cameraHead->rollDegrees },
                 true };
    }

private:
    std::optional<HeadOrientation> cameraHead;
    float centreCameraYawDegrees = 0;
    bool inverted = false, paused = false;
};
} // namespace tracker
