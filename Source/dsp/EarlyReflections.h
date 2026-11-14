#pragma once

#include "Acoustics.h"
#include "DelayTap.h"
#include "Placer.h"

// Stage: the first wall, floor and ceiling reflections (image sources of the square
// room). Each arrives later and from its own direction; its level relative to the direct
// sound is physical (path length and wall absorption), so the room gets clearer, not
// muddier, as players move closer. Silent (and nearly free) in the Dry hall.
class EarlyReflections
{
public:
    void prepare (double sampleRate);
    void setAcoustics (const Acoustics&) noexcept;
    void processSample (const History&, float& left, float& right) noexcept;
    void jumpToTargets() noexcept
    {
        for (auto& r : reflections)
            r.delay.jumpToTarget();
        clearState();
        snap = true;
    }

private:
    // Filter and diffusion state: kept while silent, it would come back as old sound.
    void clearState() noexcept;

    struct Reflection
    {
        DelayTap delay;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain, airCoefficient;
        float airState = 0.0f;
        std::array<dsp::Allpass, 2> diffusion; // different lengths per reflection: decorrelated
    };

    std::array<Reflection, geometry::numImages> reflections;

    // Directions (image order from geometry::imageSources): the 4 walls get short HRIRs;
    // floor and ceiling come from the direct sound's direction, so they share one; the 4
    // weak corner (2nd-order) images are only panned, even in binaural.
    static constexpr std::array<int, geometry::numImages> placerOf { 0, 1, 2, 3, 4, 4, 5, 6, 7, 8 };
    std::array<Placer, 9> placers;
    std::array<float, 9> placerInput {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> diffusion; // allpass g
    double sampleRate = 44100.0;
    bool snap = true, silent = true;
};
