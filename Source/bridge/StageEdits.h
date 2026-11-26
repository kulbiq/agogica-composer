#pragma once

#include "../PluginProcessor.h"

// How the page's edits land on parameters, across all instances in the process.
namespace stage
{
// One step of a user gesture from the UI: { phase: begin|change|end, target, param, value }.
// playerX/Y land on the target's player (every track of it); roomSize/listenerX/Y/outputMode/Pro on every instance.
// Unknown params or targets are ignored.
void applyEdit (ComposerProcessor& self, const juce::var& edit);

// The gestures one editor window has begun and not ended. A window that closes (or whose
// page reloads) in the middle of a drag can't send the end itself: endAll does.
class OpenGestures
{
public:
    void applyEdit (ComposerProcessor& self, const juce::var& edit);
    void endAll (ComposerProcessor& self);

private:
    struct Begun
    {
        int target;
        juce::String paramId;
    };
    std::vector<Begun> begun;
};

// Renames the player of track `id`. A player of its own is its track, so that renames the
// track (empty = back to the host track name); a player with several tracks gets a name of
// its own (empty = its first track's). setTrackName renames one track. Longer names are cut:
// names are saved with the project and sent to every window.
constexpr int maxNameLength = 64;
void setName (ComposerProcessor& self, int id, const juce::String& name);
void setTrackName (ComposerProcessor& self, int id, const juce::String& name);

// Sets player `id`'s icon: one of the keys in ui/src/icons.tsx, or empty for automatic.
// Anything else is ignored.
void setIcon (ComposerProcessor& self, int id, const juce::String& key);

// Players with several tracks. A player's tracks share a key (ComposerProcessor::getPlayerKey):
// position edits (with their gestures), names and icons sent to any of them go to all of them.
// joinPlayer: track `trackId` becomes one of the tracks of `intoId`'s player, taking its place
// (in every scene) and icon; the track keeps the name it showed, the player keeps its own.
// splitPlayer: the track becomes a player of its own again, where it stands, named after the
// track, with its own icon.
void joinPlayer (ComposerProcessor& self, int trackId, int intoId);
void splitPlayer (ComposerProcessor& self, int trackId);
} // namespace stage
