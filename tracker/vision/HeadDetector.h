#pragma once

#include <juce_graphics/juce_graphics.h>
#include "HeadGeometry.h"
#include "OnnxModel.h"

namespace tracker
{
// Finds the head to follow in a camera frame: PINTO's YOLOX Body-Head-Hand (Apache-2.0), which
// knows heads from any direction, from behind included. It sees the whole frame stretched to
// 320 x 192.
class HeadDetector
{
public:
    explicit HeadDetector (const juce::File& modelsFolder); // throws when the model can't be loaded

    // The head to follow, in the frame's pixels (HeadGeometry.h: pickHead); nothing = no head.
    // Throws when the model fails or answers in an unexpected shape.
    std::optional<Box> find (const juce::Image& frame);

    juce::String provider() const { return model.provider(); }

private:
    static constexpr int inputWidth = 320, inputHeight = 192;

    OnnxModel model;
    std::vector<float> input;
    std::optional<Box> previousHead; // in the pixels of a frame of `previousFrameSize`
    juce::Rectangle<int> previousFrameSize;
};
} // namespace tracker
