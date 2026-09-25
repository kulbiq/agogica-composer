#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/PluginProcessor.h"
#include "../Source/dsp/Spatializer.h"

using Catch::Matchers::WithinAbs;

// Counts heap allocations while `countAllocations` is set (replaces global new for this test binary).
static std::atomic<bool> countAllocations { false };
static std::atomic<int> allocations { 0 };

void* operator new (std::size_t size)
{
    if (countAllocations)
        ++allocations;

    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void operator delete (void* p) noexcept              { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
struct Output
{
    std::vector<float> left, right;
};

// Runs `input` (mono) through a spatializer in blocks of `blockSize`.
Output run (Spatializer& s, const std::vector<float>& input, int blockSize = 512)
{
    Output out { std::vector<float> (input.size()), std::vector<float> (input.size()) };

    for (size_t start = 0; start < input.size(); start += (size_t) blockSize)
    {
        const auto n = (int) std::min ((size_t) blockSize, input.size() - start);
        const float* in[] { input.data() + start };
        s.process (in, 1, out.left.data() + start, out.right.data() + start, n);
    }

    return out;
}

std::vector<float> impulse (size_t length)
{
    std::vector<float> v (length, 0.0f);
    v[0] = 1.0f;
    return v;
}

size_t peakIndex (const std::vector<float>& v)
{
    return (size_t) std::distance (v.begin(), std::max_element (v.begin(), v.end(), [] (float a, float b)
    {
        return std::abs (a) < std::abs (b);
    }));
}
} // namespace

TEST_CASE ("delay: an impulse arrives distance / 343 m/s later")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    constexpr int delaySamples = 1000;
    const auto delaySeconds = (float) (delaySamples / sampleRate);

    Spatializer s;
    s.prepare (sampleRate);
    s.setTarget (0.0f, delaySeconds);

    const auto out = run (s, impulse (2048));
    CHECK (peakIndex (out.left) == delaySamples);
    CHECK_THAT (out.left[delaySamples], WithinAbs (std::sqrt (0.5), 1e-4)); // centre: -3 dB each side
    CHECK_THAT (out.right[delaySamples], WithinAbs (std::sqrt (0.5), 1e-4));
}

TEST_CASE ("pan: constant-power law")
{
    Spatializer s;
    s.prepare (48000.0);

    const auto [pan, expectedLeft, expectedRight] = GENERATE (table<float, double, double> ({
        { -1.0f, 1.0, 0.0 },
        { 0.0f, std::sqrt (0.5), std::sqrt (0.5) },
        { 1.0f, 0.0, 1.0 },
        { -0.5f, std::cos (juce::MathConstants<double>::pi / 8), std::sin (juce::MathConstants<double>::pi / 8) },
    }));

    s.setTarget (pan, 0.0f);
    const auto out = run (s, std::vector<float> (256, 1.0f));
    CHECK_THAT (out.left.back(), WithinAbs (expectedLeft, 1e-4));
    CHECK_THAT (out.right.back(), WithinAbs (expectedRight, 1e-4));
}

TEST_CASE ("a player at the listener: no delay, centre")
{
    Spatializer s;
    s.prepare (48000.0);
    s.setTarget (0.0f, 0.0f);

    const auto out = run (s, impulse (64));
    CHECK (peakIndex (out.left) == 0);
}

TEST_CASE ("stereo input is summed to mono")
{
    Spatializer s;
    s.prepare (48000.0);
    s.setTarget (-1.0f, 0.0f); // hard left: left = the mono sum

    std::vector<float> l (64, 1.0f), r (64, 0.0f), left (64), right (64);
    const float* in[] { l.data(), r.data() };
    s.process (in, 2, left.data(), right.data(), 64);
    CHECK_THAT (left.back(), WithinAbs (0.5, 1e-4));
}

TEST_CASE ("delay changes don't click and don't bend the pitch (no Doppler)")
{
    constexpr double sampleRate = 48000.0;
    Spatializer s;
    s.prepare (sampleRate);
    s.setTarget (0.0f, 0.0f);

    std::vector<float> sine (24000);
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / (float) sampleRate);

    Output out { std::vector<float> (sine.size()), std::vector<float> (sine.size()) };
    const float* first[] { sine.data() };
    s.process (first, 1, out.left.data(), out.right.data(), 4800);

    s.setTarget (0.0f, 0.01f); // jump 10 ms; the fade happens 100 ms later and lasts 80 ms
    const float* second[] { sine.data() + 4800 };
    s.process (second, 1, out.left.data() + 4800, out.right.data() + 4800, 19200);

    // An unfaded jump would step by up to ~1.4; a 440 Hz sine moves < 0.058 per sample.
    float maxStep = 0.0f;
    for (size_t i = 1; i < out.left.size(); ++i)
        maxStep = std::max (maxStep, std::abs (out.left[i] - out.left[i - 1]));

    CHECK (maxStep < 0.065f);

    // Pitch: zero crossings over the 400 ms after the change must match the input; a delay
    // gliding by 10 ms over 50 ms (Doppler) would lose ~9 of them.
    auto crossings = [] (const float* v, int n)
    {
        int count = 0;
        for (int i = 1; i < n; ++i)
            count += (v[i - 1] < 0.0f) != (v[i] < 0.0f);
        return count;
    };

    CHECK (std::abs (crossings (out.left.data() + 4800, 19200) - crossings (sine.data() + 4800, 19200)) <= 1);
}

