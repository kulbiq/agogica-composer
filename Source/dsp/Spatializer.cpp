#include "Spatializer.h"
#include "Geometry.h"

namespace
{
// Raised-cosine fade gain for position 0..1; the two gains of a crossfade sum to 1.
float fadeGain (float position) noexcept
{
    return 0.5f - 0.5f * std::cos (position * juce::MathConstants<float>::pi);
}

float dot (const float* a, const float* b, int n) noexcept
{
   #if JUCE_CLANG
    #pragma clang fp reassociate(on) // lets the compiler vectorise the sum
   #endif
    float sum = 0.0f;

    for (int i = 0; i < n; ++i)
        sum += a[i] * b[i];

    return sum;
}
} // namespace

void Spatializer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    line.setMaximumDelayInSamples ((int) std::ceil (maxDelaySeconds * sampleRate) + 4);
    line.prepare ({ sampleRate, 1, 1 });

    hrtf = &Hrtf::forSampleRate (sampleRate);
    history.assign ((size_t) hrtf->taps() * 2, 0.0f);
    historyPos = 0;

    // Smoothing and fade times stay in milliseconds whatever the sample rate.
    pan.reset (sampleRate, 0.02);
    delayFadeStep = (float) (1.0 / (delayFadeSeconds * sampleRate));
    fadeStep20ms = (float) (1.0 / (directionFadeSeconds * sampleRate));
    settleSamples = (int) (delaySettleSeconds * sampleRate);
    maxWaitSamples = (int) (delayMaxWaitSeconds * sampleRate);
    delayFade = directionFade = modeFade = 1.0f;
    stillFor = waitingFor = 0;
    snapToTarget = true;
}

void Spatializer::setTarget (float azimuthDegrees, float delaySeconds) noexcept
{
    const auto maxDelay = (float) line.getMaximumDelayInSamples();
    const auto samples = juce::jlimit (0.0f, maxDelay, delaySeconds * (float) sampleRate);
    const auto newPan = juce::jlimit (-1.0f, 1.0f, geometry::foldedAzimuth (azimuthDegrees) / 90.0f);

    targetDelay = samples;
    targetDirection = Hrtf::nearestIndex (azimuthDegrees);

    if (snapToTarget)
    {
        fromDelay = toDelay = lastTarget = samples;
        direction = nextDirection = targetDirection;
        pan.setCurrentAndTargetValue (newPan);
        snapToTarget = false;
        return;
    }

    pan.setTargetValue (newPan);
}

void Spatializer::setMode (Mode newMode) noexcept
{
    if (newMode == mode)
        return;

    // Switching back mid-fade starts from where the fade has got to.
    fadingFrom = mode;
    modeFade = modeFade < 1.0f ? 1.0f - modeFade : 0.0f;
    mode = newMode;
}

float Spatializer::nextDelayedSample (float in) noexcept
{
    line.pushSample (0, in);

    // Is the player still moving?
    if (std::abs (targetDelay - lastTarget) > 1.0e-3f)
    {
        lastTarget = targetDelay;
        stillFor = 0;
    }
    else
    {
        stillFor = std::min (stillFor + 1, settleSamples);
    }

    // One fade once it has settled (or has kept moving for a while).
    if (delayFade >= 1.0f && std::abs (targetDelay - fromDelay) > 1.0e-3f)
    {
        if (stillFor >= settleSamples || ++waitingFor >= maxWaitSamples)
        {
            toDelay = targetDelay;
            delayFade = 0.0f;
            waitingFor = 0;
        }
    }
    else if (delayFade >= 1.0f)
    {
        waitingFor = 0;
    }

    if (delayFade >= 1.0f)
        return line.popSample (0, fromDelay, true);

    delayFade = std::min (1.0f, delayFade + delayFadeStep);

    const auto from = line.popSample (0, fromDelay, false);
    const auto to = line.popSample (0, toDelay, true);

    if (delayFade >= 1.0f)
        fromDelay = toDelay;

    return from + fadeGain (delayFade) * (to - from);
}

void Spatializer::binaural (int index, float& left, float& right) const noexcept
{
    const auto taps = hrtf->taps();
    const auto* recent = history.data() + historyPos + 1; // oldest ... newest

    left = dot (hrtf->reversedIr (index, 0), recent, taps);
    right = dot (hrtf->reversedIr (index, 1), recent, taps);
}

void Spatializer::process (const float* const* input, int numInputChannels, float* left, float* right, int numSamples) noexcept
{
    constexpr auto quarterPi = juce::MathConstants<float>::pi * 0.25f;
    const auto taps = hrtf->taps();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto in = numInputChannels > 1 ? 0.5f * (input[0][i] + input[1][i]) : input[0][i];
        const auto out = nextDelayedSample (in);

        // Feed the HRIR history even in stereo mode, so switching to binaural is seamless.
        historyPos = historyPos + 1 < taps ? historyPos + 1 : 0;
        history[(size_t) historyPos] = history[(size_t) (historyPos + taps)] = out;

        const auto modeSwitching = modeFade < 1.0f;
        const auto needStereo = mode == Mode::stereo || (modeSwitching && fadingFrom == Mode::stereo);
        const auto needBinaural = mode == Mode::binaural || (modeSwitching && fadingFrom == Mode::binaural);

        float stereoL = 0.0f, stereoR = 0.0f, binauralL = 0.0f, binauralR = 0.0f;

        const auto angle = (pan.getNextValue() + 1.0f) * quarterPi;

        if (needStereo)
        {
            // Constant-power pan law: -1..+1 -> 0..pi/2.
            stereoL = out * std::cos (angle);
            stereoR = out * std::sin (angle);
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
                const auto g = fadeGain (std::min (1.0f, directionFade + fadeStep20ms));
                binauralL += g * (nextL - binauralL);
                binauralR += g * (nextR - binauralR);
            }
        }

        if (directionFade < 1.0f)
        {
            directionFade = std::min (1.0f, directionFade + fadeStep20ms);

            if (directionFade >= 1.0f)
                direction = nextDirection;
        }

        const auto& [newL, newR] = mode == Mode::stereo ? std::pair { stereoL, stereoR } : std::pair { binauralL, binauralR };

        if (! modeSwitching)
        {
            left[i] = newL;
            right[i] = newR;
            continue;
        }

        modeFade = std::min (1.0f, modeFade + fadeStep20ms);
        const auto g = fadeGain (modeFade);
        const auto& [oldL, oldR] = mode == Mode::stereo ? std::pair { binauralL, binauralR } : std::pair { stereoL, stereoR };
        left[i] = oldL + g * (newL - oldL);
        right[i] = oldR + g * (newR - oldR);
    }
}
