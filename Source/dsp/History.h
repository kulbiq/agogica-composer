#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

// The player's recent mono input, written once per sample by the processor. Every stage
// reads it at any (fractional) delay - there is no read pointer, so stages don't depend
// on each other or on their order.
class History
{
public:
    // Allocates. Not on the audio thread.
    void prepare (int maxDelaySamples)
    {
        size = 1;
        while (size < maxDelaySamples + 8)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        pos = 0;
    }

    int maxDelay() const noexcept { return size - 8; }
    bool isPrepared() const noexcept { return size > 0; }

    void write (float sample) noexcept
    {
        pos = (pos + 1) & (size - 1);
        buffer[(size_t) pos] = sample;
    }

    // A read position: 3rd-order Lagrange interpolation at one fractional delay, its weights
    // worked out once. A path whose delay holds still (most of the time) reuses it every sample.
    struct Tap
    {
        int base = 0;
        float weights[4] {};
    };

    Tap tapAt (float delay) const noexcept
    {
        delay = std::clamp (delay, 0.0f, (float) maxDelay());
        Tap tap;
        tap.base = std::max (0, (int) delay - 1);
        const auto t = delay - (float) tap.base;
        const auto t1 = t - 1.0f, t2 = t - 2.0f, t3 = t - 3.0f;
        tap.weights[0] = -t1 * t2 * t3 / 6.0f;
        tap.weights[1] = t * t2 * t3 / 2.0f;
        tap.weights[2] = -t * t1 * t3 / 2.0f;
        tap.weights[3] = t * t1 * t2 / 6.0f;
        return tap;
    }

    float read (const Tap& tap) const noexcept
    {
        return tap.weights[0] * at (tap.base) + tap.weights[1] * at (tap.base + 1)
             + tap.weights[2] * at (tap.base + 2) + tap.weights[3] * at (tap.base + 3);
    }

    // The sample written `delay` samples before the newest one (0 = newest), interpolated.
    float readAt (float delay) const noexcept { return read (tapAt (delay)); }

private:
    float at (int age) const noexcept { return buffer[(size_t) ((pos - age) & (size - 1))]; }

    std::vector<float> buffer;
    int size = 0, pos = 0;
};
