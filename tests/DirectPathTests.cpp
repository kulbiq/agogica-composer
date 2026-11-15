#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/dsp/DirectPath.h"
#include "../Source/dsp/Geometry.h"
#include "../Source/dsp/Hrtf.h"

using Catch::Matchers::WithinAbs;

namespace
{
// The direct-path stage, driven like the processor drives it.
struct Rig
{
    DirectPath path;

    void prepare (double sampleRate) { path.prepare (sampleRate); }

    void setTarget (float azimuth, float gain = 1.0f, float airCutoffHz = 0.0f)
    {
        path.setTarget (azimuth, gain, airCutoffHz);
    }

    void setMode (OutputMode mode) { path.setMode (mode); }

    void process (const float* input, float* left, float* right, int numSamples)
    {
        for (int start = 0; start < numSamples; start += dsp::maxBlock)
        {
            const auto n = std::min (dsp::maxBlock, numSamples - start);
            std::fill (left + start, left + start + n, 0.0f);
            std::fill (right + start, right + start + n, 0.0f);
            path.process (input + start, left + start, right + start, n);
        }
    }
};

struct Output
{
    std::vector<float> left, right;
};

// Runs `input` (mono) through the rig in blocks of `blockSize`.
Output run (Rig& s, const std::vector<float>& input, int blockSize = 512)
{
    Output out { std::vector<float> (input.size()), std::vector<float> (input.size()) };

    for (size_t start = 0; start < input.size(); start += (size_t) blockSize)
    {
        const auto n = (int) std::min ((size_t) blockSize, input.size() - start);
        s.process (input.data() + start, out.left.data() + start, out.right.data() + start, n);
    }

    return out;
}

// Plays input[from, from + n) into `out` at the same positions.
void play (Rig& s, const std::vector<float>& input, Output& out, size_t from, size_t n)
{
    s.process (input.data() + from, out.left.data() + from, out.right.data() + from, (int) n);
}

std::vector<float> impulse (size_t length)
{
    std::vector<float> v (length, 0.0f);
    v[0] = 1.0f;
    return v;
}

std::vector<float> whiteNoise (size_t length, int seed)
{
    juce::Random rng (seed);
    std::vector<float> v (length);
    for (auto& x : v)
        x = rng.nextFloat() * 2.0f - 1.0f;
    return v;
}

std::vector<float> tone (double frequency, double sampleRate, size_t length)
{
    std::vector<float> v (length);
    for (size_t i = 0; i < length; ++i)
        v[i] = (float) std::sin (juce::MathConstants<double>::twoPi * frequency * (double) i / sampleRate);
    return v;
}

size_t peakIndex (const std::vector<float>& v)
{
    return (size_t) std::distance (v.begin(), std::max_element (v.begin(), v.end(), [] (float a, float b)
    {
        return std::abs (a) < std::abs (b);
    }));
}

double rms (const float* v, size_t n)
{
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i)
        sum += (double) v[i] * v[i];
    return std::sqrt (sum / (double) n);
}

double db (double ratio) { return 20.0 * std::log10 (ratio); }

Output render (OutputMode mode, float azimuth, const std::vector<float>& input, double sampleRate = 48000.0)
{
    Rig s;
    s.prepare (sampleRate);
    s.setMode (mode);
    s.setTarget (azimuth);
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

// ---- Stereo: delay, pan, level ---------------------------------------------------------

TEST_CASE ("no delay: an impulse comes out at once, -3 dB each side at the centre")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0);

    Rig s;
    s.prepare (sampleRate);
    s.setTarget (0.0f);

    const auto out = run (s, impulse (2048));
    CHECK (peakIndex (out.left) == 0);
    CHECK_THAT (out.left[0], WithinAbs (std::sqrt (0.5), 1e-4));
    CHECK_THAT (out.right[0], WithinAbs (std::sqrt (0.5), 1e-4));
}

