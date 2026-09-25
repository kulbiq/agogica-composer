#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/PluginProcessor.h"
#include "../Source/dsp/EarlyReflections.h"

using Catch::Matchers::WithinAbs;
using namespace geometry;

TEST_CASE ("image sources of a square room")
{
    const Point player { 3.0f, 5.0f }, listener { 0.0f, 0.0f };
    const auto images = imageSources (player, listener, 20.0f, 5.0f);

    auto has = [&] (Point p, int bounces)
    {
        for (const auto& i : images)
            if (std::abs (i.position.x - p.x) < 1e-4f && std::abs (i.position.y - p.y) < 1e-4f && i.bounces == bounces)
                return true;
        return false;
    };

    CHECK (has ({ 17.0f, 5.0f }, 1));   // right wall (x = 10)
    CHECK (has ({ -23.0f, 5.0f }, 1));  // left wall (x = -10)
    CHECK (has ({ 3.0f, 15.0f }, 1));   // front wall
    CHECK (has ({ 3.0f, -25.0f }, 1));  // back wall
    CHECK (has ({ 17.0f, 15.0f }, 2));  // corner
    CHECK (has ({ -23.0f, -25.0f }, 2));

    const auto direct = distance (player, listener);
    CHECK_THAT (images[4].distance, WithinAbs (std::hypot (direct, 2.4f), 1e-4));        // floor
    CHECK_THAT (images[5].distance, WithinAbs (std::hypot (direct, 2.0f * 3.8f), 1e-4)); // ceiling
}

TEST_CASE ("the longest reflection path fits the delay buffer")
{
    const auto half = maxRoomSize / 2.0f;
    float longest = 0.0f;

    for (const auto px : { -half, half })
        for (const auto py : { -half, half })
            for (const auto lx : { -half, half })
                for (const auto ly : { -half, half })
                    for (const auto& hall : halls)
                        for (const auto& image : imageSources ({ px, py }, { lx, ly }, maxRoomSize, hall.ceiling))
                            longest = std::max (longest, image.distance);

    CHECK (longest <= maxImageDistance);
    const auto maxPreDelay = std::max_element (halls.begin(), halls.end(), [] (auto& a, auto& b) { return a.preDelayMs < b.preDelayMs; })->preDelayMs;
    CHECK (maxImageDistance / speedOfSound + maxPreDelay / 1000.0f < (float) Acoustics::maxDelaySeconds);
}

namespace
{
struct Rig
{
    History history;
    EarlyReflections reflections;
    Acoustics acoustics;

    explicit Rig (const Hall& hall, double sampleRate = 48000.0)
    {
        history.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * sampleRate));
        reflections.prepare (sampleRate);
        acoustics.hall = &hall;
        acoustics.roomSize = 20.0f;
        acoustics.player = { 3.0f, 5.0f };
        acoustics.listener = { 0.0f, 0.0f };
        reflections.setAcoustics (acoustics);
    }

    // Impulse response, left and right.
    std::pair<std::vector<float>, std::vector<float>> impulse (size_t length)
    {
        std::vector<float> l (length), r (length);
        for (size_t i = 0; i < length; ++i)
        {
            history.write (i == 0 ? 1.0f : 0.0f);
            reflections.processSample (history, l[i], r[i]);
        }
        return { l, r };
    }
};
} // namespace

TEST_CASE ("a wall reflection arrives on time with its physical level")
{
    const auto& chamber = halls[2];
    Rig rig (chamber);
    const auto [l, r] = rig.impulse (4800);

    // Right wall: image at (17, 5), 17.72 m; nothing else arrives within ±2 m of it.
    const auto path = std::hypot (17.0f, 5.0f);
    const auto direct = std::hypot (3.0f, 5.0f);
    const auto expected = distanceGain (direct) * direct / path * std::sqrt (1.0f - chamber.absorption);
    const auto at = (size_t) std::lround (path / speedOfSound * 48000.0f);

    float peak = 0.0f;
    for (auto i = at - 3; i <= at + 3; ++i)
        peak = std::max (peak, std::hypot (l[i], r[i])); // constant-power pan: L² + R² = level²

    CHECK (peak > 0.55f * expected); // fractional delay spreads an impulse over 4 samples
    CHECK (peak < 1.05f * expected);
    CHECK (r[at] > std::abs (l[at])); // it comes from the right
}

