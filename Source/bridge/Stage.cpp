#include "Stage.h"
#include "../dsp/Acoustics.h"

namespace stage
{
namespace
{
// Positions are stored on a 1 cm grid; the float range conversion leaves residue like -5.6e-7.
float centimetres (float metres)
{
    return std::round (metres * 100.0f) / 100.0f;
}

geometry::Point listenerOf (const ComposerProcessor& p)
{
    return geometry::clampToRoom ({ centimetres (p.getParam (params::listenerX)), centimetres (p.getParam (params::listenerY)) },
                                  p.getParam (params::roomSize));
}

geometry::Point playerOf (const ComposerProcessor& p, float roomSize)
{
    return geometry::clampToRoom ({ centimetres (p.getParam (params::playerX)), centimetres (p.getParam (params::playerY)) }, roomSize);
}

juce::var point (geometry::Point p)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("x", p.x);
    o->setProperty ("y", p.y);
    return o.release();
}

juce::var player (const ComposerProcessor& p, int index, bool self, float roomSize, geometry::Point listener,
                  float lawExponent)
{
    const auto pos = playerOf (p, roomSize);

    auto derived = std::make_unique<juce::DynamicObject>();
    derived->setProperty ("distance", geometry::distance (pos, listener));
    derived->setProperty ("azimuth", geometry::azimuthDegrees (pos, listener));
    derived->setProperty ("pan", geometry::pan (pos, listener));
    derived->setProperty ("delayMs", geometry::delayMs (pos, listener));
    derived->setProperty ("levelDb", 20.0f * std::log10 (geometry::distanceGain (geometry::distance (pos, listener), lawExponent)));

    const auto name = p.getDisplayName();

    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("id", p.id);
    o->setProperty ("name", name.isNotEmpty() ? name : "Player " + juce::String (index + 1));
    o->setProperty ("colour", p.getTrackColour());
    o->setProperty ("x", pos.x);
    o->setProperty ("y", pos.y);
    o->setProperty ("self", self);
    o->setProperty ("derived", derived.release());
    return o.release();
}

void apply (ComposerProcessor& p, const juce::String& phase, const juce::String& paramId, float value)
{
    if (phase == "begin")
        p.beginGesture (paramId);
    else if (phase == "change")
        p.writeParam (paramId, value);
    else if (phase == "end")
        p.endGesture (paramId);
}

} // namespace

geometry::Point freeSeat (geometry::Point listener, float roomSize, const std::vector<geometry::Point>& taken)
{
    const auto spacing = std::clamp (roomSize / 10.0f, 0.75f, 2.0f);
    const auto edge = roomSize * 0.5f - 0.5f; // seats keep 0.5 m from the walls
    const auto inside = [&] (geometry::Point p) { return std::abs (p.x) <= edge + 1.0e-4f && std::abs (p.y) <= edge + 1.0e-4f; };
    const auto nearest = [&] (geometry::Point p)
    {
        auto d = std::numeric_limits<float>::max();
        for (const auto& t : taken)
            d = std::min (d, geometry::distance (p, t));
        return d;
    };

    // The first row: 3 spacings in front of the listener, or as far as the front wall allows.
    const geometry::Point front { listener.x, std::min (listener.y + 3.0f * spacing, edge) };
    const auto reach = (int) std::ceil (roomSize / spacing) + 1;

    // Seats on a grid through the first row, in order of preference (lower = better): rows of 7
    // front first, then wider rows, then rows nearer the listener, then beside and behind;
    // within a row, centre-out (0, -1, 1, -2, 2 ...).
    struct Seat { std::array<int, 3> rank; geometry::Point at; };
    std::vector<Seat> seats;

    for (int row = -2 * reach; row <= reach; ++row)
        for (int column = -reach; column <= reach; ++column)
        {
            const geometry::Point at { front.x + spacing * (float) column, front.y + spacing * (float) row };
            if (! inside (at) || geometry::distance (at, listener) < spacing)
                continue;

            const auto tier = row >= 0 ? (std::abs (column) <= 3 ? 0 : 1)
                                       : (at.y > listener.y + 0.5f * spacing ? 2 : 3);
            seats.push_back ({ { tier, std::abs (row), 2 * std::abs (column) - (column < 0 ? 1 : 0) }, at });
        }

    std::sort (seats.begin(), seats.end(), [] (const Seat& a, const Seat& b) { return a.rank < b.rank; });

    for (const auto& seat : seats)
        if (nearest (seat.at) >= 0.5f * spacing)
            return seat.at;

    // Full: the point (on a finer grid) farthest from everyone, never on top of another.
    auto best = front;
    auto bestDistance = -1.0f;
    const auto step = 0.25f * spacing;
    for (auto y = -edge; y <= edge + 1.0e-4f; y += step)
        for (auto x = -edge; x <= edge + 1.0e-4f; x += step)
            if (const auto d = nearest ({ x, y }); d > bestDistance)
                best = { x, y }, bestDistance = d;

    return best;
}


