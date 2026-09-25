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

    s.setTarget (pan * 90.0f, 0.0f); // pan -1..+1 = azimuth -90..+90
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
    s.setTarget (-90.0f, 0.0f); // -90 = hard left: left = the mono sum

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
    s.setTarget (-90.0f, 100.0f / 48000.0f); // hard left: left = the delayed input

    const auto noise = whiteNoise (20000, 7);
    Output out { std::vector<float> (noise.size()), std::vector<float> (noise.size()) };
    const float* first[] { noise.data() };
    s.process (first, 1, out.left.data(), out.right.data(), 1000);

    s.setTarget (-90.0f, 700.0f / 48000.0f);
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
    s.setTarget (-90.0f, 100.0f / 48000.0f);

    const auto noise = whiteNoise (48000, 11);
    std::vector<float> left (noise.size()), right (noise.size());

    // 64-sample blocks; the target moves every block for 300 ms, then stays at 900 samples.
    for (size_t start = 0; start < noise.size(); start += 64)
    {
        const auto moving = start < 14400;
        s.setTarget (-90.0f, (moving ? 100.0f + (float) start / 18.0f : 900.0f) / 48000.0f);
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
    s.setTarget (-90.0f, 100.0f / 48000.0f);

    const auto noise = whiteNoise (48000, 13);
    std::vector<float> left (noise.size()), right (noise.size());

    for (size_t start = 0; start < noise.size(); start += 64)
    {
        s.setTarget (-90.0f, (100.0f + (float) start / 60.0f) / 48000.0f); // never stops
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
        s.setTarget (27.0f, 0.0123f);
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

// ---- Binaural ----------------------------------------------------------------

#include "../Source/dsp/Hrtf.h"

namespace
{
double rms (const float* v, size_t n)
{
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i)
        sum += (double) v[i] * v[i];
    return std::sqrt (sum / (double) n);
}

double db (double ratio) { return 20.0 * std::log10 (ratio); }

Output render (Spatializer::Mode mode, float azimuth, const std::vector<float>& input, double sampleRate = 48000.0)
{
    Spatializer s;
    s.prepare (sampleRate);
    s.setMode (mode);
    s.setTarget (azimuth, 0.0f);
    return run (s, input);
}

size_t onset (const std::vector<float>& v)
{
    float peak = 0.0f;
    for (auto x : v)
        peak = std::max (peak, std::abs (x));

    for (size_t i = 0; i < v.size(); ++i)
        if (std::abs (v[i]) > 0.1f * peak)
            return i;

    return v.size();
}

// Largest sample-to-sample step within [from, to).
float maxStep (const std::vector<float>& v, size_t from, size_t to)
{
    float step = 0.0f;
    for (size_t i = from + 1; i < to; ++i)
        step = std::max (step, std::abs (v[i] - v[i - 1]));
    return step;
}
} // namespace

TEST_CASE ("HRTF tables: every rate, 1-degree grid, right ear louder at +90")
{
    const auto [rate, taps] = GENERATE (table<double, int> ({
        { 44100.0, 235 }, { 48000.0, 256 }, { 88200.0, 470 }, { 96000.0, 512 }, { 176400.0, 941 }, { 192000.0, 1024 },
    }));

    const auto& h = Hrtf::forSampleRate (rate);
    CHECK_THAT (h.sampleRate(), WithinAbs (rate, 0.5));
    CHECK (h.taps() == taps);

    auto energy = [&] (int index, int ear)
    {
        return rms (h.reversedIr (index, ear), (size_t) h.taps());
    };

    CHECK (db (energy (Hrtf::nearestIndex (90.0f), 1) / energy (Hrtf::nearestIndex (90.0f), 0)) > 10.0);
    CHECK (db (energy (Hrtf::nearestIndex (-90.0f), 0) / energy (Hrtf::nearestIndex (-90.0f), 1)) > 10.0);
}

TEST_CASE ("HRTF index wraps around the circle")
{
    CHECK (Hrtf::nearestIndex (0.0f) == 0);
    CHECK (Hrtf::nearestIndex (90.0f) == 90);
    CHECK (Hrtf::nearestIndex (-90.0f) == 270);
    CHECK (Hrtf::nearestIndex (180.0f) == 180);
    CHECK (Hrtf::nearestIndex (-180.0f) == 180);
    CHECK (Hrtf::nearestIndex (359.6f) == 0);
}

TEST_CASE ("binaural at the front is as loud as stereo at centre")
{
    const auto noise = whiteNoise (96000, 21);
    const auto stereo = render (Spatializer::Mode::stereo, 0.0f, noise);
    const auto binaural = render (Spatializer::Mode::binaural, 0.0f, noise);

    auto power = [] (const Output& o)
    {
        return rms (o.left.data() + 1024, o.left.size() - 1024) + rms (o.right.data() + 1024, o.right.size() - 1024);
    };

    CHECK (std::abs (db (power (binaural) / power (stereo))) < 1.0);
}

TEST_CASE ("binaural direction: level and arrival-time differences between the ears")
{
    const auto noise = whiteNoise (48000, 22);
    const auto right = render (Spatializer::Mode::binaural, 90.0f, noise);
    const auto left = render (Spatializer::Mode::binaural, -90.0f, noise);

    CHECK (db (rms (right.right.data(), noise.size()) / rms (right.left.data(), noise.size())) > 6.0);
    CHECK (db (rms (left.left.data(), noise.size()) / rms (left.right.data(), noise.size())) > 6.0);

    // Interaural time difference: at +90 the right ear hears it > 0.3 ms earlier.
    const auto click = render (Spatializer::Mode::binaural, 90.0f, impulse (1024));
    CHECK ((double) onset (click.left) - (double) onset (click.right) > 0.0003 * 48000.0);
}

TEST_CASE ("binaural tells front from back")
{
    const auto noise = whiteNoise (48000, 23);
    const auto front = render (Spatializer::Mode::binaural, 0.0f, noise);
    const auto back = render (Spatializer::Mode::binaural, 180.0f, noise);

    std::vector<float> difference (noise.size());
    for (size_t i = 0; i < noise.size(); ++i)
        difference[i] = front.left[i] - back.left[i];

    CHECK (rms (difference.data(), difference.size()) > 0.2 * rms (front.left.data(), noise.size()));
}

TEST_CASE ("direction jumps and mode switches don't click")
{
    constexpr double sampleRate = 48000.0;
    std::vector<float> sine (19200);
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / (float) sampleRate);

    Spatializer s;
    s.prepare (sampleRate);
    s.setMode (Spatializer::Mode::binaural);
    s.setTarget (0.0f, 0.0f);

    Output out { std::vector<float> (sine.size()), std::vector<float> (sine.size()) };
    auto play = [&] (size_t from, size_t n)
    {
        const float* in[] { sine.data() + from };
        s.process (in, 1, out.left.data() + from, out.right.data() + from, (int) n);
    };

    play (0, 4800);
    s.setTarget (120.0f, 0.0f); // big direction jump: one 20 ms fade
    play (4800, 4800);
    s.setMode (Spatializer::Mode::stereo);
    play (9600, 4800);
    s.setMode (Spatializer::Mode::binaural);
    play (14400, 4800);

    // Steps during the transitions stay within what the steady signals do on either side.
    for (auto* ear : { &out.left, &out.right })
    {
        const auto steady = std::max ({ maxStep (*ear, 1000, 4800), maxStep (*ear, 7000, 9600), maxStep (*ear, 12000, 14400) });
        CHECK (maxStep (*ear, 4800, 7000) <= 1.2f * steady);
        CHECK (maxStep (*ear, 9600, 12000) <= 1.2f * steady);
        CHECK (maxStep (*ear, 14400, 16800) <= 1.2f * steady);
    }
}

