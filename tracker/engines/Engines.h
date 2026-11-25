#pragma once

#include <juce_graphics/juce_graphics.h>
#include <optional>
#include "Onnx.h"

namespace engines
{
// One head-pose estimate from a camera frame, degrees, in the camera's frame. yaw: 0 = facing
// the camera, + = turned to the person's right (PoseOutput's Invert flips it for Composer if a
// camera mirrors); pitch and roll as the model gives them.
struct Estimate
{
    float yaw = 0, pitch = 0, roll = 0;
    float confidence = 0;         // 0..1, the model's own (kappaConfidence)
    juce::Rectangle<float> box;   // the head used, normalised 0..1 (the preview's overlay)
    float detectMs = 0, poseMs = 0;
};

// PINTO's YOLOX Body-Head-Hand (Apache-2.0): heads from any direction, incl. from behind.
// Input 1x3x192x320 BGR 0-255 (the whole frame, stretched); output [N,7] =
// batch, class (0 body, 1 head, 2 hand), score, x1, y1, x2, y2 in input pixels.
class HeadDetector
{
public:
    explicit HeadDetector (const juce::File& modelsDir);

    // The head to follow in `frame` (in its pixels): the strongest at first, then the one
    // nearest to the last. `ms` receives the time taken. Throws on an unexpected model output.
    std::optional<Box> find (const juce::Image& frame, float& ms);

private:
    Model model;
    std::vector<float> input;
    std::optional<Box> previous;
};

// The head's orientation from a camera frame: the head detector, then HFHPE (PINTO0309, 2026;
// code MIT, weights' licence to be confirmed before shipping; trained without non-commercial
// data) on the detected head - the joint ViT-T 128 graph: yaw, pitch and roll in one pass, each
// as (cos, sin), plus a confidence per angle (kappa). Input: the crop contract (hfhpeCrop) of
// the head box, RGB, x / 127.5 - 1.
// Loading takes seconds (CoreML compiles on first use): construct off the message thread.
// Throws when a model can't be loaded.
class HeadPoseEstimator
{
public:
    explicit HeadPoseEstimator (const juce::File& modelsDir);

    // Nothing when the frame shows no head. Throws when a model fails on the frame.
    std::optional<Estimate> estimate (const juce::Image& frame);

    juce::String provider() const { return model.provider(); } // "CoreML" / "CPU"

private:
    static constexpr int inputSize = 128;

    HeadDetector heads;
    Model model;
    std::vector<float> input;
};
} // namespace engines
