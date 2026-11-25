#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/bridge/StageSnapshot.h"
#include "../Source/bridge/StageEdits.h"
#include "../Source/stage/Scenes.h"
#include "../Source/stage/Seating.h"

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

// What the host hears about gestures on one instance: every begin needs its end, and a
// parameter is never begun twice (hosts latch automation, or stop recording, otherwise).
struct HostGestureLog final : juce::AudioProcessorListener
{
    explicit HostGestureLog (ComposerProcessor& p) : processor (p)   { processor.addListener (this); }
    ~HostGestureLog() override                                       { processor.removeListener (this); }

    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override
    {
        begunTwice = begunTwice || open.contains (index);
        open.addIfNotAlreadyThere (index);
    }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override
    {
        endedUnbegun = endedUnbegun || ! open.contains (index);
        open.removeAllInstancesOf (index);
    }

    bool balanced() const   { return open.isEmpty() && ! begunTwice && ! endedUnbegun; }

    ComposerProcessor& processor;
    juce::Array<int> open;
    bool begunTwice = false, endedUnbegun = false;
};
} // namespace

TEST_CASE ("gestures reach the host balanced, whatever the page and the stage do", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor a, b;
    HostGestureLog hostOfA (a), hostOfB (b);

    SECTION ("a gesture begun twice, or ended twice")
    {
        stage::applyEdit (a, edit ("begin", a.id, "playerX"));
        stage::applyEdit (a, edit ("begin", a.id, "playerX"));
        stage::applyEdit (a, edit ("change", a.id, "playerX", 2.0f));
        stage::applyEdit (a, edit ("end", a.id, "playerX"));
        stage::applyEdit (a, edit ("end", a.id, "playerX"));
        CHECK_THAT (a.getParam ("playerX"), WithinAbs (2.0, 1e-4));
    }

    SECTION ("a scene switch from another window during a drag")
    {
        stage::applyEdit (a, edit ("begin", a.id, "playerX"));
        stage::setScene (b, "B");
        stage::setScene (b, "A");
        stage::applyEdit (a, edit ("change", a.id, "playerX", 2.0f));
        stage::applyEdit (a, edit ("end", a.id, "playerX"));
    }

    SECTION ("a track that joins while the room is being dragged")
    {
        stage::applyEdit (a, edit ("begin", a.id, "roomSize"));
        ComposerProcessor late;
        HostGestureLog hostOfLate (late);
        stage::join (late);
        stage::applyEdit (a, edit ("change", a.id, "roomSize", 30.0f));
        stage::applyEdit (a, edit ("end", a.id, "roomSize"));
        CHECK (hostOfLate.balanced());
        CHECK_THAT (late.getParam ("roomSize"), WithinAbs (30.0, 1e-4));
    }

    SECTION ("the window closes in the middle of a drag")
    {
        {
            stage::OpenGestures ofThisWindow;
            ofThisWindow.applyEdit (a, edit ("begin", a.id, "listenerX"));
            ofThisWindow.applyEdit (a, edit ("change", a.id, "listenerX", 2.0f));
            ofThisWindow.endAll (a);
        }
        CHECK_THAT (b.getParam ("listenerX"), WithinAbs (2.0, 1e-4));
    }

    CHECK (hostOfA.balanced());
    CHECK (hostOfB.balanced());
}

TEST_CASE ("applyEdit: writes metres, ignores unknown params and other targets", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    drag (p, p.id, "playerX", -3.25f);
    drag (p, p.id, "listenerY", 2.0f);
    CHECK_THAT (p.getParam ("playerX"), WithinAbs (-3.25, 1e-4));
    CHECK_THAT (p.getParam ("listenerY"), WithinAbs (2.0, 1e-4));

    drag (p, p.id + 1000, "playerX", 7.0f);
    drag (p, p.id, "notAParam", 7.0f);
    CHECK_THAT (p.getParam ("playerX"), WithinAbs (-3.25, 1e-4));

    SECTION ("an edit the page could never send does nothing")
    {
        auto change = [&] (std::function<void (juce::DynamicObject&)> spoil)
        {
            auto e = edit ("change", p.id, "playerX", 7.0f);
            spoil (*e.getDynamicObject());
            stage::applyEdit (p, e);
        };

        change ([] (auto& e) { e.removeProperty ("value"); });
        change ([] (auto& e) { e.setProperty ("value", juce::var()); }); // what NaN becomes as JSON
        change ([] (auto& e) { e.setProperty ("value", std::numeric_limits<float>::infinity()); });
        change ([] (auto& e) { e.setProperty ("value", "7"); });
        change ([] (auto& e) { e.removeProperty ("target"); });
        change ([] (auto& e) { e.setProperty ("target", "first"); });
        CHECK_THAT (p.getParam ("playerX"), WithinAbs (-3.25, 1e-4));

        ComposerProcessor fresh;
        stage::applyEdit (fresh, edit ("wobble", fresh.id, "playerX", 7.0f));
        CHECK_FALSE (fresh.isPlaced());
    }

    SECTION ("a name is cut to 64 characters")
    {
        stage::setName (p, p.id, juce::String::repeatedString ("x", 200));
        CHECK (p.getDisplayName().length() == 64);
    }
}

