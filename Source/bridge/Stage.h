#pragma once

#include "../PluginProcessor.h"

// C++ side of the stage map: what the web UI draws and how its edits land on parameters.
namespace stage
{
// Everything the map needs, as a JSON-able var. Positions are clamped to the room;
// derived values (distance, azimuth, pan, delay) come from dsp/Geometry.h.
juce::var snapshot (ComposerProcessor& self);

// One step of a user gesture from the UI: { phase: begin|change|end, target, param, value }.
// Unknown params or targets are ignored.
void applyEdit (ComposerProcessor& self, const juce::var& edit);
} // namespace stage
