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

TEST_CASE ("every instance in the process is on the stage")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor a, b;

    CHECK (stage::snapshot (a)["players"].size() == 2);
    CHECK_FALSE ((bool) stage::snapshot (a)["alone"]);

    {
        ComposerProcessor c;
        CHECK (stage::snapshot (a)["players"].size() == 3);
    }

    CHECK (stage::snapshot (a)["players"].size() == 2); // destruction unregisters
}

TEST_CASE ("room and listener edits reach every instance; player edits only the target")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor a, b;

    drag (a, a.id, "roomSize", 35.0f);
    drag (b, b.id, "listenerX", -2.0f);
    drag (a, b.id, "playerX", 4.0f); // a's editor moves b's player

    for (auto* p : { &a, &b })
    {
        CHECK_THAT (p->getParam ("roomSize"), WithinAbs (35.0, 1e-4));
        CHECK_THAT (p->getParam ("listenerX"), WithinAbs (-2.0, 1e-4));
    }

    CHECK_THAT (b.getParam ("playerX"), WithinAbs (4.0, 1e-4));
    CHECK_THAT (a.getParam ("playerX"), WithinAbs (0.0, 1e-4));
}

TEST_CASE ("a new insert adopts the stage's scene, room/listener and takes a free seat")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor a;
    stage::join (a, true); // alone: stays where it is
    stage::setScene (a, "C");
    drag (a, a.id, "roomSize", 30.0f);
    drag (a, a.id, "listenerY", -3.0f);
    drag (a, a.id, "playerY", 3.0f); // seat 0 (0, 3) is taken by a

    ComposerProcessor b;
    stage::join (b, true);

    CHECK (b.getScene() == "C");
    CHECK_THAT (b.getParam ("roomSize"), WithinAbs (30.0, 1e-4));
    CHECK_THAT (b.getParam ("listenerY"), WithinAbs (-3.0, 1e-4));
    CHECK_THAT (b.getParam ("playerX"), WithinAbs (-2.0, 1e-4)); // seat 1: left of centre
    CHECK_THAT (b.getParam ("playerY"), WithinAbs (3.0, 1e-4));
    CHECK (b.isPlaced());
}

TEST_CASE ("scenes store and recall every instance's layout")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor a, b;

    drag (a, a.id, "playerX", 1.0f);
    drag (a, b.id, "playerX", 2.0f);
    drag (a, a.id, "roomSize", 25.0f);

    stage::setScene (b, "B"); // first visit: starts as a copy of A
    CHECK (a.getScene() == "B");
    CHECK_THAT (a.getParam ("playerX"), WithinAbs (1.0, 1e-4));

    drag (a, a.id, "playerX", -6.0f);
    drag (a, b.id, "playerX", -7.0f);
    drag (a, a.id, "roomSize", 40.0f);

    stage::setScene (a, "A");
    CHECK_THAT (a.getParam ("playerX"), WithinAbs (1.0, 1e-4));
    CHECK_THAT (b.getParam ("playerX"), WithinAbs (2.0, 1e-4));
    CHECK_THAT (b.getParam ("roomSize"), WithinAbs (25.0, 1e-4));

    stage::setScene (a, "B");
    CHECK_THAT (a.getParam ("playerX"), WithinAbs (-6.0, 1e-4));
    CHECK_THAT (b.getParam ("playerX"), WithinAbs (-7.0, 1e-4));
    CHECK_THAT (a.getParam ("roomSize"), WithinAbs (40.0, 1e-4));

    const auto scenes = stage::snapshot (a)["scenes"];
    CHECK (scenes.size() == 2);

    stage::setScene (a, "Z"); // not a scene: ignored
    CHECK (a.getScene() == "B");
}

TEST_CASE ("state round-trip keeps scenes and placed")
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::MemoryBlock saved;

    {
        ComposerProcessor p;
        drag (p, p.id, "playerX", 3.0f);
        stage::setScene (p, "D");
        drag (p, p.id, "playerX", -3.0f);
        p.getStateInformation (saved);
    }

    ComposerProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (restored.getScene() == "D");
    CHECK (restored.isPlaced());
    CHECK_THAT (restored.getParam ("playerX"), WithinAbs (-3.0, 1e-4));

    stage::setScene (restored, "A");
    CHECK_THAT (restored.getParam ("playerX"), WithinAbs (3.0, 1e-4));
}

TEST_CASE ("output mode is shared, adopted by new inserts, saved, and not part of scenes")
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::MemoryBlock saved;

    {
        ComposerProcessor a, b;
        a.markPlaced();
        drag (a, a.id, "outputMode", 1.0f);
        CHECK_THAT (b.getParam ("outputMode"), WithinAbs (1.0, 1e-4));
        CHECK (stage::snapshot (b)["outputMode"].toString() == "binaural");

        ComposerProcessor c;
        stage::join (c, true);
        CHECK_THAT (c.getParam ("outputMode"), WithinAbs (1.0, 1e-4));

        stage::setScene (a, "B");
        drag (a, a.id, "outputMode", 0.0f);
        stage::setScene (a, "A");
        CHECK_THAT (a.getParam ("outputMode"), WithinAbs (0.0, 1e-4)); // scenes don't restore it

        drag (a, a.id, "outputMode", 1.0f);
        b.getStateInformation (saved);
    }

    ComposerProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK_THAT (restored.getParam ("outputMode"), WithinAbs (1.0, 1e-4));
}

TEST_CASE ("hall is shared, stored per scene, saved, and adopted by new inserts")
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::MemoryBlock saved;

    {
        ComposerProcessor a, b;
        a.markPlaced();
        drag (a, a.id, "hall", 4.0f); // church
        CHECK_THAT (b.getParam ("hall"), WithinAbs (4.0, 1e-4));
        CHECK ((int) stage::snapshot (b)["hall"] == 4);

        stage::setScene (a, "B");
        drag (a, a.id, "hall", 1.0f); // studio in scene B
        stage::setScene (a, "A");
        CHECK_THAT (b.getParam ("hall"), WithinAbs (4.0, 1e-4)); // scene A's church is back
        stage::setScene (a, "B");
        CHECK_THAT (b.getParam ("hall"), WithinAbs (1.0, 1e-4));

        ComposerProcessor c;
        stage::join (c, true);
        CHECK_THAT (c.getParam ("hall"), WithinAbs (1.0, 1e-4));

        a.getStateInformation (saved);
    }

    ComposerProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK_THAT (restored.getParam ("hall"), WithinAbs (1.0, 1e-4));
    stage::setScene (restored, "A");
    CHECK_THAT (restored.getParam ("hall"), WithinAbs (4.0, 1e-4));
}

TEST_CASE ("snapshot names another instance whose editor is open")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor a, b;
    a.setNameOverride ("Violin I");

    CHECK_FALSE (stage::snapshot (b).hasProperty ("openElsewhere"));

    std::unique_ptr<juce::AudioProcessorEditor> editor { a.createEditorIfNeeded() };
    CHECK (stage::snapshot (b)["openElsewhere"] == juce::var ("Violin I"));
    CHECK_FALSE (stage::snapshot (a).hasProperty ("openElsewhere")); // its own editor doesn't count

    a.editorBeingDeleted (editor.get()); // what the plugin wrappers do before deleting it
    editor.reset();
    CHECK_FALSE (stage::snapshot (b).hasProperty ("openElsewhere"));
}
