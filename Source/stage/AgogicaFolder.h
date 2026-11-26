#pragma once

#include <juce_core/juce_core.h>

// Where Agogica's programs keep what belongs to this user on this computer (the head-tracking
// file, Composer's answer about usage data): ~/Library/Application Support/Agogica,
// %LOCALAPPDATA%\Agogica. Local, not roaming: none of it means anything on another machine.
inline juce::File agogicaFolder()
{
   #if JUCE_WINDOWS
    const auto base = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
   #else
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support");
   #endif
    return base.getChildFile ("Agogica");
}
