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

    // How binaural mode places the signal: the full HRIR, the ~2 ms HRIR (reflections),
    // or just the stereo pan (weak late reflections, where direction detail isn't heard).
    enum class Binaural { full, shortHrir, panOnly };

    // Allocates. Not on the audio thread.
    void prepare (double sampleRate, Binaural binauralQuality = Binaural::full);

    // Azimuth in degrees (0 = ahead, +90 = right, ±180 = behind). Stereo folds rear
    // angles to the front; binaural doesn't. `snap` jumps without a fade.
    void setDirection (float azimuthDegrees, bool snap) noexcept;
    void setMode (OutputMode) noexcept;

    // Adds the placed sample to `left` / `right`.
    void process (float in, float& left, float& right) noexcept;

private:
    void binaural (int index, float& left, float& right) const noexcept;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pan;
    float panLeft = 0.0f, panRight = 0.0f, lastPan = 2.0f; // gains for `lastPan` (2 = none yet)

    // The input, stored twice so the last `taps` samples are always contiguous.
    const Hrtf* hrtf = nullptr;
    Binaural quality = Binaural::full;
    int taps = 0;
    std::vector<float> recent;
    int recentPos = 0;
    int direction = 0, nextDirection = 0, targetDirection = 0;
    float directionFade = 1.0f, fadeStep = 0.0f;

    OutputMode mode = OutputMode::stereo, fadingFrom = OutputMode::stereo;
    float modeFade = 1.0f; // 1 = not switching
};
