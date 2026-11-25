#include "CameraFeed.h"

namespace tracker
{
namespace
{
struct SizeRange { int minWidth, minHeight, maxWidth, maxHeight; };

// VGA first: the models need no more (the detector sees 320x192, the pose model a 128 px head),
// and webcams send uncompressed frames, so larger sizes are what USB 2 caps (the integrated
// camera of an i5-10210U laptop: 1280x720 at 10 fps, 640x480 at 30 in good light, 10 in a dim
// room) and cost Windows more to convert (0.2 cores at 1280x720).
// On Windows JUCE takes the largest size in the range.
constexpr SizeRange preferredSizes[] { { 640, 480, 640, 480 }, { 640, 360, 640, 360 }, { 320, 240, 1280, 720 } };

// Long enough for a camera to start (a phone connects first), short enough not to feel dead.
constexpr int noPictureTimeoutMs = 4000;
constexpr int developmentFramesPerSecond = 30;
} // namespace

CameraFeed::CameraFeed()
    : developmentFrame (juce::ImageFileFormat::loadFrom (juce::File (juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_STILL_FRAME", {}))))
{
    if (developmentFrame.isValid())
    {
        name = "Still frame (development)";
        startTimerHz (developmentFramesPerSecond);
        return;
    }

    open (0);
    startTimer (noPictureTimeoutMs);
}

CameraFeed::~CameraFeed()
{
    stopTimer();
    close();
}

void CameraFeed::open (int index)
{
    close();

    const auto cameras = juce::CameraDevice::getAvailableDevices();
    if (cameras.isEmpty())
        return;

    cameraIndex = index % cameras.size();
    for (const auto& size : preferredSizes)
    {
        const bool highQuality = false;
        camera.reset (juce::CameraDevice::openDevice (cameraIndex, size.minWidth, size.minHeight, size.maxWidth, size.maxHeight, highQuality));
        if (camera != nullptr)
            break;
    }

    {
        const juce::ScopedLock sl (frameLock);
        name = cameras[cameraIndex];
    }
    if (camera != nullptr)
        camera->addListener (this);
}

void CameraFeed::close()
{
    if (camera != nullptr)
        camera->removeListener (this);
    camera.reset();

    const juce::ScopedLock sl (frameLock);
    name.clear();
}

void CameraFeed::timerCallback()
{
    if (developmentFrame.isValid())
        imageReceived (developmentFrame);
    else
        retryIfNoPicture();
}

void CameraFeed::retryIfNoPicture()
{
    const auto receivedNow = framesReceived();
    if (receivedNow == std::exchange (receivedAtLastLook, receivedNow))
        open (cameraIndex + 1); // the next camera, or the only one again
}

juce::String CameraFeed::cameraName() const
{
    const juce::ScopedLock sl (frameLock);
    return name;
}

void CameraFeed::imageReceived (const juce::Image& image)
{
    received.fetch_add (1, std::memory_order_relaxed);
    {
        const juce::ScopedLock sl (frameLock);
        latestFrame = image;
    }
    frameArrived.signal();
}

juce::Image CameraFeed::waitForFrame (int timeoutMs)
{
    if (! frameArrived.wait (timeoutMs))
        return {};

    const juce::ScopedLock sl (frameLock);
    return std::exchange (latestFrame, {});
}
} // namespace tracker