TEST_CASE ("level: constant-power pan times distance gain")
{
    Rig s;
    s.prepare (48000.0);

    const auto [pan, gain, expectedLeft, expectedRight] = GENERATE (table<float, float, double, double> ({
        { -1.0f, 1.0f, 1.0, 0.0 },
        { 0.0f, 1.0f, std::sqrt (0.5), std::sqrt (0.5) },
        { 1.0f, 1.0f, 0.0, 1.0 },
        { -0.5f, 1.0f, std::cos (juce::MathConstants<double>::pi / 8), std::sin (juce::MathConstants<double>::pi / 8) },
        { -1.0f, 0.25f, 0.25, 0.0 },
    }));

    s.setTarget (pan * 90.0f, gain); // pan -1..+1 = azimuth -90..+90
    const auto out = run (s, std::vector<float> (256, 1.0f));
    CHECK_THAT (out.left.back(), WithinAbs (expectedLeft, 1e-4));
    CHECK_THAT (out.right.back(), WithinAbs (expectedRight, 1e-4));
}

TEST_CASE ("air absorption: ISO loss at 8 kHz, low frequencies untouched")
{
    constexpr double sampleRate = 48000.0;
    constexpr float metres = 50.0f;

    auto lossDb = [&] (double frequency)
    {
        Rig s;
        s.prepare (sampleRate);
        s.setTarget (-90.0f, 1.0f, geometry::airCutoffHz (metres));
        const auto input = tone (frequency, sampleRate, 48000);
        const auto out = run (s, input);
        return db (rms (out.left.data() + 4800, 43200) / rms (input.data() + 4800, 43200));
    };

    CHECK_THAT (lossDb (8000.0), WithinAbs (-geometry::airLossDbAt8k (metres), 0.5)); // ~ -3.8 dB
    CHECK (lossDb (500.0) > -0.1);
}

// ---- Binaural ----------------------------------------------------------------------------

TEST_CASE ("HRTF tables: every rate, 1-degree grid, right ear louder at +90")
{
    auto louderEar = [] (const Hrtf& h, float azimuth, int ear)
    {
        const auto index = Hrtf::nearestIndex (azimuth);
        return db (rms (h.reversedIr (index, ear), (size_t) h.taps()) / rms (h.reversedIr (index, 1 - ear), (size_t) h.taps()));
    };

    SECTION ("a table for every common rate")
    {
        const auto [rate, taps] = GENERATE (table<double, int> ({
            { 44100.0, 235 }, { 48000.0, 256 }, { 88200.0, 470 }, { 96000.0, 512 }, { 176400.0, 941 }, { 192000.0, 1024 },
        }));

        const auto& h = Hrtf::forSampleRate (rate);
        CHECK_THAT (h.sampleRate(), WithinAbs (rate, 0.5));
        CHECK (h.taps() == taps);
        CHECK (louderEar (h, 90.0f, 1) > 10.0);
        CHECK (louderEar (h, -90.0f, 0) > 10.0);
    }

    SECTION ("the index wraps around the circle")
    {
        CHECK (Hrtf::nearestIndex (0.0f) == 0);
        CHECK (Hrtf::nearestIndex (90.0f) == 90);
        CHECK (Hrtf::nearestIndex (-90.0f) == 270);
        CHECK (Hrtf::nearestIndex (180.0f) == 180);
        CHECK (Hrtf::nearestIndex (-180.0f) == 180);
        CHECK (Hrtf::nearestIndex (359.6f) == 0);
    }

    SECTION ("a rate without a table falls back to the nearest one and still works")
    {
        const auto rate = GENERATE (22050.0, 32000.0, 384000.0);
        const auto noise = whiteNoise (9600, 26);
        const auto out = render (OutputMode::binaural, 90.0f, noise, rate);

        for (size_t i = 0; i < noise.size(); ++i)
            REQUIRE ((std::isfinite (out.left[i]) && std::isfinite (out.right[i])));
        CHECK (db (rms (out.right.data(), noise.size()) / rms (out.left.data(), noise.size())) > 6.0);
    }
}

