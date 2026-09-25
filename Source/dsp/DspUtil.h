#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace dsp
{
// Raised-cosine fade gain for position 0..1; the two gains of a crossfade sum to 1.
inline float fadeGain (float position) noexcept
{
    return 0.5f - 0.5f * std::cos (position * juce::MathConstants<float>::pi);
}

inline float dot (const float* a, const float* b, int n) noexcept
{
   #if JUCE_CLANG
    #pragma clang fp reassociate(on) // lets the compiler vectorise the sum
   #endif
    float sum = 0.0f;

    for (int i = 0; i < n; ++i)
        sum += a[i] * b[i];

    return sum;
}

// Coefficient for the one-pole low-pass y += a (x - y); 1 = pass through untouched.
inline float onePoleCoefficient (float cutoffHz, double sampleRate) noexcept
{
    if (cutoffHz <= 0.0f || cutoffHz >= 0.45f * (float) sampleRate)
        return 1.0f;

    return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoffHz / (float) sampleRate);
}
// Schroeder allpass: flat magnitude, smears the signal in time. Used to diffuse
// reflections (a real surface scatters; a perfect copy of the direct sound comb-filters).
class Allpass
{
public:
    void prepare (int delaySamples)
    {
        buffer.assign ((size_t) std::max (1, delaySamples), 0.0f);
        pos = 0;
    }

    // g = 0 is a pure delay of the allpass length (no scattering).
    float process (float x, float g) noexcept
    {
        const auto delayed = buffer[(size_t) pos];
        const auto v = x + g * delayed;
        buffer[(size_t) pos] = v;
        pos = pos + 1 < (int) buffer.size() ? pos + 1 : 0;
        return delayed - g * v;
    }

private:
    std::vector<float> buffer;
    int pos = 0;
};
} // namespace dsp
