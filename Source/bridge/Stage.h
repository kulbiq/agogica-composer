#pragma once

#include "../PluginProcessor.h"

// C++ side of the stage map: what the web UI draws and how its edits land on parameters.
// Works across all instances in the process through the StageRegistry.
namespace stage
{
// Everything the map needs, as a JSON-able var. Positions are clamped to the room;
// derived values (distance, azimuth, pan, delay) come from dsp/Geometry.h.
juce::var snapshot (ComposerProcessor& self);

// One step of a user gesture from the UI: { phase: begin|change|end, target, param, value }.
// playerX/Y land on the target instance; roomSize/listenerX/Y/outputMode/Pro on every instance.
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

// Renames player `id` (empty = back to the host track name). Longer names are cut: the name
// is saved with the project and sent to every window.
constexpr int maxNameLength = 64;
void setName (ComposerProcessor& self, int id, const juce::String& name);

// Switches every instance to scene "A".."H": each stores its current values in the
// scene it leaves and recalls the new one. A scene an instance never had starts as a
// copy of its current values. Written as gestures, like a drag.
void setScene (ComposerProcessor& self, const juce::String& scene);

// Copies the stage's active scene and room/listener into self and moves
// self's player to a free seat (see freeSeat). Marks self placed.
void join (ComposerProcessor& self);

// Where a new player sits, given the (clamped) positions already taken: rows of 7 in front of
// the listener, centre-out, spaced to fit the room (2 m in a 20 m room, down to 0.75 m), at
// least 0.5 m from the walls; when the front is full, wider rows, then rows nearer the
// listener, then beside and behind. A full room gets the point farthest from everyone.
geometry::Point freeSeat (geometry::Point listener, float roomSize, const std::vector<geometry::Point>& taken);
} // namespace stage
