#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Hrtf.h"

// One player: mono in -> stereo out, delayed by its distance and placed by its azimuth,
// either with a stereo pan (speakers) or an HRTF (binaural, headphones).
// Realtime-safe after prepare(): no allocation, locks or I/O.
class Spatializer
{
public:
    enum class Mode { stereo, binaural };

    // Worst case is the diagonal of the largest room: 50 m * sqrt 2 / 343 m/s ~ 206 ms.
    static constexpr double maxDelaySeconds = 0.25;

    // The delay never glides (that would bend the pitch - Doppler) and doesn't chase a
    // moving player (a chain of crossfades flutters). It holds until the target has been
    // still for `delaySettleSeconds` (or the player has kept moving for `delayMaxWaitSeconds`),
    // then crossfades once from the old delay tap to the new one.
    static constexpr double delayFadeSeconds = 0.08;
    static constexpr double delaySettleSeconds = 0.1;
    static constexpr double delayMaxWaitSeconds = 0.5;

    // Direction and mode changes crossfade over this long. Neighbouring HRIRs (1 degree
    // apart) are nearly identical, so chained direction fades during a drag don't flutter.
    static constexpr double directionFadeSeconds = 0.02;

    // Allocates for this sample rate. Not on the audio thread.
    void prepare (double sampleRate);

    // Where the player should be heard: azimuth in degrees (0 = ahead, +90 = right,
    // ±180 = behind), delay in seconds, distance gain, and the air-absorption low-pass
    // cutoff in Hz (0 = none). The first target after prepare() is taken as-is; after
    // that gain and cutoff glide over ~50 ms (they can't bend the pitch).
    void setTarget (float azimuthDegrees, float delaySeconds, float gain = 1.0f, float airCutoffHz = 0.0f) noexcept;

    void setMode (Mode) noexcept;

    // `input` holds 1 or 2 channels (a stereo input is summed to mono). Writes `left` and
    // `right`; they may alias the input channels.
    void process (const float* const* input, int numInputChannels, float* left, float* right, int numSamples) noexcept;

private:
    float nextDelayedSample (float in) noexcept;
    void binaural (int index, float& left, float& right) const noexcept;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> line;
    double sampleRate = 44100.0;
    bool snapToTarget = true;

    // Delay taps in samples: `fromDelay` plays; while fading, `toDelay` fades in.
    float fromDelay = 0.0f, toDelay = 0.0f, targetDelay = 0.0f, lastTarget = 0.0f;
    float delayFade = 1.0f, delayFadeStep = 0.0f;   // 1 = not fading
    int stillFor = 0, waitingFor = 0;               // samples
    int settleSamples = 0, maxWaitSamples = 0;

    // Distance: gain and a one-pole air-absorption low-pass (coefficient 1 = bypass).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> distanceGain, airCoefficient;
    float airState = 0.0f;

    // Stereo: constant-power pan from the folded azimuth.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pan;

    // Binaural: the delayed mono signal, stored twice so the last `taps` samples are
    // always contiguous; `direction` plays, `nextDirection` fades in.
    const Hrtf* hrtf = nullptr;
    std::vector<float> history;
    int historyPos = 0;
    int direction = 0, nextDirection = 0, targetDirection = 0;
    float directionFade = 1.0f, fadeStep20ms = 0.0f;

    Mode mode = Mode::stereo, fadingFrom = Mode::stereo;
    float modeFade = 1.0f;                          // 1 = not switching
};
