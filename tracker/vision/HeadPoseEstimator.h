#pragma once

#include <juce_graphics/juce_graphics.h>
#include <optional>
#include "../HeadObservation.h"
#include "HeadDetector.h"
#include "OnnxModel.h"

namespace tracker
{
// The head's orientation from a camera frame: the head detector, then the pose model on the
// detected head. The pose model is HFHPE (PINTO0309, 2026; code MIT, weights' licence to be
// confirmed before shipping; trained without non-commercial data), the joint ViT-T 128 graph:
// yaw, pitch and roll in one pass, each as (cos, sin), plus a certainty per angle (kappa).
// Loading takes seconds (CoreML compiles on first use): construct off the message thread.
class HeadPoseEstimator
{
public:
    explicit HeadPoseEstimator (const juce::File& modelsFolder); // throws when a model can't be loaded

    // Nothing when the frame shows no head. Throws when a model fails on the frame.
    std::optional<HeadObservation> estimate (const juce::Image& frame);

    juce::String provider() const { return poseModel.provider(); } // "CoreML" / "CPU"

private:
    static constexpr int inputSize = 128; // the pose model sees the head as 128 x 128

    HeadDetector headDetector;
    OnnxModel poseModel;
    std::vector<float> input;
};
} // namespace tracker