namespace
{
std::vector<float> whiteNoise (size_t length, int seed)
{
    juce::Random rng (seed);
    std::vector<float> v (length);
    for (auto& x : v)
        x = rng.nextFloat() * 2.0f - 1.0f;
    return v;
}
} // namespace

TEST_CASE ("after a delay change the output is exactly the input at the new delay")
{
    Spatializer s;
    s.prepare (48000.0);
    s.setTarget (-1.0f, 100.0f / 48000.0f); // hard left: left = the delayed input

    const auto noise = whiteNoise (20000, 7);
    Output out { std::vector<float> (noise.size()), std::vector<float> (noise.size()) };
    const float* first[] { noise.data() };
    s.process (first, 1, out.left.data(), out.right.data(), 1000);

    s.setTarget (-1.0f, 700.0f / 48000.0f);
    const float* rest[] { noise.data() + 1000 };
    s.process (rest, 1, out.left.data() + 1000, out.right.data() + 1000, 19000);

    for (size_t i = 1000; i < 5800; ++i) // settling: still the old delay
        REQUIRE_THAT (out.left[i], WithinAbs (noise[i - 100], 1e-3));

    for (size_t i = 9640; i < noise.size(); ++i) // settled (100 ms) + faded (80 ms)
        REQUIRE_THAT (out.left[i], WithinAbs (noise[i - 700], 1e-3));
}

TEST_CASE ("a moving player keeps its delay until it stops (no fade chain)")
{
    Spatializer s;
    s.prepare (48000.0);
    s.setTarget (-1.0f, 100.0f / 48000.0f);

    const auto noise = whiteNoise (48000, 11);
    std::vector<float> left (noise.size()), right (noise.size());

    // 64-sample blocks; the target moves every block for 300 ms, then stays at 900 samples.
    for (size_t start = 0; start < noise.size(); start += 64)
    {
        const auto moving = start < 14400;
        s.setTarget (-1.0f, (moving ? 100.0f + (float) start / 18.0f : 900.0f) / 48000.0f);
        const float* in[] { noise.data() + start };
        s.process (in, 1, left.data() + start, right.data() + start, 64);
    }

    for (size_t i = 100; i < 14400; ++i) // while moving (< 0.5 s): untouched
        REQUIRE_THAT (left[i], WithinAbs (noise[i - 100], 1e-3));

    for (size_t i = 14400 + 4800 + 3840 + 64; i < noise.size(); ++i) // stopped, settled, faded
        REQUIRE_THAT (left[i], WithinAbs (noise[i - 900], 1e-3));
}

TEST_CASE ("a player moving for a long time still updates its delay every ~0.5 s")
{
    Spatializer s;
    s.prepare (48000.0);
    s.setTarget (-1.0f, 100.0f / 48000.0f);

    const auto noise = whiteNoise (48000, 13);
    std::vector<float> left (noise.size()), right (noise.size());

    for (size_t start = 0; start < noise.size(); start += 64)
    {
        s.setTarget (-1.0f, (100.0f + (float) start / 60.0f) / 48000.0f); // never stops
        const float* in[] { noise.data() + start };
        s.process (in, 1, left.data() + start, right.data() + start, 64);
    }

    // By 0.5 s + one 80 ms fade it no longer plays the initial 100-sample delay.
    float difference = 0.0f;
    for (size_t i = 28000; i < 30000; ++i)
        difference = std::max (difference, std::abs (left[i] - noise[i - 100]));

    CHECK (difference > 0.1f);
}

TEST_CASE ("block size doesn't change the result (1-sample blocks included)")
{
    std::vector<float> noise (4096);
    juce::Random rng (42);
    for (auto& v : noise)
        v = rng.nextFloat() * 2.0f - 1.0f;

    auto render = [&] (int blockSize)
    {
        Spatializer s;
        s.prepare (44100.0);
        s.setTarget (0.3f, 0.0123f);
        return run (s, noise, blockSize);
    };

    const auto big = render (512);
    const auto tiny = render (1);
    const auto odd = render (37);

    for (size_t i = 0; i < noise.size(); ++i)
    {
        REQUIRE_THAT (big.left[i], WithinAbs (tiny.left[i], 0.0));
        REQUIRE_THAT (big.right[i], WithinAbs (odd.right[i], 0.0));
    }
}

TEST_CASE ("processBlock: position from parameters, clamped to the room, no allocation")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    auto set = [&] (const char* id, float metres)
    {
        auto* param = p.state.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (metres));
    };

    set ("roomSize", 10.0f);
    set ("playerX", 20.0f); // clamped to the edge: x = 5
    set ("playerY", 5.0f);

    constexpr double sampleRate = 48000.0;
    p.setPlayConfigDetails (2, 2, sampleRate, 512);
    p.prepareToPlay (sampleRate, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    std::vector<float> left;

    for (int block = 0; block < 4; ++block)
    {
        buffer.clear();
        if (block == 0)
            buffer.setSample (0, 0, 1.0f);

        countAllocations = true;
        p.processBlock (buffer, midi);
        countAllocations = false;

        left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
    }

    CHECK (allocations == 0);

    const auto expected = std::hypot (5.0, 5.0) / 343.0 * sampleRate; // ~989.6 samples
    CHECK (std::abs ((double) peakIndex (left) - expected) <= 1.0);
}
