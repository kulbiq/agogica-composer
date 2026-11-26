#include "StageSnapshot.h"
#include "../stage/Places.h"
#include "../dsp/Acoustics.h"

namespace stage
{
namespace
{
juce::var point (geometry::Point p)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("x", p.x);
    o->setProperty ("y", p.y);
    return o.release();
}

juce::var player (const ComposerProcessor& p, int index, bool self, float roomSize)
{
    const auto pos = playerPlace (p, roomSize);

    const auto name = p.getPlayerLabel();
    const auto track = p.getDisplayName();

    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("id", p.id);
    o->setProperty ("name", name.isNotEmpty() ? name : "Player " + juce::String (index + 1));
    o->setProperty ("colour", p.getTrackColour());
    o->setProperty ("icon", p.getIcon());
    o->setProperty ("player", p.getPlayerKey());
    o->setProperty ("track", track.isNotEmpty() ? track : "Track " + juce::String (index + 1));
    o->setProperty ("x", pos.x);
    o->setProperty ("y", pos.y);
    o->setProperty ("self", self);
    return o.release();
}
} // namespace

juce::var snapshot (ComposerProcessor& self)
{
    auto o = std::make_unique<juce::DynamicObject>();
    const auto roomSize = self.getParam (params::roomSize);
    const auto listener = listenerPlace (self);

    // ponytail: the viewing instance's room/listener stand for the stage. The copies can
    // drift if someone edits one track's automation lane by hand; add an "out of sync" hint
    // + "sync" action if that turns out to matter.
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        juce::Array<juce::var> players;
        juce::StringArray stored { self.getScene() };

        for (int i = 0; i < members.size(); ++i)
        {
            players.add (player (*members[i], i, members[i] == &self, roomSize));
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
    o->setProperty ("audience", self.getAudience());
    o->setProperty ("outputMode", self.getParam (params::outputMode) > 0.5f ? "binaural" : "stereo");

    // The yaw itself changes every tick while tracking: it goes out as its own "headYaw"
    // event (ComposerEditor), so the stage stays quiet.
    auto tracking = std::make_unique<juce::DynamicObject>();
    tracking->setProperty ("live", self.headTracking->isLive());
    tracking->setProperty ("on", self.isHeadTrackingOn());
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
    auto listenerObject = point (listener);
    listenerObject.getDynamicObject()->setProperty ("yaw", self.getParam (params::listenerYaw));
    o->setProperty ("listener", listenerObject);
    return o.release();
}
} // namespace stage
