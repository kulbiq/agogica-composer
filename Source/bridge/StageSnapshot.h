#pragma once

#include "../PluginProcessor.h"

namespace stage
{
// Everything the map needs, as a JSON-able var. Positions are clamped to the room;
// derived values (distance, azimuth, pan, delay) come from dsp/Geometry.h.
juce::var snapshot (ComposerProcessor& self);
} // namespace stage
