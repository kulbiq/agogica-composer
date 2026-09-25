#include "Stage.h"
#include "../dsp/Geometry.h"

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
    return { centimetres (p.getParam ("listenerX")), centimetres (p.getParam ("listenerY")) };
}

geometry::Point playerOf (const ComposerProcessor& p)
{
    return geometry::clampToRoom ({ centimetres (p.getParam ("playerX")), centimetres (p.getParam ("playerY")) },
                                  p.getParam ("roomSize"));
}

juce::var point (geometry::Point p)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("x", p.x);
    o->setProperty ("y", p.y);
    return o.release();
}

juce::var player (const ComposerProcessor& p, int index, bool self, geometry::Point listener)
{
    const auto pos = playerOf (p);

    auto derived = std::make_unique<juce::DynamicObject>();
    derived->setProperty ("distance", geometry::distance (pos, listener));
    derived->setProperty ("azimuth", geometry::azimuthDegrees (pos, listener));
    derived->setProperty ("pan", geometry::pan (pos, listener));
    derived->setProperty ("delayMs", geometry::delayMs (pos, listener));

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
} // namespace

juce::var snapshot (ComposerProcessor& self)
{
    const auto listener = geometry::clampToRoom (listenerOf (self), self.getParam ("roomSize"));

    juce::Array<juce::var> players;
    players.add (player (self, 0, true, listener));

    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("group", self.getGroup());
    o->setProperty ("alone", true);
    o->setProperty ("room", self.getParam ("roomSize"));
    o->setProperty ("listener", point (listener));
    o->setProperty ("players", players);
    return o.release();
}

void applyEdit (ComposerProcessor& self, const juce::var& edit)
{
    static const juce::StringArray params { "roomSize", "listenerX", "listenerY", "playerX", "playerY" };

    const auto paramId = edit["param"].toString();

    if (! params.contains (paramId) || (int) edit["target"] != self.id)
        return;

    auto* param = self.state.getParameter (paramId);
    const auto phase = edit["phase"].toString();

    if (phase == "begin")
        param->beginChangeGesture();
    else if (phase == "change")
        param->setValueNotifyingHost (param->convertTo0to1 ((float) edit["value"]));
    else if (phase == "end")
        param->endChangeGesture();
}
} // namespace stage
