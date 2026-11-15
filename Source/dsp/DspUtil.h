#pragma once

#include <juce_core/juce_core.h>

namespace dsp
{
// The processor hands the DSP at most this many samples at a time (a block fits the L1 cache,
// and one HRIR coefficient serves every output of it).
constexpr int maxBlock = 32;

// Raised-cosine fade gain for position 0..1; the two gains of a crossfade sum to 1.
inline float fadeGain (float position) noexcept
{
    return 0.5f - 0.5f * std::cos (position * juce::MathConstants<float>::pi);
}

// Coefficient for the one-pole low-pass y += a (x - y); 1 = pass through untouched.
inline float onePoleCoefficient (float cutoffHz, double sampleRate) noexcept
{
    if (cutoffHz <= 0.0f || cutoffHz >= 0.45f * (float) sampleRate)
        return 1.0f;

    return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoffHz / (float) sampleRate);
}
} // namespace dsp
