#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/PluginProcessor.h"
#include "../Source/bridge/Stage.h"
#include "../Source/dsp/EarlyReflections.h"
#include "../Source/dsp/ReverbTail.h"

using Catch::Matchers::WithinAbs;

namespace
{
void set (ComposerProcessor& p, const char* id, float value)
{
    auto* param = p.state.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

// Renders `blocks` x 480 samples of seeded noise; returns interleaved L/R.
std::vector<float> render (ComposerProcessor& p, int blocks, bool silenceAfterFirst = false)
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
            const auto noise = silenceAfterFirst && b > 0;
            buffer.setSample (0, i, noise ? 0.0f : rng.nextFloat() - 0.5f);
            buffer.setSample (1, i, noise ? 0.0f : rng.nextFloat() - 0.5f);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < 480; ++i)
            out.push_back (buffer.getSample (0, i)), out.push_back (buffer.getSample (1, i));
    }
    return out;
}
} // namespace

TEST_CASE ("Pro panel at its defaults sounds exactly as before")
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

TEST_CASE ("Pro: reflections and reverb off leave only the direct sound")
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

TEST_CASE ("Pro: floor reflection level")
{
    // The floor image is 6.31 m away (883 samples); the next reflection (ceiling) is at 1340.
    Adjustments noFloor;
    noFloor.floor = 0.0f;

    const auto withFloor = sum (reflectionsImpulse ({}), 870, 1300);
    const auto without = sum (reflectionsImpulse (noFloor), 870, 1300);
    CHECK (without < 0.01 * withFloor);
}

TEST_CASE ("Pro: diffusion 0 leaves each reflection a compact copy")
{
    Adjustments clean;
    clean.diffusion = 0.0f;

    // Share of the floor reflection's energy within 1 ms of its (delayed) start.
    auto compactness = [] (const std::vector<float>& e)
    {
        size_t start = 870;
        while (e[start] < 1e-12f)
            ++start;
        return sum (e, start, start + 48) / sum (e, start, 1300);
    };

    CHECK (compactness (reflectionsImpulse (clean)) > 0.9);
    CHECK (compactness (reflectionsImpulse ({})) < 0.7);
}

TEST_CASE ("Pro: decay scales the reverberation time")
{
    const Hall flat { "test", 10.0f, 0.2f, 1.5f, 1.5f, 0.0f, 0.2f };

    auto measure = [&] (float decay)
    {
        History history;
        history.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * 48000.0));
        ReverbTail tail;
        tail.prepare (48000.0);

        Acoustics a;
        a.hall = &flat;
        a.adjust.decay = decay;
        tail.setAcoustics (a);

        std::vector<double> energy ((size_t) (48000.0 * 1.5 * decay * 1.5));
        for (size_t i = 0; i < energy.size(); ++i)
        {
            float l = 0.0f, r = 0.0f;
            history.write (i == 0 ? 1.0f : 0.0f);
            tail.processSample (history, l, r);
            energy[i] = (double) l * l + (double) r * r;
        }

        for (size_t i = energy.size() - 1; i-- > 0;) // Schroeder backward integration
            energy[i] += energy[i + 1];

        auto timeAt = [&] (double db)
        {
            for (size_t i = 0; i < energy.size(); ++i)
                if (10.0 * std::log10 (energy[i] / energy[0]) <= db)
                    return (double) i / 48000.0;
            return (double) energy.size() / 48000.0;
        };
        return 2.0 * (timeAt (-35.0) - timeAt (-5.0));
    };

    CHECK_THAT (measure (2.0f), WithinAbs (3.0, 0.45));
    CHECK_THAT (measure (0.5f), WithinAbs (0.75, 0.12));
}

TEST_CASE ("Pro: distance law and air in the derived values")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    set (p, "playerY", 2.0f);

    CHECK_THAT ((double) stage::snapshot (p)["players"][0]["derived"]["levelDb"], WithinAbs (-2.007, 0.01)); // default
    set (p, "proDistanceLaw", 20.0f * std::log10 (2.0f));
    CHECK_THAT ((double) stage::snapshot (p)["players"][0]["derived"]["levelDb"], WithinAbs (-6.02, 0.01));

    set (p, "proAir", 0.0f);
    CHECK_THAT ((double) stage::snapshot (p)["players"][0]["derived"]["airDb"], WithinAbs (0.0, 1e-6));
    CHECK_THAT (geometry::airCutoffHz (50.0f, 0.0f), WithinAbs (0.0, 0.0)); // no filter at all
}

TEST_CASE ("Pro: shared, saved, adopted by new inserts, not part of scenes")
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::MemoryBlock saved;

    {
        ComposerProcessor a, b;
        a.markPlaced();
        auto drag = [&] (const char* id, float v)
        {
            auto edit = [&] (const char* phase)
            {
                auto o = std::make_unique<juce::DynamicObject>();
                o->setProperty ("phase", phase);
                o->setProperty ("target", a.id);
                o->setProperty ("param", id);
                o->setProperty ("value", v);
                stage::applyEdit (a, o.release());
            };
            edit ("begin"), edit ("change"), edit ("end");
        };

        drag ("proReverb", -6.0f);
        CHECK_THAT (b.getParam ("proReverb"), WithinAbs (-6.0, 1e-4));

        stage::setScene (a, "B");
        CHECK_THAT (b.getParam ("proReverb"), WithinAbs (-6.0, 1e-4)); // scenes don't touch it

        ComposerProcessor c;
        stage::join (c, true);
        CHECK_THAT (c.getParam ("proReverb"), WithinAbs (-6.0, 1e-4));

        b.getStateInformation (saved);
    }

    ComposerProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK_THAT (restored.getParam ("proReverb"), WithinAbs (-6.0, 1e-4));
}

TEST_CASE ("the longest tail (Church, decay x2) is never cut by the silence skip")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    set (p, "hall", 4.0f);
    set (p, "proDecay", 2.0f);

    const auto out = render (p, 1000, true); // 10 ms of noise, then silence until 10 s

    auto blockEnergy = [&] (int block)
    {
        double e = 0.0;
        for (size_t i = (size_t) block * 960; i < (size_t) (block + 1) * 960; ++i)
            e += (double) out[i] * out[i];
        return e;
    };

    // Still processed (not skipped) near the end, and by the time the skip may engage
    // (10 s) the tail is below -60 dB of its level at 0.2 s: cutting it then is inaudible.
    const auto early = blockEnergy (20);
    CHECK (blockEnergy (950) > 0.0);
    CHECK (10.0 * std::log10 (blockEnergy (990) / early) < -60.0);
}
