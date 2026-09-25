#pragma once

#include <juce_dsp/juce_dsp.h>

// One player: mono in -> stereo out, delayed by its distance and panned by its azimuth.
// Realtime-safe after prepare(): no allocation, locks or I/O.
class Spatializer
{
public:
    // Worst case is the diagonal of the largest room: 50 m * sqrt 2 / 343 m/s ~ 206 ms.
    static constexpr double maxDelaySeconds = 0.25;

    // Allocates the delay line for this sample rate. Not on the audio thread.
    void prepare (double sampleRate);

    // Where the player should be heard: pan -1..+1, delay in seconds. The first target
    // after prepare() is taken as-is. After that, pan follows at once (smoothed ~20 ms).
    // The delay never glides (that would bend the pitch - Doppler) and doesn't chase a
    // moving player (a chain of crossfades flutters). It holds until the target has been
    // still for `delaySettleSeconds` (or the player has kept moving for `delayMaxWaitSeconds`),
    // then crossfades once from the old delay tap to the new one.
    static constexpr double delayFadeSeconds = 0.08;
    static constexpr double delaySettleSeconds = 0.1;
    static constexpr double delayMaxWaitSeconds = 0.5;
    void setTarget (float pan, float delaySeconds) noexcept;

    // `input` holds 1 or 2 channels (a stereo input is summed to mono). Writes `left` and
    // `right`; they may alias the input channels.
    void process (const float* const* input, int numInputChannels, float* left, float* right, int numSamples) noexcept;

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> line;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pan;
    double sampleRate = 44100.0;
    bool snapToTarget = true;

    // Delay taps in samples: `fromDelay` plays; while fading, `toDelay` fades in.
    float fromDelay = 0.0f, toDelay = 0.0f, targetDelay = 0.0f, lastTarget = 0.0f;
    float fadePosition = 1.0f, fadeStep = 0.0f;   // fadePosition 1 = not fading
    int stillFor = 0, waitingFor = 0;             // samples
    int settleSamples = 0, maxWaitSamples = 0;
};
