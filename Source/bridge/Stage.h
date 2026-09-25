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
// playerX/Y land on the target instance; roomSize/listenerX/Y on every instance.
// Unknown params or targets are ignored.
void applyEdit (ComposerProcessor& self, const juce::var& edit);

// Renames player `id` (empty = back to the host track name).
void setName (ComposerProcessor& self, int id, const juce::String& name);

// Switches every instance to scene "A".."H": each stores its current values in the
// scene it leaves and recalls the new one. A scene an instance never had starts as a
// copy of its current values. Written as gestures, like a drag.
void setScene (ComposerProcessor& self, const juce::String& scene);

// Copies the stage's active scene and room/listener into self and, if `takeSeat`, moves
// self's player to a free seat in front of the listener. Marks self placed.
void join (ComposerProcessor& self, bool takeSeat);
} // namespace stage
