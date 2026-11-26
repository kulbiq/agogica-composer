#pragma once

#include <cmath>
#include <juce_data_structures/juce_data_structures.h>

// What the saved state may hold besides parameters: one set of rules for both ways in - the
// page's edits (bridge/StageEdits, stage/Scenes) and a project file (setStateInformation).
namespace stage
{
// Names are saved with the project and sent to every window: longer ones are cut.
constexpr int maxNameLength = 64;
inline juce::String cutName (const juce::String& name) { return name.trim().substring (0, maxNameLength); }

inline bool isScene (const juce::String& scene)
{
    return scene.length() == 1 && juce::String ("ABCDEFGH").contains (scene);
}

// The same keys as ui/src/icons.tsx (iconKeys); "" = picked from the name by the page.
inline bool isIcon (const juce::String& key)
{
    static const juce::StringArray known { "", "violin", "cello", "flute", "reed", "horn", "trumpet", "timpani",
                                           "drum", "piano", "organ", "harp", "voice", "other" };
    return known.contains (key);
}

// ui/src/audience.ts: 65 x 65 seats, one bit each, 529 bytes in base64 (audience.test.ts checks
// this number against the page); empty = nobody.
inline bool isAudience (const juce::String& seats)
{
    constexpr int audienceLength = 708;
    return seats.isEmpty()
        || (seats.length() == audienceLength
            && seats.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/="));
}

// A project file is somebody else's bytes: afterwards the tree holds only what the page could
// have written. What breaks a rule goes back to its default; everything else is kept as it is.
inline void sanitise (juce::ValueTree& tree)
{
    const auto notANumber = [] (const juce::var& v) { return ! std::isfinite ((double) v); };

    if (! isScene (tree.getProperty ("scene", "A").toString()))
        tree.removeProperty ("scene", nullptr);

    for (const auto* name : { "name", "playerName" })
        if (tree.hasProperty (name))
            tree.setProperty (name, cutName (tree[name].toString()), nullptr);

    // A player's key is a UUID (36 characters); a longer one is no key.
    if (tree["player"].toString().length() > maxNameLength)
        tree.removeProperty ("player", nullptr);

    if (! isIcon (tree["icon"].toString()))
        tree.removeProperty ("icon", nullptr);

    if (! isAudience (tree["audience"].toString()))
        tree.removeProperty ("audience", nullptr);

    // A parameter that isn't a number keeps the value it has (a NaN would go all the way to
    // the sound); the host's ranges then clamp the rest.
    for (int i = tree.getNumChildren(); --i >= 0;)
        if (auto child = tree.getChild (i); child.hasType ("PARAM") && notANumber (child["value"]))
            tree.removeChild (i, nullptr);

    auto scenes = tree.getChildWithName ("scenes");

    for (int i = scenes.getNumChildren(); --i >= 0;)
    {
        auto slot = scenes.getChild (i);

        if (! isScene (slot.getType().toString()))
        {
            scenes.removeChild (i, nullptr);
            continue;
        }

        for (int property = slot.getNumProperties(); --property >= 0;)
            if (const auto name = slot.getPropertyName (property); notANumber (slot[name]))
                slot.removeProperty (name, nullptr); // recallScene keeps the current value
    }
}
} // namespace stage
