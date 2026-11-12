#pragma once

#include "History.h"
#include "DspUtil.h"

// One delayed path (the direct sound, a reflection) read from the History.
// The delay never glides (that would bend the pitch - Doppler) and doesn't chase a
// moving source (a chain of crossfades flutters). It holds until the target has been
// still for `settleSeconds` (or has kept moving for `maxWaitSeconds`), then crossfades
// once from the old delay to the new one.
class DelayTap
{
public:
    static constexpr double fadeSeconds = 0.08;
    static constexpr double settleSeconds = 0.1;
    static constexpr double maxWaitSeconds = 0.5;

    void prepare (double sampleRate)
    {
        fadeStep = (float) (1.0 / (fadeSeconds * sampleRate));
        settleSamples = (int) (settleSeconds * sampleRate);
        maxWaitSamples = (int) (maxWaitSeconds * sampleRate);
        fade = 1.0f;
        stillFor = waitingFor = 0;
        snap = true;
        fromTapDelay = toTapDelay = -1.0f; // the History may have a new length
    }

    // In samples. The first target after prepare() is taken as-is.
    void setTarget (float delaySamples) noexcept
    {
        target = delaySamples;

        if (snap)
        {
            from = to = lastTarget = delaySamples;
            snap = false;
        }
    }

    // Call once per sample, after the History has been written.
    float read (const History& history) noexcept
    {
        // Is the source still moving?
        if (std::abs (target - lastTarget) > 1.0e-3f)
        {
            lastTarget = target;
            stillFor = 0;
        }
        else
        {
            stillFor = std::min (stillFor + 1, settleSamples);
        }

        // One fade once it has settled (or has kept moving for a while).
        if (fade >= 1.0f && std::abs (target - from) > 1.0e-3f)
        {
            if (stillFor >= settleSamples || ++waitingFor >= maxWaitSamples)
            {
                to = target;
                fade = 0.0f;
                waitingFor = 0;
            }
        }
        else if (fade >= 1.0f)
        {
            waitingFor = 0;
        }

        // The interpolation weights only when a delay changes (rarely): reading is most of the
        // reflections' cost, ten paths per player per sample.
        if (! juce::exactlyEqual (from, fromTapDelay))
            fromTap = history.tapAt (fromTapDelay = from);

        if (fade >= 1.0f)
            return history.read (fromTap);

        if (! juce::exactlyEqual (to, toTapDelay))
            toTap = history.tapAt (toTapDelay = to);

        fade = std::min (1.0f, fade + fadeStep);
        const auto a = history.read (fromTap);
        const auto b = history.read (toTap);

        if (fade >= 1.0f)
            from = to;

        return a + dsp::fadeGain (fade) * (b - a);
    }

    // The next target is taken as-is (after silence, when there's nothing to fade).
    void jumpToTarget() noexcept { snap = true; }

    // The delay currently heard (the one being faded from, during a fade).
    float current() const noexcept { return from; }

private:
    float from = 0.0f, to = 0.0f, target = 0.0f, lastTarget = 0.0f;
    History::Tap fromTap, toTap; // History::tapAt (from / to), kept while they hold
    float fromTapDelay = -1.0f, toTapDelay = -1.0f;
    float fade = 1.0f, fadeStep = 0.0f;
    int stillFor = 0, waitingFor = 0, settleSamples = 0, maxWaitSamples = 0;
    bool snap = true;
};
