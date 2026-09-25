#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/bridge/Stage.h"

using Catch::Matchers::WithinAbs;

namespace
{
juce::var edit (juce::String phase, int target, juce::String param, float value = 0.0f)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("phase", phase);
    o->setProperty ("target", target);
    o->setProperty ("param", param);
    o->setProperty ("value", value);
    return o.release();
}

void drag (ComposerProcessor& p, int target, const juce::String& param, float value)
{
    stage::applyEdit (p, edit ("begin", target, param));
    stage::applyEdit (p, edit ("change", target, param, value));
    stage::applyEdit (p, edit ("end", target, param));
}
} // namespace

TEST_CASE ("applyEdit writes parameters in metres")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    drag (p, p.id, "playerX", -3.25f);
    drag (p, p.id, "listenerY", 2.0f);
    CHECK_THAT (p.getParam ("playerX"), WithinAbs (-3.25, 1e-4));
    CHECK_THAT (p.getParam ("listenerY"), WithinAbs (2.0, 1e-4));
}

TEST_CASE ("applyEdit ignores unknown params and other targets")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    drag (p, p.id + 1000, "playerX", 7.0f);
    drag (p, p.id, "notAParam", 7.0f);
    CHECK_THAT (p.getParam ("playerX"), WithinAbs (0.0, 1e-4));
}

TEST_CASE ("snapshot clamps to the room but keeps stored positions")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    drag (p, p.id, "playerX", 20.0f);
    drag (p, p.id, "roomSize", 10.0f);

    const auto s = stage::snapshot (p);
    const auto self = s["players"][0];
    CHECK (self["self"]);
    CHECK_THAT ((double) self["x"], WithinAbs (5.0, 1e-4));             // drawn at the edge
    CHECK_THAT (p.getParam ("playerX"), WithinAbs (20.0, 1e-4));        // stored value kept
    CHECK_THAT ((double) self["derived"]["distance"], WithinAbs (std::hypot (5.0, 5.0), 1e-3));
}

TEST_CASE ("snapshot positions have no float residue")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    const auto s = stage::snapshot (p);
    CHECK_THAT ((double) s["listener"]["x"], WithinAbs (0.0, 0.0));
    CHECK_THAT ((double) s["players"][0]["y"], WithinAbs (5.0, 0.0));
}

TEST_CASE ("state round-trip keeps the name override")
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::MemoryBlock saved;

    {
        ComposerProcessor p;
        p.setNameOverride ("Viola");
        p.getStateInformation (saved);
    }

    ComposerProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (restored.getDisplayName() == "Viola");
}
