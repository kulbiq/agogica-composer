#pragma once

#include "../PluginProcessor.h"

// How the page's edits land on parameters, across all instances in the process.
namespace stage
{
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

// Sets player `id`'s icon: one of the keys in ui/src/icons.tsx, or empty for automatic.
// Anything else is ignored.
void setIcon (ComposerProcessor& self, int id, const juce::String& key);
} // namespace stage
