#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/PluginProcessor.h"

namespace
{
using Catch::Matchers::WithinAbs;

void setRoomSize (ComposerProcessor& p, float metres)
{
    auto* param = p.state.getParameter ("roomSize");
    param->setValueNotifyingHost (param->convertTo0to1 (metres));
}

float roomSize (const ComposerProcessor& p)
{
    return p.state.getRawParameterValue ("roomSize")->load();
}
} // namespace

TEST_CASE ("roomSize defaults to 20 m and snaps to 5 m steps")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    CHECK_THAT (roomSize (p), WithinAbs (20.0, 1e-4));

    setRoomSize (p, 12.0f);
    CHECK_THAT (roomSize (p), WithinAbs (10.0, 1e-4));

    setRoomSize (p, 13.0f);
    CHECK_THAT (roomSize (p), WithinAbs (15.0, 1e-4));

    setRoomSize (p, 80.0f);
    CHECK_THAT (roomSize (p), WithinAbs (50.0, 1e-4));
}

TEST_CASE ("state round-trip restores roomSize")
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::MemoryBlock saved;

    {
        ComposerProcessor p;
        setRoomSize (p, 35.0f);
        p.getStateInformation (saved);
    }

    ComposerProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK_THAT (roomSize (restored), WithinAbs (35.0, 1e-4));
}