juce::var snapshot (ComposerProcessor& self)
{
    auto o = std::make_unique<juce::DynamicObject>();
    const auto roomSize = self.getParam (params::roomSize);
    const auto listener = listenerOf (self);
    const auto lawExponent = self.getParam (params::proDistanceLaw) / params::dbPerDoublingFreeField;

    // ponytail: the viewing instance's room/listener stand for the stage. The copies can
    // drift if someone edits one track's automation lane by hand; add an "out of sync" hint
    // + "sync" action if that turns out to matter.
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        juce::Array<juce::var> players;
        juce::StringArray stored { self.getScene() };

        for (int i = 0; i < members.size(); ++i)
        {
            players.add (player (*members[i], i, members[i] == &self, roomSize, listener, lawExponent));
            stored.addArray (members[i]->getStoredScenes());

            // Hosts give a plugin no way to close its window, so a second editor offers
            // to step aside instead (see App.tsx).
            if (members[i] != &self && members[i]->getActiveEditor() != nullptr && ! o->hasProperty ("openElsewhere"))
                o->setProperty ("openElsewhere", players.getReference (i)["name"]);
        }

        stored.removeDuplicates (false);
        stored.sort (false);

        juce::Array<juce::var> scenes;

        for (const auto& name : stored)
            scenes.add (name);

        o->setProperty ("alone", members.size() == 1);
        o->setProperty ("players", players);
        o->setProperty ("scenes", scenes);
    });

    o->setProperty ("scene", self.getScene());
    o->setProperty ("outputMode", self.getParam (params::outputMode) > 0.5f ? "binaural" : "stereo");

    // The yaw itself changes every tick while tracking: it goes out as its own "headYaw"
    // event (ComposerEditor), so the stage stays quiet.
    auto tracking = std::make_unique<juce::DynamicObject>();
    tracking->setProperty ("live", self.headTracking->isLive());
   #if JUCE_MAC
    tracking->setProperty ("canStart", true); // the editor can start Agogica Tracker
   #else
    tracking->setProperty ("canStart", false); // ponytail: not installed on Windows yet
   #endif
    o->setProperty ("headTracking", tracking.release());

    // Pro panel values (raw parameter values; the UI shows them with their units).
    auto pro = std::make_unique<juce::DynamicObject>();
    for (const auto& id : params::ofStage)
        if (id.startsWith ("pro"))
            pro->setProperty (id, self.getParam (id));
    o->setProperty ("pro", pro.release());
    o->setProperty ("room", roomSize);
    o->setProperty ("listener", point (listener));
    return o.release();
}

void applyEdit (ComposerProcessor& self, const juce::var& edit)
{
    // The page is the other side of a trust boundary: anything but a whole, well-formed edit
    // is dropped. (A missing target would read as 0, the first track; a NaN arrives as null.)
    const auto isNumber = [] (const juce::var& v) { return (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite ((double) v); };
    const auto paramId = edit["param"].toString();
    const auto phase = edit["phase"].toString();

    if (! (phase == "begin" || phase == "change" || phase == "end") || ! isNumber (edit["target"])
        || (phase == "change" && ! isNumber (edit["value"])))
        return;

    const auto value = (float) edit["value"];
    const auto target = (int) edit["target"];

    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
        {
            if (params::ofStage.contains (paramId) || (params::ofPlayer.contains (paramId) && p->id == target))
            {
                apply (*p, phase, paramId, value);
                p->markPlaced();
            }
        }
    });
}

void OpenGestures::applyEdit (ComposerProcessor& self, const juce::var& edit)
{
    const Begun gesture { (int) edit["target"], edit["param"].toString() };
    const auto same = [&] (const Begun& other) { return other.target == gesture.target && other.paramId == gesture.paramId; };

    if (edit["phase"] == "begin" && std::none_of (begun.begin(), begun.end(), same))
        begun.push_back (gesture);
    else if (edit["phase"] == "end")
        std::erase_if (begun, same);

    stage::applyEdit (self, edit);
}

void OpenGestures::endAll (ComposerProcessor& self)
{
    for (const auto& gesture : std::exchange (begun, {}))
    {
        auto end = std::make_unique<juce::DynamicObject>();
        end->setProperty ("phase", "end");
        end->setProperty ("target", gesture.target);
        end->setProperty ("param", gesture.paramId);
        stage::applyEdit (self, end.release());
    }
}

void setName (ComposerProcessor& self, int id, const juce::String& name)
{
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
            if (p->id == id)
                p->setNameOverride (name.trim().substring (0, maxNameLength));
    });
}

void setScene (ComposerProcessor& self, const juce::String& scene)
{
    if (scene.length() != 1 || ! juce::String ("ABCDEFGH").contains (scene))
        return;

    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        // The scene's room and listener are the same for everyone; a place is each track's own.
        // A track that has never been in this scene (added later) keeps its place and takes
        // the room and listener from a track that has.
        ComposerProcessor* hasBeenThere = nullptr;
        juce::Array<ComposerProcessor*> newToScene;

        for (auto* p : members)
        {
            if (p->getScene() == scene)
                continue;

            p->storeScene();

            if (p->recallScene (scene))
                hasBeenThere = p;
            else
                newToScene.add (p);

            p->setActiveScene (scene);
        }

        if (hasBeenThere != nullptr)
            for (auto* p : newToScene)
                for (const auto& paramId : params::ofScene)
                    if (params::ofStage.contains (paramId))
                        p->writeParam (paramId, hasBeenThere->getParam (paramId));
    });
}

void join (ComposerProcessor& self, bool takeSeat)
{
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        ComposerProcessor* source = nullptr;

        for (auto* p : members)
            if (p != &self && p->isPlaced())
                source = source == nullptr ? p : source;

        if (source != nullptr)
        {
            self.setActiveScene (source->getScene());

            for (const auto& paramId : params::ofStage)
                self.writeParam (paramId, source->getParam (paramId));
        }

        if (takeSeat && source != nullptr)
        {
            // Where everyone is drawn (clamped to the room), not their stored values.
            const auto roomSize = self.getParam (params::roomSize);
            std::vector<geometry::Point> taken;
            for (auto* p : members)
                if (p != &self)
                    taken.push_back (playerOf (*p, roomSize));

            const auto s = freeSeat (listenerOf (self), roomSize, taken);
            self.writeParam (params::playerX, s.x);
            self.writeParam (params::playerY, s.y);
        }

        self.markPlaced();
    });
}
} // namespace stage
