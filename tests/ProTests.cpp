#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/PluginProcessor.h"
#include "../Source/bridge/StageSnapshot.h"
#include "../Source/bridge/StageEdits.h"
#include "../Source/stage/Scenes.h"
#include "../Source/stage/Seating.h"
#include "LegacyDirectPath.h"

using Catch::Matchers::WithinAbs;

namespace
{
void set (ComposerProcessor& p, const char* id, float value)
{
    auto* param = p.state.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

// Renders `blocks` x 480 samples of seeded noise; returns interleaved L/R.
std::vector<float> render (ComposerProcessor& p, int blocks)
{
    p.setPlayConfigDetails (2, 2, 48000.0, 480);
    p.prepareToPlay (48000.0, 480);

    juce::Random rng (77);
    juce::MidiBuffer midi;
    std::vector<float> out;

    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, 480);
        for (int i = 0; i < 480; ++i)
        {
            buffer.setSample (0, i, rng.nextFloat() - 0.5f);
            buffer.setSample (1, i, rng.nextFloat() - 0.5f);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < 480; ++i)
            out.push_back (buffer.getSample (0, i)), out.push_back (buffer.getSample (1, i));
    }
    return out;
}
} // namespace

TEST_CASE ("Pro panel at the values it shipped with (no delay stretch): the direct sound exactly as before", "[integration]")
{
    // The chain is the direct sound alone for now, so the processor must sound like the
    // per-sample direct-path stage it shipped with (tests/LegacyDirectPath.h). The defaults have
    // changed since (-2.5 dB, stretched 1-3x): the shipped values are set here.
    juce::ScopedJuceInitialiser_GUI juce;

    for (const auto mode : { 0, 1 })
    {
        ComposerProcessor p;
        set (p, "proDistanceLaw", params::legacyDistanceLaw);
        set (p, "proDelayTo", 1.0f);
        set (p, "outputMode", (float) mode);
        set (p, "playerX", -4.0f);
        set (p, "playerY", 7.0f);
        set (p, "roomSize", 25.0f);
        const auto out = render (p, 25);

        History history;
        history.prepare ((int) std::ceil (History::maxDelaySeconds * 48000.0));
        legacy::DirectPath path;
        path.prepare (48000.0);
        Acoustics a;
        a.player = { -4.0f, 7.0f };
        a.mode = mode == 1 ? OutputMode::binaural : OutputMode::stereo;

        juce::Random rng (77); // render()'s input
        for (size_t i = 0; i < out.size() / 2; ++i)
        {
            if (i % 480 == 0)
                path.setAcoustics (a);

            const auto l = rng.nextFloat() - 0.5f, r = rng.nextFloat() - 0.5f;
            history.write (0.5f * (l + r));
            float left = 0.0f, right = 0.0f;
            path.processSample (history, left, right);
            REQUIRE_THAT (out[2 * i], WithinAbs (left, 1e-5));
            REQUIRE_THAT (out[2 * i + 1], WithinAbs (right, 1e-5));
        }
    }
}

TEST_CASE ("delay stretch: a smooth field, near places alike, far ones apart, always the same")
{
    float lowest = 1.0f, highest = 0.0f, steepest = 0.0f;
    juce::Random rng (5);

    for (int i = 0; i < 4000; ++i)
    {
        const geometry::Point p { rng.nextFloat() * 50.0f - 25.0f, rng.nextFloat() * 50.0f - 25.0f };
        const auto t = rng.nextDouble() * 600.0;
        const auto v = stretch::field (p, t);
        const auto angle = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        const geometry::Point near { p.x + std::cos (angle), p.y + std::sin (angle) }; // 1 m away

        lowest = std::min (lowest, v);
        highest = std::max (highest, v);
        steepest = std::max (steepest, std::abs (stretch::field (near, t) - v));
        REQUIRE (juce::exactlyEqual (v, stretch::field (p, t))); // the same every time
    }

    CHECK (lowest >= 0.0f);
    CHECK (highest <= 1.0f);
    CHECK (highest - lowest > 0.6f);   // far apart, the players differ
    CHECK (steepest < 1.5f / stretch::spanMetres + 0.01f); // 1 m apart: never more than ~13 % of the range
}