TEST_CASE ("binaural cues: level match, ILD/ITD, front/back")
{
    SECTION ("binaural at the front is as loud as stereo at centre")
    {
        const auto noise = whiteNoise (96000, 21);
        const auto stereo = render (OutputMode::stereo, 0.0f, noise);
        const auto binaural = render (OutputMode::binaural, 0.0f, noise);

        auto power = [] (const Output& o)
        {
            return rms (o.left.data() + 1024, o.left.size() - 1024) + rms (o.right.data() + 1024, o.right.size() - 1024);
        };

        CHECK (std::abs (db (power (binaural) / power (stereo))) < 1.0);
    }

    SECTION ("level and arrival-time differences between the ears")
    {
        const auto noise = whiteNoise (48000, 22);
        const auto right = render (OutputMode::binaural, 90.0f, noise);
        const auto left = render (OutputMode::binaural, -90.0f, noise);

        CHECK (db (rms (right.right.data(), noise.size()) / rms (right.left.data(), noise.size())) > 6.0);
        CHECK (db (rms (left.left.data(), noise.size()) / rms (left.right.data(), noise.size())) > 6.0);

        // Interaural time difference: at +90 the right ear hears it > 0.3 ms earlier.
        const auto click = render (OutputMode::binaural, 90.0f, impulse (1024));
        CHECK ((double) onset (click.left) - (double) onset (click.right) > 0.0003 * 48000.0);
    }

    SECTION ("front and back sound different")
    {
        const auto noise = whiteNoise (48000, 23);
        const auto front = render (OutputMode::binaural, 0.0f, noise);
        const auto back = render (OutputMode::binaural, 180.0f, noise);

        std::vector<float> difference (noise.size());
        for (size_t i = 0; i < noise.size(); ++i)
            difference[i] = front.left[i] - back.left[i];

        CHECK (rms (difference.data(), difference.size()) > 0.2 * rms (front.left.data(), noise.size()));
    }
}

// ---- Clicks ------------------------------------------------------------------------------

TEST_CASE ("no clicks: direction jump, mode switch, gain jump")
{
    constexpr double sampleRate = 48000.0;
    Rig s;
    s.prepare (sampleRate);

    SECTION ("direction jumps and mode switches")
    {
        const auto sine = tone (440.0, sampleRate, 19200);
        s.setMode (OutputMode::binaural);
        s.setTarget (0.0f);

        Output out { std::vector<float> (sine.size()), std::vector<float> (sine.size()) };
        play (s, sine, out, 0, 4800);
        s.setTarget (120.0f); // big direction jump: one 20 ms fade
        play (s, sine, out, 4800, 4800);
        s.setMode (OutputMode::stereo);
        play (s, sine, out, 9600, 4800);
        s.setMode (OutputMode::binaural);
        play (s, sine, out, 14400, 4800);

        // Steps during the transitions stay within what the steady signals do on either side.
        for (auto* ear : { &out.left, &out.right })
        {
            const auto steady = std::max ({ maxStep (*ear, 1000, 4800), maxStep (*ear, 7000, 9600), maxStep (*ear, 12000, 14400) });
            CHECK (maxStep (*ear, 4800, 7000) <= 1.2f * steady);
            CHECK (maxStep (*ear, 9600, 12000) <= 1.2f * steady);
            CHECK (maxStep (*ear, 14400, 16800) <= 1.2f * steady);
        }
    }

    SECTION ("moving away changes loudness smoothly")
    {
        const auto input = tone (440.0, sampleRate, 9600);
        s.setTarget (-90.0f, 1.0f);

        Output out { std::vector<float> (input.size()), std::vector<float> (input.size()) };
        play (s, input, out, 0, 4800);
        s.setTarget (-90.0f, 0.1f); // -20 dB at once
        play (s, input, out, 4800, 4800);

        CHECK (maxStep (out.left, 4700, 9600) <= maxStep (out.left, 100, 4800));
        CHECK_THAT (out.left.back() / input.back(), WithinAbs (0.1, 1e-3)); // arrived after 50 ms
    }
}
