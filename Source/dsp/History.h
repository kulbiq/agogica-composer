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

    // The sample written `delay` samples before the newest one (0 = newest), with
    // 3rd-order Lagrange interpolation between samples.
    float readAt (float delay) const noexcept
    {
        delay = std::clamp (delay, 0.0f, (float) maxDelay());
        const auto base = std::max (0, (int) delay - 1);
        const auto t = delay - (float) base;

        const auto y0 = at (base), y1 = at (base + 1), y2 = at (base + 2), y3 = at (base + 3);
        const auto t1 = t - 1.0f, t2 = t - 2.0f, t3 = t - 3.0f;

        return -y0 * t1 * t2 * t3 / 6.0f
             + y1 * t * t2 * t3 / 2.0f
             - y2 * t * t1 * t3 / 2.0f
             + y3 * t * t1 * t2 / 6.0f;
    }

private:
    float at (int age) const noexcept { return buffer[(size_t) ((pos - age) & (size - 1))]; }

    std::vector<float> buffer;
    int size = 0, pos = 0;
};
