#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Acoustics.h"
#include "Hrtf.h"

// Places one mono signal at an azimuth: a constant-power stereo pan (speakers) or an
// HRTF (binaural, headphones). Direction and mode changes crossfade over 20 ms;
// neighbouring HRIRs (1 degree apart) are nearly identical, so chained direction fades
// during a drag don't flutter.
class Placer
{
public:
    static constexpr double fadeSeconds = 0.02;

    // Allocates. Not on the audio thread.
    void prepare (double sampleRate);

    // Azimuth in degrees (0 = ahead, +90 = right, ±180 = behind). Stereo folds rear
    // angles to the front; binaural doesn't. `snap` jumps without a fade.
    void setDirection (float azimuthDegrees, bool snap) noexcept;
    void setMode (OutputMode) noexcept;

    // Adds the placed sample to `left` / `right`.
    void process (float in, float& left, float& right) noexcept;

private:
    void binaural (int index, float& left, float& right) const noexcept;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pan;

    // The input, stored twice so the last `taps` samples are always contiguous.
    const Hrtf* hrtf = nullptr;
    std::vector<float> recent;
    int recentPos = 0;
    int direction = 0, nextDirection = 0, targetDirection = 0;
    float directionFade = 1.0f, fadeStep = 0.0f;

    OutputMode mode = OutputMode::stereo, fadingFrom = OutputMode::stereo;
    float modeFade = 1.0f; // 1 = not switching
};