TEST_CASE ("binaural: block size doesn't change the result, no allocation")
{
    const auto noise = whiteNoise (8192, 24);

    auto renderInBlocks = [&] (int blockSize)
    {
        Spatializer s;
        s.prepare (48000.0);
        s.setMode (Spatializer::Mode::binaural);
        s.setTarget (-37.0f, 0.004f);

        countAllocations = true;
        auto out = run (s, noise, blockSize);
        countAllocations = false;
        return out;
    };

    allocations = 0;
    const auto big = renderInBlocks (512);
    const auto tiny = renderInBlocks (1);
    CHECK (allocations <= 4); // run() allocates its two output vectors per call; process() nothing

    for (size_t i = 0; i < noise.size(); ++i)
        REQUIRE_THAT (big.right[i], WithinAbs (tiny.right[i], 0.0));
}

TEST_CASE ("binaural CPU: 32 players", "[.perf]")
{
    const auto sampleRate = GENERATE (48000.0, 96000.0);
    constexpr int players = 32, block = 512;
    const auto seconds = 10.0;

    std::vector<Spatializer> spatializers (players);
    for (int p = 0; p < players; ++p)
    {
        spatializers[(size_t) p].prepare (sampleRate);
        spatializers[(size_t) p].setMode (Spatializer::Mode::binaural);
        spatializers[(size_t) p].setTarget ((float) (p * 11 % 360) - 180.0f, 0.01f);
    }

    const auto noise = whiteNoise (block, 25);
    std::vector<float> l (block), r (block);
    const float* in[] { noise.data() };
    const auto blocks = (int) (seconds * sampleRate / block);

    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < blocks; ++b)
        for (auto& s : spatializers)
            s.process (in, 1, l.data(), r.data(), block);
    const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;

    const auto load = elapsed / seconds;
    UNSCOPED_INFO (players << " binaural players at " << sampleRate << " Hz: " << juce::String (load * 100.0, 1) << " % of one core");
    CHECK (load < (sampleRate < 50000.0 ? 0.10 : 0.40));
}

