#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#if JUCE_MSVC && defined (_M_X64) && ! JUCE_CLANG
 #define COMPOSER_DOT_BY_HAND 1
 #include <emmintrin.h> // dot: SSE by hand where the compiler won't vectorise
#endif

namespace dsp
{
// Raised-cosine fade gain for position 0..1; the two gains of a crossfade sum to 1.
inline float fadeGain (float position) noexcept
{
    return 0.5f - 0.5f * std::cos (position * juce::MathConstants<float>::pi);
}

// The HRIR convolutions' inner loop (most of Binaural's cost). Clang vectorises the plain loop
// once allowed to reorder the sum; MSVC has no such switch for one loop and keeps it scalar
// (32 binaural players took 2.8 cores on an i5-10210U), so x64 MSVC gets SSE by hand: two
// accumulators of four lanes (SSE2 is in every x64 CPU). An AVX2/FMA version picked at run
// time measured slower there (72 -> 85 % for 32 binaural players: the IRs are short, and the
// rest of the build is SSE).
inline float dot (const float* a, const float* b, int n) noexcept
{
   #if COMPOSER_DOT_BY_HAND
    __m128 sum0 = _mm_setzero_ps(), sum1 = _mm_setzero_ps();
    int i = 0;

    for (; i + 8 <= n; i += 8)
    {
        sum0 = _mm_add_ps (sum0, _mm_mul_ps (_mm_loadu_ps (a + i), _mm_loadu_ps (b + i)));
        sum1 = _mm_add_ps (sum1, _mm_mul_ps (_mm_loadu_ps (a + i + 4), _mm_loadu_ps (b + i + 4)));
    }

    alignas (16) float lanes[4];
    _mm_store_ps (lanes, _mm_add_ps (sum0, sum1));
    float sum = (lanes[0] + lanes[1]) + (lanes[2] + lanes[3]);

    for (; i < n; ++i)
        sum += a[i] * b[i];

    return sum;
   #else
    #if JUCE_CLANG
     #pragma clang fp reassociate(on) // lets the compiler vectorise the sum
    #endif
    float sum = 0.0f;

    for (int i = 0; i < n; ++i)
        sum += a[i] * b[i];

    return sum;
   #endif
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
