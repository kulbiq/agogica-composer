#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/PluginProcessor.h"
#include "../Source/bridge/Stage.h"
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

TEST_CASE ("Pro panel at its defaults: the direct sound exactly as before", "[integration]")
{
    // The chain is the direct sound alone for now, so the processor must sound like the
    // per-sample direct-path stage it shipped with (tests/LegacyDirectPath.h).
    juce::ScopedJuceInitialiser_GUI juce;

    for (const auto mode : { 0, 1 })
    {
        ComposerProcessor p;
        set (p, "outputMode", (float) mode);
        set (p, "playerX", -4.0f);
        set (p, "playerY", 7.0f);
        set (p, "roomSize", 25.0f);
        const auto out = render (p, 25);

        History history;
        history.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * 48000.0));
        legacy::DirectPath path;
        path.prepare (48000.0);
        Acoustics a;
        a.roomSize = 25.0f;
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

TEST_CASE ("Pro: distance law and air in the derived values", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    set (p, "playerY", 2.0f);

    CHECK_THAT ((double) stage::snapshot (p)["players"][0]["derived"]["levelDb"], WithinAbs (-2.007, 0.01)); // default
    set (p, "proDistanceLaw", 20.0f * std::log10 (2.0f));
    CHECK_THAT ((double) stage::snapshot (p)["players"][0]["derived"]["levelDb"], WithinAbs (-6.02, 0.01));

    set (p, "proAir", 0.0f);
    CHECK_THAT ((double) stage::snapshot (p)["players"][0]["derived"]["airDb"], WithinAbs (0.0, 1e-6));
}
