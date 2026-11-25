#pragma once

#include "../PluginProcessor.h"

namespace stage
{
// Switches every instance to scene "A".."H": each stores its current values in the
// scene it leaves and recalls the new one. A scene an instance never had starts as a
// copy of its current values. Written as gestures, like a drag.
void setScene (ComposerProcessor& self, const juce::String& scene);
} // namespace stage
