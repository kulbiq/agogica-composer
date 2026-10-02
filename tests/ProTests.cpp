#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/PluginProcessor.h"
#include "../Source/bridge/Stage.h"
#include "../Source/dsp/EarlyReflections.h"

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

TEST_CASE ("Pro panel at its defaults sounds exactly as before", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;

    for (const auto mode : { 0, 1 })
    {
        ComposerProcessor p;
        set (p, "outputMode", (float) mode);
        set (p, "hall", 3.0f);
        set (p, "playerX", -4.0f);
        set (p, "playerY", 7.0f);
        set (p, "roomSize", 25.0f);
        const auto out = render (p, 25);

        juce::MemoryBlock reference;
        REQUIRE (juce::File (juce::String (COMPOSER_TEST_DATA) + "/reference_" + (mode ? "binaural" : "stereo") + ".f32").loadFileAsData (reference));
        REQUIRE (reference.getSize() == out.size() * sizeof (float));

        juce::MemoryInputStream in (reference, false);
        for (size_t i = 0; i < out.size(); ++i)
            REQUIRE_THAT (out[i], WithinAbs (in.readFloat(), 1e-4)); // written by the pre-Pro build
    }
}

TEST_CASE ("Pro: reflections and reverb off leave only the direct sound", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;

    ComposerProcessor dry;
    set (dry, "hall", 0.0f);
    const auto reference = render (dry, 20);

    ComposerProcessor off;
    set (off, "hall", 3.0f);
    set (off, "proReflections", -24.0f);
    set (off, "proReverb", -24.0f);
    const auto out = render (off, 20);

    for (size_t i = 0; i < out.size(); ++i)
        REQUIRE_THAT (out[i], WithinAbs (reference[i], 1e-6));
}

namespace
{
// Early reflections alone for one impulse, Chamber, player (3, 5), listener at the centre.
std::vector<float> reflectionsImpulse (const Adjustments& adjust)
{
    History history;
    history.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * 48000.0));
    EarlyReflections reflections;
    reflections.prepare (48000.0);

    Acoustics a;
    a.hall = &halls[2];
    a.roomSize = 20.0f;
    a.player = { 3.0f, 5.0f };
    a.adjust = adjust;
    reflections.setAcoustics (a);

    std::vector<float> energy (4800);
    for (size_t i = 0; i < energy.size(); ++i)
    {
        float l = 0.0f, r = 0.0f;
        history.write (i == 0 ? 1.0f : 0.0f);
        reflections.processSample (history, l, r);
        energy[i] = l * l + r * r;
    }
    return energy;
}

double sum (const std::vector<float>& v, size_t from, size_t to)
{
    double s = 0.0;
    for (auto i = from; i < to; ++i)
        s += v[i];
    return s;
}
} // namespace

TEST_CASE ("Pro: floor and diffusion shape the early reflections")
{
    const auto plain = reflectionsImpulse ({});

    // Floor level 0 removes the floor reflection: the floor image is 6.31 m away
    // (883 samples); the next reflection (ceiling) is at 1340.
    Adjustments noFloor;
    noFloor.floor = 0.0f;
    CHECK (sum (reflectionsImpulse (noFloor), 870, 1300) < 0.01 * sum (plain, 870, 1300));

    // Diffusion 0 leaves each reflection a compact copy: share of the floor reflection's
    // energy within 1 ms of its (delayed) start.
    auto compactness = [] (const std::vector<float>& e)
    {
        size_t start = 870;
        while (e[start] < 1e-12f)
            ++start;
        return sum (e, start, start + 48) / sum (e, start, 1300);
    };

    Adjustments clean;
    clean.diffusion = 0.0f;
    CHECK (compactness (reflectionsImpulse (clean)) > 0.9);
    CHECK (compactness (plain) < 0.7);
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
