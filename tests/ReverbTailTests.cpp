#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/PluginProcessor.h"
#include "../Source/dsp/ReverbTail.h"

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double sampleRate = 48000.0;

// Impulse response of the tail stage alone for a given hall, left and right.
std::pair<std::vector<float>, std::vector<float>> tailResponse (const Hall& hall, double seconds)
{
    History history;
    history.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * sampleRate));
    ReverbTail tail;
    tail.prepare (sampleRate);

    Acoustics a;
    a.hall = &hall;
    a.player = { 0.0f, 0.0f };
    tail.setAcoustics (a);

    const auto n = (size_t) (seconds * sampleRate);
    std::vector<float> l (n), r (n);
    for (size_t i = 0; i < n; ++i)
    {
        history.write (i == 0 ? 1.0f : 0.0f);
        tail.processSample (history, l[i], r[i]);
    }
    return { l, r };
}

// Reverberation time from the Schroeder backward-integrated decay: T30 (-5 to -35 dB) x 2.
double rt60 (const std::vector<float>& l, const std::vector<float>& r)
{
    std::vector<double> energy (l.size());
    double sum = 0.0;
    for (size_t i = l.size(); i-- > 0;)
        energy[i] = (sum += (double) l[i] * l[i] + (double) r[i] * r[i]);

    auto timeAt = [&] (double db)
    {
        for (size_t i = 0; i < energy.size(); ++i)
            if (10.0 * std::log10 (energy[i] / energy[0]) <= db)
                return (double) i / sampleRate;
        return (double) energy.size() / sampleRate;
    };

    return 2.0 * (timeAt (-35.0) - timeAt (-5.0));
}
} // namespace

TEST_CASE ("the tail decays in the hall's reverberation time")
{
    for (const auto seconds : { 1.0f, 2.0f, 4.0f })
    {
        const Hall flat { "test", 10.0f, 0.2f, seconds, seconds, 0.0f, 0.2f }; // same decay at all frequencies
        const auto [l, r] = tailResponse (flat, seconds * 1.5);
        CHECK_THAT (rt60 (l, r), WithinAbs (seconds, 0.15 * seconds));
    }
}

TEST_CASE ("high frequencies die away faster (hall presets)")
{
    const auto& concert = halls[3]; // 2.0 s, highs 1.2 s
    const auto [l, r] = tailResponse (concert, 2.0);

    // Share of high-frequency energy (first difference ~ a high-pass) early vs late.
    auto highShare = [&] (size_t from, size_t to)
    {
        double all = 0.0, high = 0.0;
        for (size_t i = from; i < to; ++i)
        {
            all += (double) l[i] * l[i];
            high += std::pow ((double) l[i] - l[i - 1], 2.0);
        }
        return high / all;
    };

    CHECK (highShare (48000, 72000) < 0.7 * highShare (2400, 12000));
}

TEST_CASE ("tail left and right are decorrelated")
{
    const auto [l, r] = tailResponse (halls[3], 1.0);

    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (size_t i = 4800; i < l.size(); ++i)
        lr += (double) l[i] * r[i], ll += (double) l[i] * l[i], rr += (double) r[i] * r[i];

    CHECK (std::abs (lr / std::sqrt (ll * rr)) < 0.3);
}

TEST_CASE ("switching to Dry lets the tail die away instead of cutting it")
{
    History history;
    history.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * sampleRate));
    ReverbTail tail;
    tail.prepare (sampleRate);

    Acoustics a;
    a.hall = &halls[4]; // church
    tail.setAcoustics (a);

    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < 24000; ++i) // half a second of noise
    {
        history.write (juce::Random::getSystemRandom().nextFloat() - 0.5f);
        tail.processSample (history, l, r);
    }

    a.hall = &halls[0]; // Dry
    tail.setAcoustics (a);

    double late = 0.0; // 0.5 .. 1.0 s after the switch
    for (int i = 0; i < 48000; ++i)
    {
        l = r = 0.0f;
        history.write (0.0f);
        tail.processSample (history, l, r);
        if (i >= 24000)
            late += (double) l * l;
    }

    CHECK (late > 1e-6);
}

TEST_CASE ("a silent track skips its work and resumes at the current position")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    auto set = [&] (const char* id, float v)
    {
        auto* param = p.state.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    };
    set ("hall", 0.0f); // Dry: the direct sound alone, so the impulse position is exact

    p.setPlayConfigDetails (2, 2, sampleRate, 480);
    p.prepareToPlay (sampleRate, 480);

    juce::AudioBuffer<float> buffer (2, 480);
    juce::MidiBuffer midi;

    buffer.setSample (0, 0, 1.0f);
    p.processBlock (buffer, midi);

    for (int block = 0; block < 1100; ++block) // 11 s of silence (longer than the 10 s tail)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
    }

    CHECK (buffer.getMagnitude (0, 480) <= 0.0f);

    set ("playerY", 10.0f); // moved while silent: 10 m -> 1399.4 samples
    std::vector<float> left;
    for (int block = 0; block < 4; ++block)
    {
        buffer.clear();
        if (block == 0)
            buffer.setSample (0, 0, 1.0f);
        p.processBlock (buffer, midi);
        left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 480);
    }

    const auto peak = std::distance (left.begin(), std::max_element (left.begin(), left.end()));
    CHECK (std::abs ((double) peak - 10.0 / 343.0 * sampleRate) <= 1.0);
}
