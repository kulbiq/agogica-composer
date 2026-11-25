#include "CameraFeed.h"

namespace tracker
{
namespace
{
// Long enough for a camera to start: a phone offered as a camera has to wake and connect first,
// and at 4 s it was closed and opened again before it could answer.
constexpr int noPictureTimeoutMs = 10000;
constexpr int developmentFramesPerSecond = 30;
} // namespace

CameraFeed::CameraFeed (const juce::String& chosenCamera)
    : developmentFrame (juce::ImageFileFormat::loadFrom (juce::File (juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_STILL_FRAME", {}))))
{
    if (developmentFrame.isValid())
    {
        name = "Still frame (development)";
        startTimerHz (developmentFramesPerSecond);
        return;
    }

    const auto chosenIndex = availableCameras().indexOf (chosenCamera);
    startTrying (juce::jmax (0, chosenIndex), chosenIndex >= 0);
}

CameraFeed::~CameraFeed()
{
    stopTimer();
    close();
}

void CameraFeed::choose (const juce::String& cameraName)
{
    if (developmentFrame.isValid())
        return;

    if (const auto index = availableCameras().indexOf (cameraName); index >= 0)
        startTrying (index, true);
}

void CameraFeed::startTrying (int index, bool usersChoice)
{
    isUsersChoice = usersChoice;
    camerasTried = 1;
    gaveUp = false;
    open (index);
    startTimer (noPictureTimeoutMs);
}

void CameraFeed::open (int index)
{
    close();
    receivedAtLastLook = framesReceived();

    const auto cameras = availableCameras();
    if (cameras.isEmpty())
        return;

    cameraIndex = index % cameras.size();
    {
        const juce::ScopedLock sl (frameLock);
        name = cameras[cameraIndex];
    }
    camera = CameraSource::open (cameraIndex, [this] (const juce::Image& frame) { frameArrivedFromCamera (frame); });
}

// The camera is let go; its name stays, for the status.
void CameraFeed::close()
{
    camera.reset(); // no frame arrives after this
}

void CameraFeed::giveUp()
{
    stopTimer();
    close();
    gaveUp = true;
}

void CameraFeed::timerCallback()
{
    if (developmentFrame.isValid())
        frameArrivedFromCamera (developmentFrame);
    else
        moveOnIfNoPicture();
}

// Every `noPictureTimeoutMs`: no frame since the last look? With no choice made, the next
// camera gets its turn, each once; the user's own choice, or the last camera, is let go.
void CameraFeed::moveOnIfNoPicture()
{
    const auto receivedNow = framesReceived();
    if (receivedNow != std::exchange (receivedAtLastLook, receivedNow))
    {
        camerasTried = 1;
        return;
    }

    if (isUsersChoice || camerasTried >= availableCameras().size())
        return giveUp();

    ++camerasTried;
    open (cameraIndex + 1);
}

juce::String CameraFeed::cameraName() const
{
    const juce::ScopedLock sl (frameLock);
    return name;
}

void CameraFeed::frameArrivedFromCamera (const juce::Image& image)
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
