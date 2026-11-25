#include "StageEdits.h"

namespace stage
{
namespace
{
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
} // namespace stage
