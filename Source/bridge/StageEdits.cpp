#include "StageEdits.h"

#include <limits>
#include <optional>

namespace stage
{
namespace
{
enum class Phase { begin, change, end };

struct Edit
{
    Phase phase;
    juce::String paramId;
    int target;
    float value;
};

std::optional<Edit> validateEdit (const juce::var& edit)
{
    // The page is the other side of a trust boundary. A missing target would read as 0,
    // the first track; a NaN arrives as null.
    const auto isNumber = [] (const juce::var& v) { return (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite ((double) v); };
    const auto phase = edit["phase"].toString();
    const auto& target = edit["target"];
    const auto& value = edit["value"];
    if (! (phase == "begin" || phase == "change" || phase == "end")
        || ! isNumber (target) || (double) target < std::numeric_limits<int>::min()
        || (double) target > std::numeric_limits<int>::max()
        || (phase == "change" && (! isNumber (value) || std::abs ((double) value) > std::numeric_limits<float>::max())))
        return std::nullopt;

    return Edit { phase == "begin" ? Phase::begin : phase == "change" ? Phase::change : Phase::end,
                  edit["param"].toString(), (int) target, phase == "change" ? (float) value : 0.0f };
}

void dispatch (ComposerProcessor& self, const Edit& edit, const void* owner)
{
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
        {
            if (params::ofStage.contains (edit.paramId) || (params::ofPlayer.contains (edit.paramId) && p->id == edit.target))
            {
                switch (edit.phase)
                {
                    case Phase::begin:  p->beginGesture (edit.paramId, owner); break;
                    case Phase::change: p->writeParam (edit.paramId, edit.value); break;
                    case Phase::end:    p->endGesture (edit.paramId, owner); break;
                }
                p->markPlaced();
            }
        }
    });
}
} // namespace

void applyEdit (ComposerProcessor& self, const juce::var& edit)
{
    if (const auto validated = validateEdit (edit))
        dispatch (self, *validated, &self);
}

void OpenGestures::applyEdit (ComposerProcessor& self, const juce::var& edit)
{
    const auto validated = validateEdit (edit);
    if (! validated)
        return;

    // A stage parameter has the same recipients regardless of the target in the message.
    const Begun gesture { params::ofStage.contains (validated->paramId) ? 0 : validated->target, validated->paramId };
    const auto same = [&] (const Begun& other) { return other.target == gesture.target && other.paramId == gesture.paramId; };

    if (validated->phase == Phase::begin && std::none_of (begun.begin(), begun.end(), same))
        begun.push_back (gesture);
    else if (validated->phase == Phase::end)
        std::erase_if (begun, same);

    dispatch (self, *validated, this);
}

void OpenGestures::endAll (ComposerProcessor& self)
{
    for (const auto& gesture : std::exchange (begun, {}))
        dispatch (self, Edit { Phase::end, gesture.paramId, gesture.target, 0.0f }, this);
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

void setIcon (ComposerProcessor& self, int id, const juce::String& key)
{
    // The same keys as ui/src/icons.tsx (iconKeys); "" = automatic.
    static const juce::StringArray known { "", "violin", "cello", "flute", "reed", "horn", "trumpet", "timpani",
                                           "drum", "piano", "organ", "harp", "voice", "other" };

    if (! known.contains (key))
        return;

    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
            if (p->id == id)
                p->setIcon (key);
    });
}
} // namespace stage
