#pragma once

#include "Acoustics.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "DelayTap.h"

// Stage: the late reverb of the hall - an 8-line feedback delay network (Hadamard mixing)
// fed with the direct arrival plus a pre-delay. Its level doesn't depend on the player's
// distance (a diffuse field fills the room), so the direct-to-reverb balance is the
// distance cue. Left and right are decorrelated sums; no HRTF (a diffuse field has little
// interaural coherence). Silent (and nearly free) in the Dry hall.
class ReverbTail
{
public:
    static constexpr int numLines = 8;

    void prepare (double sampleRate);
    void setAcoustics (const Acoustics&) noexcept;
    void processSample (const History&, float& left, float& right) noexcept;
    void jumpToTargets() noexcept { input.jumpToTarget(); snap = true; }

private:
    // ponytail: every instance uses the same line lengths, so many tails reinforce the same
    // resonances; add a deterministic per-player length offset if that becomes audible.
    static constexpr std::array<double, numLines> lineMs { 29.7, 37.1, 41.1, 43.7, 53.9, 59.3, 67.1, 79.9 };

    struct Line
    {
        std::vector<float> buffer;
        int pos = 0;
        float feedback = 0.0f, damping = 0.0f, state = 0.0f; // first-order absorptive filter
    };

    std::array<Line, numLines> lines;
    DelayTap input;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> level;
    double sampleRate = 44100.0;
    const Hall* hall = nullptr;
    float decay = 0.0f; // the Pro decay multiplier the coefficients were computed for
    bool snap = true, silent = true, hasEnergy = false;
    int dryFor = 0, drySilentAfter = 0; // samples; in Dry, keep running until the tail has died away
};
