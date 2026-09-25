#pragma once

#include <juce_graphics/juce_graphics.h>
#include <optional>
#include "Decode.h"

namespace engines
{
// One head-pose estimate from a camera frame. Angles in each model's own sign convention
// (the tracker page's Invert makes "head turned right" positive per engine).
struct Estimate
{
    float yaw = 0, pitch = 0, roll = 0;
    float rawYaw = 0;             // before any reconstruction (diagnostics)
    float confidence = 0;
    juce::Rectangle<float> box;   // the head/person used, normalised 0..1 (debug overlay)
    float detectMs = 0, poseMs = 0;
};

class Engine
{
public:
    virtual ~Engine() = default;
    virtual std::optional<Estimate> estimate (const juce::Image& frame) = 0;
    virtual juce::String provider() const = 0; // "CoreML" / "CPU"
};

// "6drepnet360" (the chosen engine); models from `modelsDir`. Loading can take
// seconds (CoreML compiles on first use): call off the message thread. Throws on failure.
std::unique_ptr<Engine> makeEngine (const juce::String& name, const juce::File& modelsDir);
} // namespace engines
