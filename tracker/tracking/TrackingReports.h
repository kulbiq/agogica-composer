#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include "../HeadObservation.h"

// What the tracking thread tells the rest of the app (the page shows it; the development log
// records it). Plain values: nothing here knows about JSON or the page.
namespace tracker
{
enum class TrackerPhase
{
    loadingModels,
    waitingForCamera, // no picture (yet, or any more): the camera is starting, missing or silent
    compilingModels,  // the first frame is being processed (CoreML compiles on its first run)
    tracking,
    failed
};

struct TrackerStatus
{
    TrackerPhase phase = TrackerPhase::loadingModels;
    // failed: the reason; compilingModels and tracking: what runs the models ("CoreML", "CPU");
    // waitingForCamera: the camera's name, empty when the computer has none.
    juce::String detail;

    bool operator== (const TrackerStatus& other) const { return phase == other.phase && detail == other.detail; }
};

// One camera frame the tracking thread got to.
struct FrameReport
{
    std::optional<HeadObservation> head;  // nothing = no head in the frame
    bool reusedStillHead = false;         // the head hadn't moved: the last observation again, the models didn't run
    float changeSinceLastEstimate = 0;    // StillGate's measure for this frame
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
} // namespace tracker
