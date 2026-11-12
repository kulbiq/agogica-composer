#pragma once

#include <atomic>
#include <cstdint>

// Development monitoring: how much of real time this instance's processBlock takes.
// The audio thread adds each block (lock-free, no allocation: a few relaxed atomics); the
// editor's timer reads and resets. Not for offline renders (the caller skips those).
class LoadMeter
{
public:
    struct Reading
    {
        float average = 0; // busy time / real time over the period (0.25 = a quarter of one core)
        float peak = 0;    // the worst single block
        int overruns = 0;  // blocks that alone took longer than they last: a certain dropout
    };

    // Audio thread: `busySeconds` spent on a block lasting `blockSeconds` of audio.
    void add (double busySeconds, double blockSeconds) noexcept
    {
        if (blockSeconds <= 0)
            return;

        busy.fetch_add (busySeconds, std::memory_order_relaxed);
        audio.fetch_add (blockSeconds, std::memory_order_relaxed);

        const auto ratio = (float) (busySeconds / blockSeconds);
        for (auto prev = peak.load (std::memory_order_relaxed);
             prev < ratio && ! peak.compare_exchange_weak (prev, ratio, std::memory_order_relaxed);)
            ;
        if (ratio > 1.0f)
            overruns.fetch_add (1, std::memory_order_relaxed);
    }

    // Message thread: since the last read. A block landing mid-read can be split between two
    // periods; fine for a meter.
    Reading read() noexcept
    {
        const auto b = busy.exchange (0, std::memory_order_relaxed);
        const auto a = audio.exchange (0, std::memory_order_relaxed);
        return { a > 0 ? (float) (b / a) : 0.0f, peak.exchange (0, std::memory_order_relaxed),
                 overruns.exchange (0, std::memory_order_relaxed) };
    }

private:
    std::atomic<double> busy { 0 }, audio { 0 };
    std::atomic<float> peak { 0 };
    std::atomic<int> overruns { 0 };
    static_assert (std::atomic<double>::is_always_lock_free && std::atomic<float>::is_always_lock_free);
};
