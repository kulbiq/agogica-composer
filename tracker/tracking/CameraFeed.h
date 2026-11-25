#pragma once

#include <juce_video/juce_video.h>

namespace tracker
{
// The camera, and its latest frame for whoever asks: frames arrive on the camera's own thread
// faster than they may be used, so a newer frame replaces one nobody took.
class CameraFeed final : private juce::CameraDevice::Listener
{
public:
    CameraFeed(); // opens the camera (message thread)
    ~CameraFeed() override;

    bool isOpen() const { return camera != nullptr; }

    // Waits for a frame nobody has taken yet; an invalid image when none came in time. Any thread.
    juce::Image waitForFrame (int timeoutMs);

    // Every frame the camera has delivered, taken or not.
    int framesReceived() const { return received.load (std::memory_order_relaxed); }

private:
    void imageReceived (const juce::Image&) override;

    std::unique_ptr<juce::CameraDevice> camera;
    std::atomic<int> received { 0 };
    juce::CriticalSection frameLock;
    juce::Image latestFrame;
    juce::WaitableEvent frameArrived;
};
} // namespace tracker
