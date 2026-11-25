#pragma once

#include <juce_core/juce_core.h>
#include "../vision/StillHeadGate.h"
#include "CameraFeed.h"
#include "ListenerPoseOutput.h"
#include "TrackingReports.h"

namespace tracker
{
class HeadPoseEstimator;

// Head tracking, on its own thread: takes the camera's latest frame, finds the head in it
// (HeadPoseEstimator, or the last observation again while the head is still), passes its
// orientation on to Composer (ListenerPoseOutput) and reports what it did. Starts when constructed, stops when destroyed.
class TrackingLoop final : private juce::Thread
{
public:
    TrackingLoop (juce::File modelsFolder, CameraFeed&, ListenerPoseOutput&, TrackingListener&);
    ~TrackingLoop() override;

private:
    void run() override;
    bool loadEstimator();
    void setStatus (TrackerStatus);
    void reportCameraSilent();
    FrameReport trackHead (const juce::Image& frame);
    void publishCameraPreviewIfDue (const juce::Image& frame);

    const juce::File modelsFolder;
    CameraFeed& camera;
    ListenerPoseOutput& poseOutput;
    TrackingListener& listener;

    // Tracking thread only.
    std::unique_ptr<HeadPoseEstimator> estimator;
    StillHeadGate stillHead;
    std::optional<HeadObservation> lastHead;
    std::optional<TrackerStatus> status;
    double lastPreviewSeconds = 0;
};
} // namespace tracker
