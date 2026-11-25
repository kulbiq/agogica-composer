#pragma once

#include <juce_graphics/juce_graphics.h>

namespace tracker
{
// One open camera delivering video frames. Two implementations, chosen by the build:
// - macOS: CameraSource_mac.mm, AVFoundation's video output. Not JUCE's camera there: it takes
//   a still photo per frame (compressed, then decoded again), which halves the frame rate and
//   stops for good when one photo fails.
// - elsewhere: CameraSource_juce.cpp, juce::CameraDevice (on Windows it reads real video frames).
class CameraSource
{
public:
    // Called with each frame on the camera's own thread, never after the source is destroyed.
    using FrameCallback = std::function<void (const juce::Image&)>;

    // The computer's cameras, by name, in the system's order.
    static juce::StringArray availableCameras();

    // Opens camera `index` of `availableCameras()` and starts it; nothing when it can't be opened.
    // Message thread.
    static std::unique_ptr<CameraSource> open (int index, FrameCallback);

    virtual ~CameraSource() = default;
};
} // namespace tracker