TEST_CASE ("delay stretch: a track keeps its multiplier while it plays, takes a new one after a pause")
{
    constexpr double rate = 48000.0;
    DelayStretch s;
    s.prepare (rate);
    const geometry::Point player { 8.0f, 14.0f };
    const auto physical = 0.3f; // s: stretched, 0.3 to 1.2 s
    double t = 0.0;
    auto block = [&] (float peak) { const auto took = s.update (peak, 480, player, t, physical, 1.0f, 4.0f); t += 0.01; return took; };

    CHECK (block (0.5f)); // the first sound takes one
    const auto first = s.multiplier (1.0f, 4.0f);
    CHECK (juce::exactlyEqual (first, 1.0f + 3.0f * stretch::field (player, 0.0)));

    // Playing on for two minutes (the field moves meanwhile) and through short gaps: held.
    for (int b = 0; b < 12000; ++b)
        CHECK_FALSE (block (b % 50 < 40 ? 0.5f : 0.0f)); // 0.1 s gaps between notes
    CHECK (juce::exactlyEqual (s.multiplier (1.0f, 4.0f), first));

    // A pause shorter than the stretched delays: held (the last phrase is still on its way).
    for (int b = 0; b < 30; ++b)
        block (0.0f);
    CHECK_FALSE (block (0.5f));

    // Quiet relative to the phrase (a reverb tail) counts as a pause; after it, a new one.
    for (int b = 0; b < 130; ++b) // 1.3 s: longer than any of the delays
        block (0.001f);
    const auto at = t;
    CHECK (block (0.5f));
    CHECK (juce::exactlyEqual (s.multiplier (1.0f, 4.0f), 1.0f + 3.0f * stretch::field (player, at)));
    CHECK (juce::exactlyEqual (s.multiplier (2.0f, 2.0f), 2.0f)); // a range of one value: that value
}

TEST_CASE ("delay stretch in the processor: later by the multiplier, repeatable, never jumping under a note", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    constexpr double rate = 48000.0;
    const auto physical = 10.0 / 343.0 * rate; // samples: the player 10 m ahead

    // An impulse after silence, then noise phrases with a pause between them.
    const auto play = [&] (float from, float to)
    {
        ComposerProcessor p;
        set (p, "playerY", 10.0f);
        set (p, "proDelayFrom", from);
        set (p, "proDelayTo", to);
        p.setPlayConfigDetails (2, 2, rate, 480);
        p.prepareToPlay (rate, 480);
        juce::Random rng (2);
        juce::MidiBuffer midi;
        std::vector<float> out;
        for (int b = 0; b < 600; ++b)
        {
            juce::AudioBuffer<float> buffer (2, 480);
            buffer.clear();
            if (b == 0)
                buffer.setSample (0, 0, 1.0f), buffer.setSample (1, 0, 1.0f);
            else if ((b >= 100 && b < 300) || b >= 400) // 2 s, then 1 s of pause, then 2 s
                for (int i = 0; i < 480; ++i)
                    buffer.setSample (0, i, rng.nextFloat() - 0.5f), buffer.setSample (1, i, buffer.getSample (0, i));
            p.processBlock (buffer, midi);
            for (int i = 0; i < 480; ++i)
                out.push_back (buffer.getSample (0, i) + buffer.getSample (1, i));
        }
        return out;
    };
    const auto firstSound = [] (const std::vector<float>& out, size_t from) // index of the first audible sample
    {
        for (auto i = from; i < out.size(); ++i)
            if (std::abs (out[i]) > 1.0e-3f)
                return (double) i;
        return -1.0;
    };

    SECTION ("a range of one value: every delay that many times the physical one")
    {
        const auto out = play (3.0f, 3.0f);
        CHECK (std::abs (firstSound (out, 0) - 3.0 * physical) <= 2.0);
        CHECK (std::abs (firstSound (out, 100 * 480) - (100 * 480 + 3.0 * physical)) <= 2.0);
    }

    SECTION ("a range: each phrase after a pause takes the field's value, the same on every playback")
    {
        const auto out = play (1.0f, 4.0f);
        CHECK (out == play (1.0f, 4.0f));

        for (const auto block : { 100, 400 }) // the field at the time each phrase begins
        {
            const auto scale = 1.0 + 3.0 * stretch::field ({ 0.0f, 10.0f }, block * 480 / rate);
            CHECK (std::abs (firstSound (out, (size_t) block * 480) - (block * 480 + scale * physical)) <= 2.0);
        }
    }
}