TEST_CASE ("the Dry hall has no reflections at all")
{
    Rig rig (halls[0]);
    const auto [l, r] = rig.impulse (4800);

    for (size_t i = 0; i < l.size(); ++i)
    {
        REQUIRE_THAT (l[i], WithinAbs (0.0, 0.0));
        REQUIRE_THAT (r[i], WithinAbs (0.0, 0.0));
    }
}

TEST_CASE ("the whole chain gives the same result for any block size")
{
    juce::ScopedJuceInitialiser_GUI juce;

    auto render = [] (int blockSize)
    {
        ComposerProcessor p;
        auto set = [&] (const char* id, float v)
        {
            auto* param = p.state.getParameter (id);
            param->setValueNotifyingHost (param->convertTo0to1 (v));
        };
        set ("outputMode", 1.0f); // binaural
        set ("hall", 3.0f);       // concert hall
        set ("playerX", -4.0f);

        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        juce::Random rng (5);
        std::vector<float> left;
        juce::MidiBuffer midi;

        for (int done = 0; done < 9600; done += blockSize)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (0, i, rng.nextFloat() * 2.0f - 1.0f), buffer.setSample (1, i, 0.0f);

            p.processBlock (buffer, midi);
            left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
        }

        return left;
    };

    const auto big = render (480);
    const auto odd = render (120);

    for (size_t i = 0; i < 9600; ++i)
        REQUIRE_THAT (big[i], WithinAbs (odd[i], 1e-6));
}

TEST_CASE ("CPU: 32 players, whole chain", "[.perf]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto sampleRate = GENERATE (48000.0, 96000.0);
    const auto mode = GENERATE (0.0f, 1.0f);
    constexpr int players = 32, block = 512;
    constexpr double seconds = 10.0;

    std::vector<std::unique_ptr<ComposerProcessor>> instances;
    for (int i = 0; i < players; ++i)
    {
        auto& p = *instances.emplace_back (std::make_unique<ComposerProcessor>());
        auto set = [&] (const char* id, float v)
        {
            auto* param = p.state.getParameter (id);
            param->setValueNotifyingHost (param->convertTo0to1 (v));
        };
        set ("outputMode", mode);
        set ("hall", 3.0f);
        set ("playerX", (float) (i % 8) * 2.0f - 7.0f);
        set ("playerY", (float) (i / 8) * 2.0f + 3.0f);
        p.setPlayConfigDetails (2, 2, sampleRate, block);
        p.prepareToPlay (sampleRate, block);
    }

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    juce::Random rng (9);
    const auto blocks = (int) (seconds * sampleRate / block);

    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < blocks; ++b)
        for (auto& p : instances)
        {
            for (int i = 0; i < block; ++i)
                buffer.setSample (0, i, rng.nextFloat() - 0.5f), buffer.setSample (1, i, 0.0f);
            p->processBlock (buffer, midi);
        }
    const auto load = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0 / seconds;

    UNSCOPED_INFO (players << " players, " << (mode > 0.5f ? "binaural" : "stereo") << ", " << sampleRate << " Hz: "
                           << juce::String (load * 100.0, 1) << " % of one core");
    CHECK (load < (sampleRate < 50000.0 ? 0.30 : 0.90)); // regression guard; multi-core hosts spread tracks
}

TEST_CASE ("output stays finite for any parameter values (fuzz)")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);

    juce::Random rng (1234);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    const juce::StringArray ids { "roomSize", "listenerX", "listenerY", "playerX", "playerY", "outputMode", "hall" };

    for (int block = 0; block < 2000; ++block)
    {
        // Mostly random values, sometimes the extremes (players and listener meeting on a wall).
        for (const auto& id : ids)
            p.state.getParameter (id)->setValueNotifyingHost (rng.nextInt (4) == 0 ? (float) rng.nextInt (2) : rng.nextFloat());

        for (int i = 0; i < 256; ++i)
            buffer.setSample (0, i, rng.nextFloat() * 2.0f - 1.0f), buffer.setSample (1, i, rng.nextFloat() * 2.0f - 1.0f);

        p.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                REQUIRE (std::isfinite (buffer.getSample (ch, i)));
    }
}
