#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include "../HeadObservation.h"
#include "ListenerPose.h"

// What the tracking thread tells the rest of the app (the page shows it; the development log
// records it). Plain values: nothing here knows about JSON or the page.
namespace tracker
{
enum class TrackerPhase
{
    loadingModels,
    waitingForCamera, // no picture yet: the camera is starting, or the computer has none
    noPicture,        // no camera gave a picture and none is tried any more: the Camera menu is the way on
    compilingModels,  // the first frame is being processed (CoreML compiles on its first run)
    tracking,
    failed
};

struct TrackerStatus
{
    TrackerPhase phase = TrackerPhase::loadingModels;
    // failed: the reason; compilingModels and tracking: what runs the models ("CoreML", "CPU");
    // waitingForCamera, noPicture: the camera's name, empty when the computer has none.
    juce::String detail;

    bool operator== (const TrackerStatus& other) const { return phase == other.phase && detail == other.detail; }
};

// One camera frame the tracking thread got to.
struct FrameReport
{
    std::optional<HeadObservation> head;  // as the camera sees it; nothing = no head in the frame
    ListenerPose::Reading heard;          // what Composer hears after this frame
    bool reusedStillHead = false;         // the head hadn't moved: the last observation again, the models didn't run
    float changeSinceLastEstimate = 0;    // StillHeadGate's measure for this frame
    juce::String warning;                 // the models failed on this frame
};

// Once a second: where the camera's frames and the processor went.
struct PerformanceReport
{
    int cameraWidth = 0, cameraHeight = 0;
    double cameraFramesPerSecond = 0;
    double cpuCores = 0;                  // the whole process, 1.0 = one core busy
    double estimatedPerSecond = 0;        // frames the models ran on
    double reusedPerSecond = 0;           // frames a still head reused
    double droppedPerSecond = 0;          // camera frames the tracking thread never got to
};
// Whoever shows the reports (the page). Called on the tracking thread.
class TrackingListener
{
public:
    virtual ~TrackingListener() = default;
    virtual void report (const TrackerStatus&) = 0;
    virtual void report (const FrameReport&) = 0;
    virtual void report (const PerformanceReport&) = 0;

    // While true: a small JPEG of the camera's picture, about ten times a second.
    virtual bool wantsCameraPreview() const = 0;
    virtual void reportCameraPreview (const juce::MemoryBlock& jpeg) = 0;
};
} // namespace tracker
