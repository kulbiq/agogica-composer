#pragma once

#include <juce_core/juce_core.h>
#include <cstdlib>

// Points the head tracking reader at `file` while in scope (read when HeadTracking is created).
struct TrackingFileOverride
{
    explicit TrackingFileOverride (const juce::File& file)
    {
       #if JUCE_WINDOWS
        _putenv_s ("AGOGICA_HEAD_TRACKING_FILE", file.getFullPathName().toRawUTF8());
       #else
        setenv ("AGOGICA_HEAD_TRACKING_FILE", file.getFullPathName().toRawUTF8(), 1);
       #endif
    }

    ~TrackingFileOverride()
    {
       #if JUCE_WINDOWS
        _putenv_s ("AGOGICA_HEAD_TRACKING_FILE", "");
       #else
        unsetenv ("AGOGICA_HEAD_TRACKING_FILE");
       #endif
    }
};
