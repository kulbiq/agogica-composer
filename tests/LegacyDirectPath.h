#pragma once

// The direct-sound stage as it was before block processing (commit 0c15d19), frozen as the
// reference the block version is checked against: one sample at a time, two HRIR convolutions
// per ear while a direction fades, the HRIR window written in stereo too. Test code only.
// One difference: `dot` is the plain sum (the x64 build summed in SSE lanes; same to rounding).

#include "../Source/dsp/Acoustics.h"
#include "../Source/dsp/DelayTap.h"
#include "../Source/dsp/Hrtf.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace legacy
{
inline float dot (const float* a, const float* b, int n) noexcept
{
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += a[i] * b[i];
    return sum;
}

class Placer
{
public:
    static constexpr double fadeSeconds = 0.02;

    void prepare (double sampleRate)
    {
        hrtf = &Hrtf::forSampleRate (sampleRate);
        taps = hrtf->taps();
        recent.assign ((size_t) taps * 2, 0.0f);
        recentPos = 0;
        pan.reset (sampleRate, 0.02);
        fadeStep = (float) (1.0 / (fadeSeconds * sampleRate));
        directionFade = modeFade = 1.0f;
        lastPan = 2.0f;
    }

    void setDirection (float azimuthDegrees, bool snap) noexcept
    {
        azimuthDegrees = std::remainder (azimuthDegrees, 360.0f);
        const auto newPan = juce::jlimit (-1.0f, 1.0f, geometry::foldedAzimuth (azimuthDegrees) / 90.0f);
        targetDirection = Hrtf::nearestIndex (azimuthDegrees);

        if (snap)
        {
            direction = nextDirection = targetDirection;
            directionFade = 1.0f;
            pan.setCurrentAndTargetValue (newPan);
            lastPan = 2.0f;
            return;
        }

        pan.setTargetValue (newPan);
    }

    void setMode (OutputMode newMode) noexcept
    {
        if (newMode == mode)
            return;

        fadingFrom = mode;
        modeFade = modeFade < 1.0f ? 1.0f - modeFade : 0.0f;
        mode = newMode;
    }

    void process (float in, float& left, float& right) noexcept
    {
        constexpr auto quarterPi = juce::MathConstants<float>::pi * 0.25f;

        if (const auto p = pan.getNextValue(); ! juce::exactlyEqual (p, lastPan))
        {
            lastPan = p;
            panLeft = std::cos ((p + 1.0f) * quarterPi);
            panRight = std::sin ((p + 1.0f) * quarterPi);
        }

        recentPos = recentPos + 1 < taps ? recentPos + 1 : 0;
        recent[(size_t) recentPos] = recent[(size_t) (recentPos + taps)] = in;

        const auto modeSwitching = modeFade < 1.0f;
        const auto needStereo = mode == OutputMode::stereo || (modeSwitching && fadingFrom == OutputMode::stereo);
        const auto needBinaural = mode == OutputMode::binaural || (modeSwitching && fadingFrom == OutputMode::binaural);

        float stereoL = 0.0f, stereoR = 0.0f, binauralL = 0.0f, binauralR = 0.0f;

        if (needStereo)
        {
            stereoL = in * panLeft;
            stereoR = in * panRight;
        }

        if (directionFade >= 1.0f && targetDirection != direction)
        {
            nextDirection = targetDirection;
            directionFade = 0.0f;
        }

        if (needBinaural)
        {
            binaural (direction, binauralL, binauralR);

            if (directionFade < 1.0f)
            {
                float nextL, nextR;
                binaural (nextDirection, nextL, nextR);
                const auto g = dsp::fadeGain (std::min (1.0f, directionFade + fadeStep));
                binauralL += g * (nextL - binauralL);
                binauralR += g * (nextR - binauralR);
            }
        }

        if (directionFade < 1.0f)
        {
            directionFade = std::min (1.0f, directionFade + fadeStep);

            if (directionFade >= 1.0f)
                direction = nextDirection;
        }

        const auto& [newL, newR] = mode == OutputMode::stereo ? std::pair { stereoL, stereoR } : std::pair { binauralL, binauralR };

        if (! modeSwitching)
        {
            left += newL;
            right += newR;
            return;
        }

        modeFade = std::min (1.0f, modeFade + fadeStep);
        const auto g = dsp::fadeGain (modeFade);
        const auto& [oldL, oldR] = mode == OutputMode::stereo ? std::pair { binauralL, binauralR } : std::pair { stereoL, stereoR };
        left += oldL + g * (newL - oldL);
        right += oldR + g * (newR - oldR);
    }

private:
    void binaural (int index, float& left, float& right) const noexcept
    {
        const auto* window = recent.data() + recentPos + 1;
        left = dot (hrtf->reversedIr (index, 0), window, taps);
        right = dot (hrtf->reversedIr (index, 1), window, taps);
    }

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pan;
    float panLeft = 0.0f, panRight = 0.0f, lastPan = 2.0f;
    const Hrtf* hrtf = nullptr;
    int taps = 0;
    std::vector<float> recent;
    int recentPos = 0;
    int direction = 0, nextDirection = 0, targetDirection = 0;
    float directionFade = 1.0f, fadeStep = 0.0f;
    OutputMode mode = OutputMode::stereo, fadingFrom = OutputMode::stereo;
    float modeFade = 1.0f;
};

class DirectPath
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        delay.prepare (sampleRate);
        placer.prepare (sampleRate);
        gain.reset (sampleRate, 0.05);
        airCoefficient.reset (sampleRate, 0.05);
        airState = 0.0f;
        snap = true;
    }

    void setAcoustics (const Acoustics& a) noexcept
    {
        const auto distance = geometry::distance (a.player, a.listener);
        setMode (a.mode);
        setTarget (geometry::azimuthDegrees (a.player, a.listener) - a.headYaw,
                   geometry::delayMs (a.player, a.listener) / 1000.0f,
                   geometry::distanceGain (distance, a.adjust.distanceExponent),
                   geometry::airCutoffHz (distance, a.adjust.air));
    }

    void setTarget (float azimuthDegrees, float delaySeconds, float newGain = 1.0f, float airCutoffHz = 0.0f) noexcept
    {
        const auto coefficient = dsp::onePoleCoefficient (airCutoffHz, sampleRate);
        delay.setTarget (delaySeconds * (float) sampleRate);
        placer.setDirection (azimuthDegrees, snap);

        if (snap)
        {
            gain.setCurrentAndTargetValue (newGain);
            airCoefficient.setCurrentAndTargetValue (coefficient);
            snap = false;
            return;
        }

        gain.setTargetValue (newGain);
        airCoefficient.setTargetValue (coefficient);
    }

    void setMode (OutputMode mode) noexcept { placer.setMode (mode); }

    void processSample (const History& history, float& left, float& right) noexcept
    {
        airState += airCoefficient.getNextValue() * (delay.read (history) - airState);
        placer.process (airState * gain.getNextValue(), left, right);
    }

private:
    double sampleRate = 44100.0;
    DelayTap delay;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain, airCoefficient;
    float airState = 0.0f;
    Placer placer;
    bool snap = true;
};
} // namespace legacy
