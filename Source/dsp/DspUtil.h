#pragma once

#include <juce_core/juce_core.h>

namespace dsp
{
// Raised-cosine fade gain for position 0..1; the two gains of a crossfade sum to 1.
inline float fadeGain (float position) noexcept
{
    return 0.5f - 0.5f * std::cos (position * juce::MathConstants<float>::pi);
}
} // namespace dsp
