#pragma once

#include "Acoustics.h"
#include "DelayTap.h"
#include "Placer.h"

// Stage: the direct sound from the player to the listener - delayed by the distance,
// quieter and darker with distance (air absorption), placed at its azimuth.
class DirectPath
{
public:
    void prepare (double sampleRate);
    void setAcoustics (const Acoustics&) noexcept;
    void processSample (const History&, float& left, float& right) noexcept;

    // The same, spelled out (tests set these directly). Gain and air cutoff glide over
    // ~50 ms (they can't bend the pitch); cutoff 0 = no air filter.
    void setTarget (float azimuthDegrees, float delaySeconds, float gain = 1.0f, float airCutoffHz = 0.0f) noexcept;
    void setMode (OutputMode mode) noexcept { placer.setMode (mode); }

private:
    double sampleRate = 44100.0;
    DelayTap delay;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain, airCoefficient;
    float airState = 0.0f;
    Placer placer;
    bool snap = true;
};
