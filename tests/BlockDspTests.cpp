#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/dsp/DirectPath.h"
#include "../Source/dsp/Fir.h"
#include "LegacyDirectPath.h"

using Catch::Matchers::WithinAbs;

// Block processing (History::maxBlock samples at a time) against the per-sample stage it replaced.

TEST_CASE ("convolution kernels: every one this CPU runs gives the true sums, however a run is split")
{
    dsp::NamedConvolveTwo kernels[4];
    const auto count = dsp::availableConvolveTwo (kernels, 4);
    REQUIRE (count >= 1);

    const auto taps = GENERATE (1, 96, 235, 256, 512, 941, 1024);
    juce::Random rng (taps);
    std::vector<float> irLeft ((size_t) taps), irRight ((size_t) taps), x ((size_t) taps + 128);
    for (auto* v : { &irLeft, &irRight, &x })
        for (auto& s : *v)
            s = rng.nextFloat() * 2.0f - 1.0f;

    constexpr int n = 70; // two groups of 32 and a short rest
    for (int k = 0; k < count; ++k)
    {
        INFO (kernels[k].name << ", " << taps << " taps");
        std::vector<float> left (n), right (n);
        kernels[k].convolve (irLeft.data(), irRight.data(), taps, x.data(), left.data(), right.data(), n);

        for (int m = 0; m < n; ++m)
        {
            double l = 0.0, r = 0.0, magnitude = 0.0;
            for (int j = 0; j < taps; ++j)
            {
                l += (double) irLeft[(size_t) j] * x[(size_t) (j + m)];
                r += (double) irRight[(size_t) j] * x[(size_t) (j + m)];
                magnitude += std::abs ((double) x[(size_t) (j + m)]);
            }
            REQUIRE_THAT (left[(size_t) m], WithinAbs (l, 1e-6 * magnitude));
            REQUIRE_THAT (right[(size_t) m], WithinAbs (r, 1e-6 * magnitude));
        }

        // Split into uneven pieces: bit for bit the same, so host block sizes never change the sound.
        std::vector<float> pieceL (n), pieceR (n);
        for (int start = 0, size = 1; start < n; start += size, size = size % 37 + 5)
        {
            const auto length = std::min (size, n - start);
            kernels[k].convolve (irLeft.data(), irRight.data(), taps, x.data() + start, pieceL.data() + start, pieceR.data() + start, length);
        }
        CHECK (pieceL == left);
        CHECK (pieceR == right);
    }
}

namespace
{
struct Output
{
    std::vector<float> left, right;
};

// The block stage, driven like the processor drives it, and the per-sample one it replaced
// (tests/LegacyDirectPath.h), fed the same input and the same targets.
struct BlockAndLegacy
{
    History history, legacyHistory;
    DirectPath path;
    legacy::DirectPath legacyPath;

    explicit BlockAndLegacy (double sampleRate)
    {
        history.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * sampleRate));
        legacyHistory.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * sampleRate));
        path.prepare (sampleRate);
        legacyPath.prepare (sampleRate);
    }

    void setTarget (float azimuth, float delaySeconds, float gain)
    {
        path.setTarget (azimuth, delaySeconds, gain);
        legacyPath.setTarget (azimuth, delaySeconds, gain);
    }

    void setMode (OutputMode mode)
    {
        path.setMode (mode);
        legacyPath.setMode (mode);
    }

    // Plays input[from, from + n) into both outputs at the same positions.
    void play (const std::vector<float>& input, Output& out, Output& legacyOut, size_t from, int n)
    {
        for (int start = 0; start < n; start += History::maxBlock)
        {
            const auto first = from + (size_t) start;
            const auto count = std::min (History::maxBlock, n - start);

            for (int i = 0; i < count; ++i)
                history.write (input[first + (size_t) i]);

            std::fill_n (out.left.data() + first, count, 0.0f);
            std::fill_n (out.right.data() + first, count, 0.0f);
            path.process (history, out.left.data() + first, out.right.data() + first, count);
        }

        for (auto i = from; i < from + (size_t) n; ++i)
        {
            legacyHistory.write (input[i]);
            float l = 0.0f, r = 0.0f;
            legacyPath.processSample (legacyHistory, l, r);
            legacyOut.left[i] = l;
            legacyOut.right[i] = r;
        }
    }
};

std::vector<float> whiteNoise (size_t length, int seed)
{
    juce::Random rng (seed);
    std::vector<float> v (length);
    for (auto& x : v)
        x = rng.nextFloat() * 2.0f - 1.0f;
    return v;
}

