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
constexpr int firstCamera = 0;
} // namespace

CameraFeed::CameraFeed()
{
    for (const auto& size : preferredSizes)
    {
        const bool highQuality = false;
        camera.reset (juce::CameraDevice::openDevice (firstCamera, size.minWidth, size.minHeight, size.maxWidth, size.maxHeight, highQuality));
        if (camera != nullptr)
            break;
    }

    if (camera != nullptr)
        camera->addListener (this);
}

CameraFeed::~CameraFeed()
{
    if (camera != nullptr)
        camera->removeListener (this);
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
