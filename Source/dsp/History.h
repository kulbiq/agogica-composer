#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

// The player's mono input, written by the processor up to `maxBlock` samples at a time before
// those samples are processed as one block. It is read at any (fractional) delay: there is no
// read pointer, so several readers wouldn't depend on each other or on their order.
class History
{
public:
    // The most samples written at once (a block fits the L1 cache, and one HRIR coefficient
    // serves every output of it).
    static constexpr int maxBlock = 32;

    // The longest delay anyone asks for: the longest direct path (opposite corners of the
    // largest room, 70.7 m: 0.206 s) stretched 4x (dsp/DelayStretch.h): 0.825 s, with margin.
    static constexpr double maxDelaySeconds = 0.85;

    // Allocates. Not on the audio thread.
    void prepare (int maxDelaySamples)
    {
        size = 1;
        while (size < maxDelaySamples + 8 + maxBlock)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        pos = 0;
    }

    // Silence, as if nothing had been written yet. Doesn't allocate.
    void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    // Reads within a block reach up to maxBlock - 1 samples further back than their delay.
    int maxDelay() const noexcept { return size - 8 - maxBlock; }
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

    // `age`: how many samples before the newest one this reading is for (in a block of n
    // samples just written, sample i is n - 1 - i samples old).
    float read (const Tap& tap, int age = 0) const noexcept
    {
        const auto base = tap.base + age;
        return tap.weights[0] * at (base) + tap.weights[1] * at (base + 1)
             + tap.weights[2] * at (base + 2) + tap.weights[3] * at (base + 3);
    }

private:
    float at (int age) const noexcept { return buffer[(size_t) ((pos - age) & (size - 1))]; }

    std::vector<float> buffer;
    int size = 0, pos = 0;
};
