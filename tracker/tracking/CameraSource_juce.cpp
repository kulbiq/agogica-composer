#include <juce_video/juce_video.h>
#include "CameraSource.h"

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

class JuceCameraSource final : public CameraSource,
                               private juce::CameraDevice::Listener
{
public:
    JuceCameraSource (std::unique_ptr<juce::CameraDevice> device, FrameCallback callback)
        : camera (std::move (device)), onFrame (std::move (callback))
    {
        camera->addListener (this);
    }

    ~JuceCameraSource() override { camera->removeListener (this); }

private:
    void imageReceived (const juce::Image& frame) override { onFrame (frame); }

    const std::unique_ptr<juce::CameraDevice> camera;
    const FrameCallback onFrame;
};
} // namespace

juce::StringArray CameraSource::availableCameras() { return juce::CameraDevice::getAvailableDevices(); }

std::unique_ptr<CameraSource> CameraSource::open (int index, FrameCallback onFrame)
{
    for (const auto& size : preferredSizes)
    {
        const bool highQuality = false;
        std::unique_ptr<juce::CameraDevice> device (juce::CameraDevice::openDevice (index, size.minWidth, size.minHeight, size.maxWidth, size.maxHeight, highQuality));
        if (device != nullptr)
            return std::make_unique<JuceCameraSource> (std::move (device), std::move (onFrame));
    }
    return nullptr;
}
} // namespace tracker