TEST_CASE ("snapshot: drawn clamped to the room, stored value kept, no float residue", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    const auto fresh = stage::snapshot (p);
    CHECK_THAT ((double) fresh["listener"]["x"], WithinAbs (0.0, 0.0));
    CHECK_THAT ((double) fresh["players"][0]["y"], WithinAbs (5.0, 0.0));

    drag (p, p.id, "playerX", 20.0f);
    drag (p, p.id, "roomSize", 10.0f);

    const auto s = stage::snapshot (p);
    const auto self = s["players"][0];
    CHECK (self["self"]);
    CHECK_THAT ((double) self["x"], WithinAbs (5.0, 1e-4));             // drawn at the edge
    CHECK_THAT (p.getParam ("playerX"), WithinAbs (20.0, 1e-4));        // stored value kept
    CHECK_THAT ((double) self["derived"]["distance"], WithinAbs (std::hypot (5.0, 5.0), 1e-3));
}

TEST_CASE ("state round-trip: parameters, name, scenes, placed", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::MemoryBlock saved;

    {
        ComposerProcessor p;
        p.setNameOverride ("Viola");
        drag (p, p.id, "roomSize", 35.0f);
        drag (p, p.id, "playerX", 3.0f);
        stage::setScene (p, "D");
        drag (p, p.id, "playerX", -3.0f);
        p.getStateInformation (saved);
    }

    ComposerProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (restored.getDisplayName() == "Viola");
    CHECK_THAT (restored.getParam ("roomSize"), WithinAbs (35.0, 1e-4));
    CHECK (restored.getScene() == "D");
    CHECK (restored.isPlaced());
    CHECK_THAT (restored.getParam ("playerX"), WithinAbs (-3.0, 1e-4));

    stage::setScene (restored, "A");
    CHECK_THAT (restored.getParam ("playerX"), WithinAbs (3.0, 1e-4));
}

TEST_CASE ("every instance in the process is on the stage", "[integration]")
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

TEST_CASE ("room and listener edits reach every instance; player edits only the target", "[integration]")
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

TEST_CASE ("a new insert adopts the stage's scene, room/listener and takes a free seat", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor a;
    stage::join (a); // alone: stays where it is
    stage::setScene (a, "C");
    drag (a, a.id, "roomSize", 30.0f);
    drag (a, a.id, "listenerY", -3.0f);
    drag (a, a.id, "playerY", 3.0f); // seat 0 (0, 3) is taken by a

    ComposerProcessor b;
    stage::join (b);

    CHECK (b.getScene() == "C");
    CHECK_THAT (b.getParam ("roomSize"), WithinAbs (30.0, 1e-4));
    CHECK_THAT (b.getParam ("listenerY"), WithinAbs (-3.0, 1e-4));
    CHECK_THAT (b.getParam ("playerX"), WithinAbs (-2.0, 1e-4)); // seat 1: left of centre
    CHECK_THAT (b.getParam ("playerY"), WithinAbs (3.0, 1e-4));
    CHECK (b.isPlaced());
}

TEST_CASE ("seats: everyone fits in the room, nobody sits on another")
{
    const auto [roomSize, minimumGap] = GENERATE (table<float, float> ({ { 20.0f, 2.0f }, { 10.0f, 1.0f }, { 5.0f, 0.35f } }));
    const auto listener = GENERATE (geometry::Point { 0.0f, 0.0f }, geometry::Point { 0.0f, 9.0f }, geometry::Point { -2.0f, -2.0f });
    const auto clamped = geometry::clampToRoom (listener, roomSize);

    std::vector<geometry::Point> taken { { 0.0f, 5.0f } }; // the first insert stays at its default
    taken.front() = geometry::clampToRoom (taken.front(), roomSize);
    for (int i = 0; i < 31; ++i)
        taken.push_back (stage::freeSeat (clamped, roomSize, taken));

    for (size_t i = 1; i < taken.size(); ++i)
    {
        INFO ("room " << roomSize << ", seat " << i << " at " << taken[i].x << ", " << taken[i].y);
        CHECK (std::abs (taken[i].x) <= roomSize * 0.5f - 0.5f + 1e-3f);
        CHECK (std::abs (taken[i].y) <= roomSize * 0.5f - 0.5f + 1e-3f);
        for (size_t j = 0; j < i; ++j) // the first player stands off the grid: half the gap from it
            CHECK (geometry::distance (taken[i], taken[j]) >= (j == 0 ? 0.5f : 1.0f) * minimumGap - 1e-3f);
    }
}

