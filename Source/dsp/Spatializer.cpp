#include "Spatializer.h"

void Spatializer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    line.setMaximumDelayInSamples ((int) std::ceil (maxDelaySeconds * sampleRate) + 4);
    line.prepare ({ sampleRate, 1, 1 });

    // Smoothing and fade times stay in milliseconds whatever the sample rate.
    pan.reset (sampleRate, 0.02);
    fadeStep = (float) (1.0 / (delayFadeSeconds * sampleRate));
    settleSamples = (int) (delaySettleSeconds * sampleRate);
    maxWaitSamples = (int) (delayMaxWaitSeconds * sampleRate);
    fadePosition = 1.0f;
    stillFor = waitingFor = 0;
    snapToTarget = true;
}

void Spatializer::setTarget (float newPan, float delaySeconds) noexcept
{
    const auto maxDelay = (float) line.getMaximumDelayInSamples();
    const auto samples = juce::jlimit (0.0f, maxDelay, delaySeconds * (float) sampleRate);
    newPan = juce::jlimit (-1.0f, 1.0f, newPan);

    targetDelay = samples;

    if (snapToTarget)
    {
        fromDelay = toDelay = lastTarget = samples;
        pan.setCurrentAndTargetValue (newPan);
        snapToTarget = false;
        return;
    }

    pan.setTargetValue (newPan);
}

void Spatializer::process (const float* const* input, int numInputChannels, float* left, float* right, int numSamples) noexcept
{
    constexpr auto quarterPi = juce::MathConstants<float>::pi * 0.25f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto in = numInputChannels > 1 ? 0.5f * (input[0][i] + input[1][i]) : input[0][i];

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
        if (fadePosition >= 1.0f && std::abs (targetDelay - fromDelay) > 1.0e-3f)
        {
            if (stillFor >= settleSamples || ++waitingFor >= maxWaitSamples)
            {
                toDelay = targetDelay;
                fadePosition = 0.0f;
                waitingFor = 0;
            }
        }
        else if (fadePosition >= 1.0f)
        {
            waitingFor = 0;
        }

        float out;

        if (fadePosition < 1.0f)
        {
            fadePosition = std::min (1.0f, fadePosition + fadeStep);

            // Raised-cosine gains that sum to 1: the two taps are usually near-identical
            // (small moves), where an equal-gain sum keeps the level steady.
            const auto g = 0.5f - 0.5f * std::cos (fadePosition * juce::MathConstants<float>::pi);
            const auto from = line.popSample (0, fromDelay, false);
            const auto to = line.popSample (0, toDelay, true);
            out = from + g * (to - from);

            if (fadePosition >= 1.0f)
                fromDelay = toDelay;
        }
        else
        {
            out = line.popSample (0, fromDelay, true);
        }

        // Constant-power pan law: -1..+1 -> 0..pi/2.
        const auto angle = (pan.getNextValue() + 1.0f) * quarterPi;
        left[i] = out * std::cos (angle);
        right[i] = out * std::sin (angle);
    }
}
