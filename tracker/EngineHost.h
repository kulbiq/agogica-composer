#pragma once

#include <juce_video/juce_video.h>
#include "engines/Engines.h"

// Runs the native head-pose engines: the camera (juce::CameraDevice, frames on the message
// thread) feeds the latest frame to a worker thread that runs the active engine; each frame's
// filtered heading goes straight to `output` (PoseOutput, for Composer) and the details to the
// page as "estimate" events (status, readouts, plot). "mediapipe" means: no native engine.
class EngineHost final : private juce::CameraDevice::Listener,
                         private juce::Thread,
                         private juce::AsyncUpdater
{
public:
    // `emit` is called on the message thread with each event (name, payload): "estimate" per
    // camera frame, "frame" (a JPEG data URL) for the preview; `output` on the engine thread
    // with each frame's filtered heading (found, yaw, pitch, roll), for Composer.
    using Emit = std::function<void (const juce::Identifier& event, const juce::var& payload)>;
    using Output = std::function<void (bool found, float yaw, float pitch, float roll)>;
    EngineHost (juce::File modelsDir, Emit emit, Output output);
    ~EngineHost() override;

    void setEngine (const juce::String& name); // message thread

    // The page's preview: while on, ~10 small frames a second. Any thread.
    void setPreview (bool on) { previewWanted = on; }

    // Pins a filter parameter to `value`, or back to automatic (void var). Message thread.
    void setFilterParam (const juce::String& name, const juce::var& value);

private:
    void imageReceived (const juce::Image&) override;
    void run() override;
    void handleAsyncUpdate() override;
    void post (juce::DynamicObject::Ptr);
    void postPreview (const juce::Image&);

    const juce::File modelsDir;
    const Emit emit;
    const Output output;
    std::atomic<bool> previewWanted { false };
    std::atomic<int> framesReceived { 0 }; // from the camera (monitoring)
    double lastPreview = 0; // engine thread
    juce::String engineName;
    std::unique_ptr<juce::CameraDevice> camera;

    juce::CriticalSection frameLock;
    juce::Image latestFrame;
    juce::WaitableEvent frameReady;

    juce::CriticalSection postLock;
    juce::DynamicObject::Ptr pending; // the latest of each wins
    juce::String pendingPreview;

    juce::CriticalSection pinsLock;
    std::array<std::optional<float>, 7> pins; // engines::Param order
};
