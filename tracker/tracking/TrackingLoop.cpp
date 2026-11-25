#include "TrackingLoop.h"
#include "../../Source/stage/HeadTrackingFile.h"
#include "../vision/HeadPoseEstimator.h"
#include "ProcessCpu.h"

namespace tracker
{
namespace
{
constexpr int frameWaitMs = 200;            // how often the loop looks whether it should stop
constexpr double cameraSilentAfterSeconds = headtracking::staleAfterMs / 1000.0; // no frame for this long: no head
constexpr double previewIntervalSeconds = 0.1;
constexpr int previewWidthPixels = 480;     // sharp at 2x in the compact window's ~320 px
constexpr float previewJpegQuality = 0.7f;
constexpr double performanceIntervalSeconds = 1.0;

double secondsNow() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

// Development monitoring: the camera's frame size and rate, the whole process's CPU, and where
// the camera's frames went (estimated, reused for a still head, or never looked at).
class PerformanceMeter
{
public:
    explicit PerformanceMeter (int cameraFramesSoFar) { restart (cameraFramesSoFar); }

    void countFrame (bool reusedStillHead) { ++(reusedStillHead ? reused : estimated); }

    // A report once `performanceIntervalSeconds` have passed since the last one.
    std::optional<PerformanceReport> measureIfDue (const juce::Image& frame, int cameraFramesSoFar)
    {
        const auto seconds = secondsNow() - startedSeconds;
        if (seconds < performanceIntervalSeconds)
            return std::nullopt;

        const auto cameraFrames = cameraFramesSoFar - cameraFramesAtStart;
        PerformanceReport report;
        report.cameraWidth = frame.getWidth();
        report.cameraHeight = frame.getHeight();
        report.cameraFramesPerSecond = cameraFrames / seconds;
        report.cpuCores = (processCpuSeconds() - cpuSecondsAtStart) / seconds;
        report.estimatedPerSecond = estimated / seconds;
        report.reusedPerSecond = reused / seconds;
        report.droppedPerSecond = juce::jmax (0, cameraFrames - estimated - reused) / seconds;

        restart (cameraFramesSoFar);
        return report;
    }

private:
    void restart (int cameraFramesSoFar)
    {
        startedSeconds = secondsNow();
        cpuSecondsAtStart = processCpuSeconds();
        cameraFramesAtStart = cameraFramesSoFar;
        estimated = reused = 0;
    }

    double startedSeconds = 0, cpuSecondsAtStart = 0;
    int cameraFramesAtStart = 0, estimated = 0, reused = 0;
};
} // namespace

TrackingLoop::TrackingLoop (juce::File models, CameraFeed& feed, HeadOutput output, Reports r)
    : juce::Thread ("head tracking"), modelsFolder (std::move (models)), camera (feed),
      headOutput (std::move (output)), reports (std::move (r))
{
    startThread();
}

TrackingLoop::~TrackingLoop()
{
    // Waits for the frame in hand or for the models to finish loading, however long (the first
    // load on a Mac took 7.6 s): a thread killed inside ONNX Runtime can take the app with it.
    const int waitForever = -1;
    stopThread (waitForever);
}

void TrackingLoop::run()
{
    if (! loadEstimator())
        return;

    PerformanceMeter performance (camera.framesReceived());
    bool isFirstFrame = true;
    double lastFrameSeconds = 0;

    while (! threadShouldExit())
    {
        const auto frame = camera.waitForFrame (frameWaitMs);
        if (! frame.isValid())
        {
            if (secondsNow() - lastFrameSeconds > cameraSilentAfterSeconds)
                reportCameraSilent();
            continue;
        }
        lastFrameSeconds = secondsNow();

        if (isFirstFrame)
            setStatus ({ TrackerPhase::compilingModels, estimator->provider() });

        const auto report = trackHead (frame);
        publishCameraPreviewIfDue (frame); // after the pose is out: Composer doesn't wait for a JPEG

        isFirstFrame = false;
        setStatus ({ TrackerPhase::tracking, estimator->provider() });
        reports.frame (report);

        if (report.warning.isEmpty())
            performance.countFrame (report.reusedStillHead);
        if (const auto measured = performance.measureIfDue (frame, camera.framesReceived()))
            reports.performance (*measured);
    }
}

// Without frames there is no head: Composer and the page are told once, not left with the last one.
void TrackingLoop::reportCameraSilent()
{
    const TrackerStatus waiting { TrackerPhase::waitingForCamera, camera.cameraName() };
    if (status.has_value() && status->phase == TrackerPhase::tracking)
    {
        lastHead.reset();
        stillHead.clear();
        FrameReport noFrame;
        noFrame.heard = headOutput (std::nullopt);
        reports.frame (noFrame);
    }
    setStatus (waiting);
}

// Reported when it changes.
void TrackingLoop::setStatus (TrackerStatus newStatus)
{
    if (status == newStatus)
        return;
    status = newStatus;
    reports.status (newStatus);
}

bool TrackingLoop::loadEstimator()
{
    setStatus ({ TrackerPhase::loadingModels, {} });
    try
    {
        estimator = std::make_unique<HeadPoseEstimator> (modelsFolder);
        return true;
    }
    catch (const std::exception& e)
    {
        setStatus ({ TrackerPhase::failed, e.what() });
        return false;
    }
}

// The head in `frame`, passed on to Composer as it is: no filter, no smoothing.
FrameReport TrackingLoop::trackHead (const juce::Image& frame)
{
    FrameReport report;
    try
    {
        // A still head repeats the last observation instead of running the models.
        const auto now = secondsNow();
        report.reusedStillHead = lastHead.has_value() && stillHead.unchanged (frame, now);
        if (! report.reusedStillHead)
        {
            lastHead = estimator->estimate (frame);
            if (lastHead)
                stillHead.reset (frame, lastHead->headBoxNormalised, now);
            else
                stillHead.clear();
        }

        report.head = lastHead;
        report.changeSinceLastEstimate = stillHead.difference;
    }
    catch (const std::exception& e)
    {
        // One bad frame is "no head this frame", not the end of tracking.
        lastHead.reset();
        stillHead.clear();
        report = {};
        report.warning = e.what();
    }

    report.heard = headOutput (report.head ? std::optional (report.head->orientation) : std::nullopt);
    return report;
}

// The page can't open the camera itself (Windows allows one reader), so it gets small JPEGs.
void TrackingLoop::publishCameraPreviewIfDue (const juce::Image& frame)
{
    const auto now = secondsNow();
    if (! cameraPreviewEnabled || now - lastPreviewSeconds < previewIntervalSeconds || frame.getWidth() <= 0)
        return;
    lastPreviewSeconds = now;

    const auto height = juce::jmax (1, previewWidthPixels * frame.getHeight() / frame.getWidth());
    const auto small = frame.rescaled (previewWidthPixels, height, juce::Graphics::mediumResamplingQuality);

    juce::MemoryOutputStream jpeg;
    juce::JPEGImageFormat format;
    format.setQuality (previewJpegQuality);
    if (format.writeImageToStream (small, jpeg))
        reports.cameraPreview (jpeg.getMemoryBlock());
}
} // namespace tracker
