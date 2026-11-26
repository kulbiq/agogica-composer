#include "StageEdits.h"

#include <algorithm>
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

ComposerProcessor* memberWithId (const juce::Array<ComposerProcessor*>& members, int id)
{
    for (auto* p : members)
        if (p->id == id)
            return p;

    return nullptr;
}

// Whether `p` is one of the tracks of the player that track `target` belongs to.
struct PlayerOf
{
    PlayerOf (const juce::Array<ComposerProcessor*>& members, int targetId) : target (targetId)
    {
        if (const auto* p = memberWithId (members, targetId))
            key = p->getPlayerKey();
    }

    bool operator() (const ComposerProcessor& p) const { return p.id == target || (key.isNotEmpty() && p.getPlayerKey() == key); }

    int target;
    juce::String key;
};

// A key left on one track only is a player of its own again.
void dropLoneKeys (const juce::Array<ComposerProcessor*>& members)
{
    for (auto* p : members)
        if (const auto key = p->getPlayerKey(); key.isNotEmpty()
            && std::count_if (members.begin(), members.end(), [&] (auto* q) { return q->getPlayerKey() == key; }) == 1)
            p->setPlayerKey ({});
}

void dispatch (ComposerProcessor& self, const Edit& edit, const void* owner)
{
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        const PlayerOf ofTarget (members, edit.target);

        for (auto* p : members)
        {
            if (params::ofStage.contains (edit.paramId) || (params::ofPlayer.contains (edit.paramId) && ofTarget (*p)))
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
        auto* target = memberWithId (members, id);
        const auto cut = cutName (name);

        if (target == nullptr)
            return;

        // A player of its own: its name is its track's.
        if (target->getPlayerKey().isEmpty())
        {
            target->setNameOverride (cut);
            target->setPlayerName ({});
            return;
        }

        const PlayerOf ofPlayer (members, id);

        for (auto* p : members)
            if (ofPlayer (*p))
                p->setPlayerName (cut);
    });
}

void setTrackName (ComposerProcessor& self, int id, const juce::String& name)
{
    self.registry->withMember (id, [&] (ComposerProcessor& p) { p.setNameOverride (cutName (name)); });
}

void setIcon (ComposerProcessor& self, int id, const juce::String& key)
{
    if (! isIcon (key))
        return;

    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        const PlayerOf ofPlayer (members, id);

        for (auto* p : members)
            if (ofPlayer (*p))
                p->setIcon (key);
    });
}

void setHeadTracking (ComposerProcessor& self, bool on)
{
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
            p->setHeadTrackingOn (on);
    });
}

void setAudience (ComposerProcessor& self, const juce::String& seats)
{
    if (! isAudience (seats))
        return;

    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
            p->setAudience (seats);
    });
}

void joinPlayer (ComposerProcessor& self, int trackId, int intoId)
{
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        auto* track = memberWithId (members, trackId);
        auto* into = memberWithId (members, intoId);

        if (track == nullptr || into == nullptr || track == into
            || (into->getPlayerKey().isNotEmpty() && track->getPlayerKey() == into->getPlayerKey()))
            return;

        // The track keeps the name it shows: as a player of its own, the player's name (which
        // differs from the track's after its player lost its other tracks).
        if (track->getPlayerKey().isEmpty() && track->getPlayerName().isNotEmpty())
            track->setNameOverride (track->getPlayerName());
        track->setPlayerName ({});

        if (into->getPlayerKey().isEmpty())
            into->setPlayerKey (juce::Uuid().toDashedString());

        track->endGestures (params::ofPlayer);
        track->setPlayerKey (into->getPlayerKey());
        track->copyScenePlaces (*into);

        for (const auto& paramId : params::ofPlayer)
            track->writeParam (paramId, into->getParam (paramId));

        // The player keeps the name and icon it had, written to every track so they no longer
        // depend on which of them is first.
        const PlayerOf ofPlayer (members, intoId);
        const auto name = into->getPlayerLabel();
        const auto icon = into->getIcon();

        for (auto* p : members)
        {
            if (ofPlayer (*p))
            {
                p->setPlayerName (name);
                p->setIcon (icon);
            }
        }

        track->markPlaced();
        dropLoneKeys (members);
    });
}

void splitPlayer (ComposerProcessor& self, int trackId)
{
    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        auto* track = memberWithId (members, trackId);

        if (track == nullptr || track->getPlayerKey().isEmpty())
            return;

        track->endGestures (params::ofPlayer);
        track->setPlayerKey ({});
        track->setPlayerName ({}); // named after its track again, with its own icon
        track->setIcon ({});
        dropLoneKeys (members);
    });
}
} // namespace stage
