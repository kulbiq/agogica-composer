#include "Placer.h"
#include "DspUtil.h"

void Placer::prepare (double sampleRate)
{
    hrtf = &Hrtf::forSampleRate (sampleRate);
    recent.assign ((size_t) hrtf->taps() * 2, 0.0f);
    recentPos = 0;

    pan.reset (sampleRate, 0.02);
    fadeStep = (float) (1.0 / (fadeSeconds * sampleRate));
    directionFade = modeFade = 1.0f;
}

void Placer::setDirection (float azimuthDegrees, bool snap) noexcept
{
    const auto newPan = juce::jlimit (-1.0f, 1.0f, geometry::foldedAzimuth (azimuthDegrees) / 90.0f);
    targetDirection = Hrtf::nearestIndex (azimuthDegrees);

    if (snap)
    {
        direction = nextDirection = targetDirection;
        directionFade = 1.0f;
        pan.setCurrentAndTargetValue (newPan);
        return;
    }

    pan.setTargetValue (newPan);
}

void Placer::setMode (OutputMode newMode) noexcept
{
    if (newMode == mode)
        return;

    // Switching back mid-fade starts from where the fade has got to.
    fadingFrom = mode;
    modeFade = modeFade < 1.0f ? 1.0f - modeFade : 0.0f;
    mode = newMode;
}

void Placer::binaural (int index, float& left, float& right) const noexcept
{
    const auto taps = hrtf->taps();
    const auto* window = recent.data() + recentPos + 1; // oldest ... newest

    left = dsp::dot (hrtf->reversedIr (index, 0), window, taps);
    right = dsp::dot (hrtf->reversedIr (index, 1), window, taps);
}

void Placer::process (float in, float& left, float& right) noexcept
{
    constexpr auto quarterPi = juce::MathConstants<float>::pi * 0.25f;
    const auto taps = hrtf->taps();

    // Feed the HRIR input even in stereo mode, so switching to binaural is seamless.
    recentPos = recentPos + 1 < taps ? recentPos + 1 : 0;
    recent[(size_t) recentPos] = recent[(size_t) (recentPos + taps)] = in;

    const auto modeSwitching = modeFade < 1.0f;
    const auto needStereo = mode == OutputMode::stereo || (modeSwitching && fadingFrom == OutputMode::stereo);
    const auto needBinaural = mode == OutputMode::binaural || (modeSwitching && fadingFrom == OutputMode::binaural);

    float stereoL = 0.0f, stereoR = 0.0f, binauralL = 0.0f, binauralR = 0.0f;

    // Constant-power pan law: -1..+1 -> 0..pi/2.
    const auto angle = (pan.getNextValue() + 1.0f) * quarterPi;

    if (needStereo)
    {
        stereoL = in * std::cos (angle);
        stereoR = in * std::sin (angle);
    }

    // Direction changes: fade to the latest direction once the previous fade is done.
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
