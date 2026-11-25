#pragma once

#include "Acoustics.h"
#include "DelayTap.h"
#include "Placer.h"

// Stage: the direct sound from the player to the listener - delayed by the distance,
// quieter with distance, placed at its azimuth.
class DirectPath
{
public:
    void prepare (double sampleRate);
    void setAcoustics (const Acoustics&) noexcept;

    // Adds the last n samples written to the History (n <= History::maxBlock) to left / right.
    void process (const History&, float* left, float* right, int n) noexcept;
    void jumpToTargets() noexcept { delay.jumpToTarget(); snap = true; }
    // After a gap in the input (bypass): nothing heard before it comes out after it.
    void clear() noexcept { placer.clear(); jumpToTargets(); }

    // The same, spelled out (tests set these directly). The gain glides over ~50 ms.
    void setTarget (float azimuthDegrees, float delaySeconds, float gain = 1.0f) noexcept;
    void setMode (OutputMode mode) noexcept { placer.setMode (mode); }

private:
    double sampleRate = 44100.0;
    DelayTap delay;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain;
    Placer placer;
    bool snap = true;
};