TEST_CASE ("seats: rows of 7 in front of the listener, centre-out, as before in a 20 m room")
{
    std::vector<geometry::Point> taken;
    for (int i = 0; i < 9; ++i)
        taken.push_back (stage::freeSeat ({ 0.0f, 0.0f }, 20.0f, taken));

    const std::array<float, 7> xs { 0.0f, -2.0f, 2.0f, -4.0f, 4.0f, -6.0f, 6.0f };
    for (size_t i = 0; i < 7; ++i)
    {
        CHECK_THAT (taken[i].x, WithinAbs (xs[i], 1e-4));
        CHECK_THAT (taken[i].y, WithinAbs (6.0, 1e-4));
    }
    CHECK_THAT (taken[7].x, WithinAbs (0.0, 1e-4)); // the next row, 2 m further back
    CHECK_THAT (taken[7].y, WithinAbs (8.0, 1e-4));
}

TEST_CASE ("scenes store and recall every instance's layout", "[integration]")
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

TEST_CASE ("a track added in a later scene shares the room and listener of every scene", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor a, b;
    drag (a, a.id, "roomSize", 25.0f);
    drag (a, a.id, "listenerX", 1.0f);

    stage::setScene (a, "B");
    drag (a, a.id, "roomSize", 40.0f);
    drag (a, a.id, "listenerX", -3.0f);

    ComposerProcessor late; // inserted while B is active: it has never been in A
    stage::join (late);
    const auto seatInB = late.getParam ("playerX");

    stage::setScene (a, "A");
    CHECK (late.getScene() == "A");
    CHECK_THAT (late.getParam ("roomSize"), WithinAbs (25.0, 1e-4));
    CHECK_THAT (late.getParam ("listenerX"), WithinAbs (1.0, 1e-4));
    CHECK_THAT (late.getParam ("playerX"), WithinAbs (seatInB, 1e-4)); // its place is its own: kept

    stage::setScene (a, "B");
    CHECK_THAT (late.getParam ("roomSize"), WithinAbs (40.0, 1e-4));
    CHECK_THAT (late.getParam ("listenerX"), WithinAbs (-3.0, 1e-4));
}

TEST_CASE ("stage-wide settings: shared, saved, adopted by new inserts; the room is per scene", "[integration]")
{
    // first: set in scene A; second: set in scene B; shown: how the snapshot reports `first`.
    const auto [id, first, second, perScene, shown] = GENERATE (table<const char*, float, float, bool, juce::var> ({
        { "outputMode", 1.0f, 0.0f, false, juce::var ("binaural") },
        { "roomSize", 35.0f, 10.0f, true, juce::var() },
        { "proDistanceLaw", 3.0f, 1.0f, false, juce::var() },
    }));
    CAPTURE (id);

    juce::ScopedJuceInitialiser_GUI juce;
    juce::MemoryBlock saved;

    {
        ComposerProcessor a, b;
        a.markPlaced();
        drag (a, a.id, id, first);
        CHECK_THAT (b.getParam (id), WithinAbs (first, 1e-4)); // shared
        if (! shown.isVoid())
            CHECK (stage::snapshot (b)[id] == shown);

        stage::setScene (a, "B");
        drag (a, a.id, id, second);
        stage::setScene (a, "A");
        CHECK_THAT (b.getParam (id), WithinAbs (perScene ? first : second, 1e-4)); // scene A's value is back, or untouched
        stage::setScene (a, "B");
        CHECK_THAT (b.getParam (id), WithinAbs (second, 1e-4));

        ComposerProcessor c;
        stage::join (c);
        CHECK_THAT (c.getParam (id), WithinAbs (second, 1e-4)); // adopted

        b.getStateInformation (saved);
    }

    ComposerProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK_THAT (restored.getParam (id), WithinAbs (second, 1e-4)); // saved
    stage::setScene (restored, "A");
    CHECK_THAT (restored.getParam (id), WithinAbs (perScene ? first : second, 1e-4));
}

TEST_CASE ("snapshot names another instance whose editor is open", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor a, b;
    a.setNameOverride ("Violin I");

    CHECK_FALSE (stage::snapshot (b).hasProperty ("openElsewhere"));

    std::unique_ptr<juce::AudioProcessorEditor> editor { a.createEditorAndMakeActive() };
    CHECK (stage::snapshot (b)["openElsewhere"] == juce::var ("Violin I"));
    CHECK_FALSE (stage::snapshot (a).hasProperty ("openElsewhere")); // its own editor doesn't count

    a.editorBeingDeleted (editor.get()); // what the plugin wrappers do before deleting it
    editor.reset();
    CHECK_FALSE (stage::snapshot (b).hasProperty ("openElsewhere"));
}
