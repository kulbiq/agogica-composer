#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

namespace tracker
{
// What the app remembers between runs: ~/Library/Application Support/Agogica/Tracker.settings
// (Windows: %APPDATA%\Agogica). The only place that knows the file's keys.
class TrackerSettings
{
public:
    TrackerSettings() : file (options()) {}

    static juce::File folder() { return options().getDefaultFile().getParentDirectory(); }

    bool inverted() const                 { return file.getBoolValue ("invert", false); }
    void setInverted (bool on)            { file.setValue ("invert", on); }

    bool debugView() const                { return file.getBoolValue ("debug", false); }
    void setDebugView (bool on)           { file.setValue ("debug", on); }

    // JUCE's window state string (position, size); empty until the window has been shown once.
    juce::String windowState() const      { return file.getValue ("windowState"); }
    void setWindowState (const juce::String& state) { file.setValue ("windowState", state); }

    // The debugging view's own size; empty until it has been shown once.
    juce::Rectangle<int> debugViewBounds() const { return juce::Rectangle<int>::fromString (file.getValue ("debugBounds")); }
    void setDebugViewBounds (juce::Rectangle<int> bounds) { file.setValue ("debugBounds", bounds.toString()); }

private:
    static juce::PropertiesFile::Options options()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Tracker";
        o.folderName = "Agogica";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    juce::PropertiesFile file;
};
} // namespace tracker