// Largest difference between the two renders outside [skipFrom, skipTo).
float largestDifference (const Output& a, const Output& b, size_t skipFrom = 0, size_t skipTo = 0)
{
    float difference = 0.0f;
    for (size_t i = 0; i < a.left.size(); ++i)
        if (i < skipFrom || i >= skipTo)
            difference = std::max ({ difference, std::abs (a.left[i] - b.left[i]), std::abs (a.right[i] - b.right[i]) });
    return difference;
}
} // namespace

TEST_CASE ("block processing sounds like the per-sample direct path it replaced")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    const auto hostBlock = GENERATE (512, 37);
    const auto noise = whiteNoise ((size_t) (sampleRate * 0.6), 31);
    const auto taps = (size_t) Hrtf::forSampleRate (sampleRate).taps();
    const auto fadeLength = (size_t) std::lround (Placer::fadeSeconds * sampleRate);
    INFO (sampleRate << " Hz, host blocks of " << hostBlock);

    Output out { std::vector<float> (noise.size()), std::vector<float> (noise.size()) };
    auto legacyOut = out;
    BlockAndLegacy s (sampleRate);

    // Calls `targets (start)` before each host block, then plays it.
    auto render = [&] (auto&& targets)
    {
        for (size_t start = 0; start < noise.size(); start += (size_t) hostBlock)
        {
            targets (start);
            s.play (noise, out, legacyOut, start, (int) std::min ((size_t) hostBlock, noise.size() - start));
        }
    };

    SECTION ("a still source, any direction, both modes")
    {
        const auto mode = GENERATE (OutputMode::stereo, OutputMode::binaural);
        const auto azimuth = GENERATE (-135.0f, -30.0f, 0.0f, 47.0f, 90.0f, 170.0f);
        s.setMode (mode);
        render ([&] (size_t) { s.setTarget (azimuth, 0.0123f, 0.7f); });

        CHECK (largestDifference (out, legacyOut) < 1e-5f);
    }

    SECTION ("a moving source: level, delay and pan glide the same way")
    {
        // Stereo: everything but the HRIR, so every sample must match.
        render ([&] (size_t start)
        {
            const auto t = (float) ((double) start / sampleRate);
            s.setTarget (-60.0f + 200.0f * t, t < 0.2f ? 0.01f : 0.03f, 1.0f - t);
        });

        CHECK (largestDifference (out, legacyOut) < 1e-5f);
    }

    SECTION ("binaural, a direction change: the same outside its 20 ms fade; inside, close for a step, the same for a jump")
    {
        const auto jump = GENERATE (-1.0f, (float) Placer::blendDegrees, 60.0f, 180.0f);
        s.setMode (OutputMode::binaural);
        const auto change = (size_t) (0.25 * sampleRate);
        size_t changedAt = 0;
        render ([&] (size_t start)
        {
            if (start >= change && changedAt == 0)
                changedAt = start;
            s.setTarget (start >= change ? 30.0f + jump : 30.0f, 0.005f, 1.0f);
        });

        CHECK (largestDifference (out, legacyOut, changedAt, changedAt + fadeLength + 1) < 1e-5f);

        if (std::abs (jump) > (float) Placer::blendDegrees)
        {
            CHECK (largestDifference (out, legacyOut) < 1e-4f); // the fade's own steps, to rounding
        }
        else
        {
            // One blend of the two HRIRs per block instead of a fade every sample: measured 0.8-1.6 %
            // RMS here, up to 2.1 % for other steps and directions (white noise).
            double difference = 0.0, signal = 0.0;
            for (auto i = changedAt; i < changedAt + fadeLength; ++i)
                for (const auto* ear : { &out.left, &out.right })
                {
                    const auto& reference = ear == &out.left ? legacyOut.left : legacyOut.right;
                    difference += std::pow ((*ear)[i] - reference[i], 2.0f);
                    signal += std::pow (reference[i], 2.0f);
                }
            CHECK (std::sqrt (difference / signal) < 0.03);
        }
    }

    SECTION ("mode switches: the same once the fade into binaural is over")
    {
        // Stereo no longer writes the HRIR window: going binaural, the fade waits one HRIR length
        // for the window to fill, then runs as before. Back to stereo it is the same throughout.
        const auto toBinaural = (size_t) (0.1 * sampleRate), toStereo = (size_t) (0.4 * sampleRate);
        size_t switchedAt = 0;
        render ([&] (size_t start)
        {
            const auto binaural = start >= toBinaural && start < toStereo;
            if (binaural && switchedAt == 0)
                switchedAt = start;
            s.setMode (binaural ? OutputMode::binaural : OutputMode::stereo);
            s.setTarget (-70.0f, 0.002f, 1.0f);
        });

        CHECK (largestDifference (out, legacyOut, switchedAt, switchedAt + taps + fadeLength + 1) < 1e-5f);
    }
}
