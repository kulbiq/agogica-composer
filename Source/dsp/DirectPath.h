#pragma once

#include "Acoustics.h"
#include "Placer.h"

// The direct sound from the player to the listener: quieter and darker with distance (air
// absorption), placed at its azimuth. No propagation delay: every track stays in time.
class DirectPath
{
public:
    void prepare (double sampleRate);
    void setAcoustics (const Acoustics&) noexcept;

    // Adds the placed input block (n <= dsp::maxBlock) to left / right.
    void process (const float* in, float* left, float* right, int n) noexcept;

    // After silence: the next targets are taken as they are (there is nothing to glide).
    void jumpToTargets() noexcept { snap = true; }

    // The same, spelled out (tests set these directly). Gain and air cutoff glide over
    // ~50 ms; cutoff 0 = no air filter.
    void setTarget (float azimuthDegrees, float gain = 1.0f, float airCutoffHz = 0.0f) noexcept;
    void setMode (OutputMode mode) noexcept { placer.setMode (mode); }

private:
    double sampleRate = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain, airCoefficient;
    float airState = 0.0f;
    Placer placer;
    bool snap = true;
};
