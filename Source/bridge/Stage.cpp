#include "Stage.h"
#include "../dsp/Acoustics.h"

namespace stage
{
namespace
{
// Shared by every instance (each keeps its own copy). outputMode is a listening setting,
// not part of scenes (ComposerProcessor::sceneParams); hall is.
const juce::StringArray sharedParams { "roomSize", "listenerX", "listenerY", "outputMode", "hall",
                                      "proReflections", "proDiffusion", "proFloor", "proBrightness", "proReverb",
                                      "proDecay", "proPreDelay", "proDistanceLaw", "proAir" };
const juce::StringArray playerParams { "playerX", "playerY" };

// Positions are stored on a 1 cm grid; the float range conversion leaves residue like -5.6e-7.
float centimetres (float metres)
{
    return std::round (metres * 100.0f) / 100.0f;
}

geometry::Point listenerOf (const ComposerProcessor& p)
{
    return geometry::clampToRoom ({ centimetres (p.getParam ("listenerX")), centimetres (p.getParam ("listenerY")) },
                                  p.getParam ("roomSize"));
}

geometry::Point playerOf (const ComposerProcessor& p, float roomSize)
{
    return geometry::clampToRoom ({ centimetres (p.getParam ("playerX")), centimetres (p.getParam ("playerY")) }, roomSize);
}

juce::var point (geometry::Point p)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("x", p.x);
    o->setProperty ("y", p.y);
    return o.release();
}

juce::var player (const ComposerProcessor& p, int index, bool self, float roomSize, geometry::Point listener,
                  float lawExponent, float airAmount)
{
    const auto pos = playerOf (p, roomSize);

    auto derived = std::make_unique<juce::DynamicObject>();
    derived->setProperty ("distance", geometry::distance (pos, listener));
    derived->setProperty ("azimuth", geometry::azimuthDegrees (pos, listener));
    derived->setProperty ("pan", geometry::pan (pos, listener));
    derived->setProperty ("delayMs", geometry::delayMs (pos, listener));
    derived->setProperty ("levelDb", 20.0f * std::log10 (geometry::distanceGain (geometry::distance (pos, listener), lawExponent)));
    derived->setProperty ("airDb", -geometry::airLossDbAt8k (geometry::distance (pos, listener), airAmount));

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
    auto* param = p.state.getParameter (paramId);

    if (phase == "begin")
        param->beginChangeGesture();
    else if (phase == "change")
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    else if (phase == "end")
        param->endChangeGesture();
}

// Seats fill rows of 7, centre-out, 2 m apart, starting 6 m in front of the listener.
geometry::Point seat (int index, geometry::Point listener)
{
    static constexpr int order[] { 0, -1, 1, -2, 2, -3, 3 };
    return { listener.x + 2.0f * (float) order[index % 7], listener.y + 6.0f + 2.0f * (float) (index / 7) };
}
} // namespace

juce::var snapshot (ComposerProcessor& self)
{
    auto o = std::make_unique<juce::DynamicObject>();
    const auto roomSize = self.getParam ("roomSize");
    const auto listener = listenerOf (self);
    const auto lawExponent = self.getParam ("proDistanceLaw") / (20.0f * std::log10 (2.0f));
    const auto airAmount = self.getParam ("proAir");

    // ponytail: the viewing instance's room/listener stand for the stage. The copies can
    // drift if someone edits one track's automation lane by hand; add an "out of sync" hint
    // + "sync" action if that turns out to matter.
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        juce::Array<juce::var> players;
        juce::StringArray stored { self.getScene() };

        for (int i = 0; i < members.size(); ++i)
        {
            players.add (player (*members[i], i, members[i] == &self, roomSize, listener, lawExponent, airAmount));
            stored.addArray (members[i]->getStoredScenes());
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
    o->setProperty ("outputMode", self.getParam ("outputMode") > 0.5f ? "binaural" : "stereo");
    o->setProperty ("hall", (int) self.getParam ("hall"));

    juce::Array<juce::var> hallNames;
    for (const auto& h : halls)
        hallNames.add (juce::String (h.name));
    o->setProperty ("halls", hallNames);

    // Pro panel values (raw parameter values; the UI shows them with their units).
    auto pro = std::make_unique<juce::DynamicObject>();
    for (const auto& id : sharedParams)
        if (id.startsWith ("pro"))
            pro->setProperty (id, self.getParam (id));
    o->setProperty ("pro", pro.release());
    o->setProperty ("room", roomSize);
    o->setProperty ("listener", point (listener));
    return o.release();
}

void applyEdit (ComposerProcessor& self, const juce::var& edit)
{
    const auto paramId = edit["param"].toString();
    const auto phase = edit["phase"].toString();
    const auto value = (float) edit["value"];
    const auto target = (int) edit["target"];

    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
        {
            if (sharedParams.contains (paramId) || (playerParams.contains (paramId) && p->id == target))
            {
                apply (*p, phase, paramId, value);
                p->markPlaced();
            }
        }
    });
}

void setName (ComposerProcessor& self, int id, const juce::String& name)
{
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
            if (p->id == id)
                p->setNameOverride (name);
    });
}

void setScene (ComposerProcessor& self, const juce::String& scene)
{
    if (scene.length() != 1 || ! juce::String ("ABCDEFGH").contains (scene))
        return;

    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
        {
            if (p->getScene() == scene)
                continue;

            p->storeScene();
            p->recallScene (scene);
            p->setActiveScene (scene);
        }
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

            for (const auto& paramId : sharedParams)
                self.writeParam (paramId, source->getParam (paramId));
        }

        if (takeSeat && source != nullptr)
        {
            const auto listener = geometry::Point { self.getParam ("listenerX"), self.getParam ("listenerY") };

            auto taken = [&] (geometry::Point s)
            {
                for (auto* p : members)
                    if (p != &self && geometry::distance ({ p->getParam ("playerX"), p->getParam ("playerY") }, s) < 0.5f)
                        return true;

                return false;
            };

            for (int i = 0; i < 49; ++i)
            {
                if (const auto s = seat (i, listener); ! taken (s))
                {
                    self.writeParam ("playerX", s.x);
                    self.writeParam ("playerY", s.y);
                    break;
                }
            }
        }

        self.markPlaced();
    });
}
} // namespace stage
