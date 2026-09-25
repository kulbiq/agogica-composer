#pragma once

#include <juce_video/juce_video.h>
#include "engines/Engines.h"

// Runs the native head-pose engines: the camera (juce::CameraDevice, frames on the message
// thread) feeds the latest frame to a worker thread that runs the active engine; results go
// to the page as "estimate" events. The page smooths them and writes the shared file, the
// same way for every engine. "mediapipe" means: no native engine (the page does it all).
class EngineHost final : private juce::CameraDevice::Listener,
                         private juce::Thread,
                         private juce::AsyncUpdater
{
public:
    // `emit` is called on the message thread with each event payload.
    EngineHost (juce::File modelsDir, std::function<void (const juce::var&)> emit);
    ~EngineHost() override;

    void setEngine (const juce::String& name); // message thread

private:
    void imageReceived (const juce::Image&) override;
    void run() override;
    void handleAsyncUpdate() override;
    void post (juce::DynamicObject::Ptr);

    const juce::File modelsDir;
    const std::function<void (const juce::var&)> emit;
    juce::String engineName;
    std::unique_ptr<juce::CameraDevice> camera;

    juce::CriticalSection frameLock;
    juce::Image latestFrame;
    juce::WaitableEvent frameReady;

    juce::CriticalSection postLock;
    juce::DynamicObject::Ptr pending;
};
