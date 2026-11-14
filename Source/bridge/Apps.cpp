#include "Apps.h"

namespace apps
{
juce::File windowsExecutable ([[maybe_unused]] const juce::String& bundleId, [[maybe_unused]] const juce::String& registryRoot)
{
   #if JUCE_WINDOWS
    if (bundleId != "com.agogica.hub")
        return {};

    const auto folder = juce::WindowsRegistry::getValue (registryRoot + "Agogica Hub\\").unquoted();
    if (! juce::File::isAbsolutePath (folder))
        return {};

    const auto exe = juce::File (folder).getChildFile ("agogica-hub.exe");
    return exe.existsAsFile() ? exe : juce::File {};
   #else
    return {};
   #endif
}
} // namespace apps
