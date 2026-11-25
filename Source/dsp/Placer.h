#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "OutputMode.h"
#include "Fir.h"
#include "History.h"
#include "Hrtf.h"

// Places one mono signal at an azimuth: a constant-power stereo pan (speakers) or an
// HRTF (binaural, headphones). Direction and mode changes crossfade over 20 ms;
// neighbouring HRIRs (1 degree apart) are nearly identical, so chained direction fades
// during a drag don't flutter.
//
// Works on blocks of up to History::maxBlock samples. A direction fade over a few degrees (a
// turning head, a dragged player: nearly every fade) convolves once per ear with a blend of the
// two HRIRs, the blend stepping once per block: fading the two convolutions' outputs (as larger
// jumps still do) at half the cost, since convolution is linear. The steps put it within 1-2 %
// RMS of the per-sample fade for white noise, whatever the step up to 5 degrees (neighbouring
// SADIE II responses differ mostly in the highs). On larger jumps they would be heard as a
// zipper: +36 % sample-to-sample change on a 90-degree jump of a 440 Hz tone.
class Placer
{
public:
    static constexpr double fadeSeconds = 0.02;
    static constexpr int blendDegrees = 5;

    // The HRIR changes only once the angle is this far past the half-way point to the next
    // one: a head tracker's jitter around x.5 degrees would otherwise fade back and forth.
    static constexpr float hysteresisDegrees = 0.35f;

    // Allocates. Not on the audio thread.
    void prepare (double sampleRate);

    // Azimuth in degrees (0 = ahead, +90 = right, ±180 = behind). Stereo folds rear
    // angles to the front; binaural doesn't. `snap` jumps without a fade.
    void setDirection (float azimuthDegrees, bool snap) noexcept;
    void setMode (OutputMode, bool snap = false) noexcept;

    // Forgets the sound heard so far (the HRIR's input). Doesn't allocate.
    void clear() noexcept;

    // Adds the placed block (n <= History::maxBlock) to `left` / `right`.
    void process (const float* in, float* left, float* right, int n) noexcept;

private:
    void pushWindow (const float* in, int n) noexcept;
    void binaural (int n, float* left, float* right) noexcept; // sets, doesn't add
    const float* ir (int direction, int ear) const noexcept;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pan;
    float panLeft = 0.0f, panRight = 0.0f, lastPan = 2.0f; // gains for `lastPan` (2 = none yet)

    const Hrtf* hrtf = nullptr;
    int taps = 0;
    dsp::ConvolveTwo convolve = nullptr;

    // The HRIR input, oldest first: window[end - taps .. end) are the latest `taps` samples.
    // Written only while binaural is heard; spare room after `end` lets runs of outputs be
    // computed 8 at a time, and lets the latest samples move to the front only now and then.
    std::vector<float> window;
    int end = 0;
    int filled = 0;    // samples written since the window was last emptied (up to `taps`)
    bool live = true;  // the window holds the input up to now (false after a stretch in stereo)

    int direction = 0, nextDirection = 0, targetDirection = 0;
    int fadeLength = 1, fadePosition = 1; // direction fade, in samples; done when position >= length
    std::vector<float> blended;           // the two HRIRs mixed for the current block (left, right)

    OutputMode mode = OutputMode::stereo, fadingFrom = OutputMode::stereo;
    float modeFade = 1.0f, fadeStep = 0.0f; // 1 = not switching
};