// ---- Distance: loudness and air ------------------------------------------------

#include "../Source/dsp/Geometry.h"

namespace
{
std::vector<float> tone (double frequency, double sampleRate, size_t length)
{
    std::vector<float> v (length);
    for (size_t i = 0; i < length; ++i)
        v[i] = (float) std::sin (juce::MathConstants<double>::twoPi * frequency * (double) i / sampleRate);
    return v;
}
} // namespace

TEST_CASE ("distance gain scales the output")
{
    Spatializer s;
    s.prepare (48000.0);
    s.setTarget (-90.0f, 0.0f, 0.25f); // hard left, gain 0.25

    const auto out = run (s, std::vector<float> (256, 1.0f));
    CHECK_THAT (out.left.back(), WithinAbs (0.25, 1e-5));
}

TEST_CASE ("air absorption: ISO loss at 8 kHz, low frequencies untouched")
{
    constexpr double sampleRate = 48000.0;
    constexpr float metres = 50.0f;

    auto lossDb = [&] (double frequency)
    {
        Spatializer s;
        s.prepare (sampleRate);
        s.setTarget (-90.0f, 0.0f, 1.0f, geometry::airCutoffHz (metres));
        const auto input = tone (frequency, sampleRate, 48000);
        const auto out = run (s, input);
        return db (rms (out.left.data() + 4800, 43200) / rms (input.data() + 4800, 43200));
    };

    CHECK_THAT (lossDb (8000.0), WithinAbs (-geometry::airLossDbAt8k (metres), 0.5)); // ~ -3.8 dB
    CHECK (lossDb (500.0) > -0.1);
}

TEST_CASE ("moving away changes loudness smoothly (no clicks)")
{
    constexpr double sampleRate = 48000.0;
    const auto input = tone (440.0, sampleRate, 9600);

    Spatializer s;
    s.prepare (sampleRate);
    s.setTarget (-90.0f, 0.0f, 1.0f);

    Output out { std::vector<float> (input.size()), std::vector<float> (input.size()) };
    const float* first[] { input.data() };
    s.process (first, 1, out.left.data(), out.right.data(), 4800);
    s.setTarget (-90.0f, 0.0f, 0.1f); // -20 dB at once
    const float* second[] { input.data() + 4800 };
    s.process (second, 1, out.left.data() + 4800, out.right.data() + 4800, 4800);

    CHECK (maxStep (out.left, 4700, 9600) <= maxStep (out.left, 100, 4800));
    CHECK_THAT (out.left.back() / input.back(), WithinAbs (0.1, 1e-3)); // arrived after 50 ms
}
