#pragma once

#include <juce_video/juce_video.h>

namespace tracker
{
// A camera, and its latest frame for whoever asks: frames arrive on the camera's own thread
// faster than they may be used, so a newer frame replaces one nobody took.
// A camera that gives no picture is not kept: after `noPictureTimeoutMs` it is opened again or,
// with several cameras, the next one is tried. (On macOS JUCE gets frames as a chain of still
// captures, and one failed capture ends the chain for good; a phone offered as a camera may
// never deliver at all.)
class CameraFeed final : private juce::CameraDevice::Listener,
                         private juce::Timer
{
public:
    CameraFeed(); // opens the first camera (message thread)
    ~CameraFeed() override;

    // Waits for a frame nobody has taken yet; an invalid image when none came in time. Any thread.
    juce::Image waitForFrame (int timeoutMs);

    // Every frame the cameras have delivered, taken or not. Any thread.
    int framesReceived() const { return received.load (std::memory_order_relaxed); }

    // The camera in use or being tried; empty when the computer has none. Any thread.
    juce::String cameraName() const;

private:
    void imageReceived (const juce::Image&) override;
    void timerCallback() override;
    void retryIfNoPicture();
    void open (int cameraIndex);
    void close();

    // Development: AGOGICA_TRACKER_STILL_FRAME=<picture file> stands in for the camera, 30 times
    // a second - the whole app can then be run and checked on a computer without a camera.
    const juce::Image developmentFrame;

    std::unique_ptr<juce::CameraDevice> camera;
    int cameraIndex = 0;
    int receivedAtLastLook = 0;
    std::atomic<int> received { 0 };

    juce::CriticalSection frameLock; // latestFrame and name
    juce::Image latestFrame;
    juce::String name;
    juce::WaitableEvent frameArrived;
};
} // namespace tracker
