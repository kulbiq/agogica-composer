#pragma once

#include <juce_core/juce_core.h>
#include "../engines/StillGate.h"
#include "CameraFeed.h"
#include "TrackingReports.h"

namespace engines { class HeadPoseEstimator; }

namespace tracker
{
// Head tracking, on its own thread: takes the camera's latest frame, finds the head in it
// (HeadPoseEstimator, or the last observation again while the head is still) and passes its
// orientation on; reports what it did. Starts when constructed, stops when destroyed.
class TrackingLoop final : private juce::Thread
{
public:
    // Each frame's head (nothing = no head), for Composer. Called on the tracking thread.
    using HeadOutput = std::function<void (std::optional<HeadOrientation>)>;

    // Called on the tracking thread.
    struct Reports
    {
        std::function<void (const TrackerStatus&)> status;
        std::function<void (const FrameReport&)> frame;
        std::function<void (const PerformanceReport&)> performance;
        std::function<void (const juce::MemoryBlock& jpeg)> cameraPreview;
    };

    TrackingLoop (juce::File modelsFolder, CameraFeed&, HeadOutput, Reports);
    ~TrackingLoop() override;

    // While on: a small picture of the camera about ten times a second (Reports::cameraPreview).
    void setCameraPreviewEnabled (bool on) { cameraPreviewEnabled = on; }

private:
    void run() override;
    bool loadEstimator();
    void setStatus (TrackerStatus);
    FrameReport trackHead (const juce::Image& frame);
    void publishCameraPreviewIfDue (const juce::Image& frame);

    const juce::File modelsFolder;
    CameraFeed& camera;
    const HeadOutput headOutput;
    const Reports reports;
    std::atomic<bool> cameraPreviewEnabled { false };

    // Tracking thread only.
    std::unique_ptr<engines::HeadPoseEstimator> estimator;
    engines::StillGate stillHead;
    std::optional<HeadObservation> lastHead;
    std::optional<TrackerStatus> status;
    double lastPreviewSeconds = 0;
};
} // namespace tracker
