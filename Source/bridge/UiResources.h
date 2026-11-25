#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace web
{
// A file of the page, from the zip built into the plugin ("/" is index.html); nothing for a
// path that isn't in it. No network and no local server.
std::optional<juce::WebBrowserComponent::Resource> uiResource (const juce::String& url);
} // namespace web
