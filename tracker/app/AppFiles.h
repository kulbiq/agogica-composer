#pragma once

#include <juce_core/juce_core.h>

namespace tracker
{
// What the build puts next to the app: the page (ui/) and the models (models/).
inline juce::File appResourcesFolder()
{
    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
   #if JUCE_MAC
    return app.getChildFile ("Contents/Resources");
   #else
    return app.getParentDirectory();
   #endif
}
} // namespace tracker
