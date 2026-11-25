#pragma once

#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace tracker
{
struct CameraDeviceInfo
{
    juce::String id;
    juce::String name;
};

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

    // The computer's cameras in the system's order. Names are for display.
    static std::vector<CameraDeviceInfo> availableCameras();

    // Opens the camera with that ID, or returns null if it is unavailable. Message thread.
    static std::unique_ptr<CameraSource> open (const juce::String& deviceId, FrameCallback);

    virtual ~CameraSource() = default;
};
} // namespace tracker
