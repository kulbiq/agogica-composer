#pragma once

#include <juce_core/juce_core.h>

// Where Agogica's own apps are installed, for the editor's "Open Agogica Hub".
namespace apps
{
// Windows: Agogica Hub's installer (Tauri 2, NSIS, per user; agogica-platform desktop/src-tauri)
// stores its folder as the default value of HKCU\Software\<publisher>\<product name>, publisher
// "agogica" (from the identifier com.agogica.hub), product "Agogica Hub"; the exe is the Cargo
// binary, agogica-hub.exe. An empty File when the app isn't installed, for any other app (the
// Tracker has no Windows installer yet, Hub plan 7b), and off Windows (macOS: `open -b`).
juce::File windowsExecutable (const juce::String& bundleId,
                              const juce::String& registryRoot = "HKEY_CURRENT_USER\\Software\\agogica\\");
} // namespace apps
