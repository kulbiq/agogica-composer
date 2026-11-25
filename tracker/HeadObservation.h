#pragma once

#include <juce_graphics/juce_graphics.h>
#include "HeadOrientation.h"

namespace tracker
{
// The head found in one camera frame.
struct HeadObservation
{
    HeadOrientation orientation;                // as the camera sees it: yaw 0 = facing the camera
    float confidence = 0;                       // 0..1, the pose model's own (confidenceFromKappa)
    juce::Rectangle<float> headBoxNormalised;   // in the frame, 0..1 (the preview's overlay)
    float detectMs = 0, poseMs = 0;             // what the two models took
};
} // namespace tracker
