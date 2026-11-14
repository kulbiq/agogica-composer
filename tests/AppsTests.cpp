#include "../Source/bridge/Apps.h"
#include <catch2/catch_test_macros.hpp>

#if JUCE_WINDOWS
TEST_CASE ("Agogica Hub is found where its installer records it (Windows)", "[integration]")
{
    // A key of our own, never the real HKCU\Software\agogica (this machine may have the Hub).
    const juce::String root { "HKEY_CURRENT_USER\\Software\\Agogica-ComposerTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()) + "\\" };
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("composer-tests").getChildFile ("Agogica Hub");
    folder.deleteRecursively();
    REQUIRE (folder.createDirectory());

    SECTION ("not installed: no key")
    {
        CHECK (apps::windowsExecutable ("com.agogica.hub", root) == juce::File {});
    }

    SECTION ("installed: the folder in the key's default value, the Cargo binary in it")
    {
        const auto exe = folder.getChildFile ("agogica-hub.exe");
        REQUIRE (exe.replaceWithText ("not a real exe"));
        REQUIRE (juce::WindowsRegistry::setValue (root + "Agogica Hub\\", folder.getFullPathName()));

        CHECK (apps::windowsExecutable ("com.agogica.hub", root) == exe);
        // Only the Hub has a Windows installer; the Tracker comes with Hub plan 7b.
        CHECK (apps::windowsExecutable ("com.agogica.tracker", root) == juce::File {});
    }

    SECTION ("removed by hand: a key left behind with no exe")
    {
        REQUIRE (juce::WindowsRegistry::setValue (root + "Agogica Hub\\", folder.getFullPathName()));

        CHECK (apps::windowsExecutable ("com.agogica.hub", root) == juce::File {});
    }

    juce::WindowsRegistry::deleteKey (root.dropLastCharacters (1)); // recursive
    folder.deleteRecursively();
}
#endif
