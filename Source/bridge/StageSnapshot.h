#pragma once

#include "../PluginProcessor.h"

namespace stage
{
// Everything the page draws, as a JSON-able var: positions clamped to the room, names, scenes,
// and the ranges and defaults of the settings it has controls for. No acoustic values: the
// page does no audio maths and shows none.
juce::var snapshot (ComposerProcessor& self);
} // namespace stage
