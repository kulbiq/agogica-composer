#include "CameraFeed.h"

namespace tracker
{
namespace
{
// Long enough for a camera to start: a phone offered as a camera has to wake and connect first,
// and at 4 s it was closed and opened again before it could answer.
constexpr int noPictureTimeoutMs = 10000;
constexpr int attemptsPerCamera = 3; // for a camera the user chose, or one that did work
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
    camerasTriedWithoutPicture = 0;
    gaveUp = false;
    open (index);
    startTimer (noPictureTimeoutMs);
}

void CameraFeed::open (int index)
{
    close();
    hasCurrentCameraDelivered = false;
    attemptsWithoutPicture = 0;
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
        retryIfNoPicture();
}

// Every `noPictureTimeoutMs`: did frames come since the last look?
void CameraFeed::retryIfNoPicture()
{
    const auto receivedNow = framesReceived();
    if (receivedNow != std::exchange (receivedAtLastLook, receivedNow))
    {
        hasCurrentCameraDelivered = true;
        attemptsWithoutPicture = 0;
        return;
    }

    // The user's camera, or one that worked until now: the same one again, a few times.
    if (isUsersChoice || hasCurrentCameraDelivered)
    {
        const auto attempts = attemptsWithoutPicture + 1;
        const bool delivered = hasCurrentCameraDelivered;
        if (attempts >= attemptsPerCamera)
            return giveUp();

        camera.reset(); // open() starts the counts again: keep them across it
        open (cameraIndex);
        attemptsWithoutPicture = attempts;
        hasCurrentCameraDelivered = delivered;
        return;
    }

    // No choice made, and this camera never gave a picture: the next one, each camera once.
    if (++camerasTriedWithoutPicture >= availableCameras().size())
        return giveUp();
    const auto tried = camerasTriedWithoutPicture;
    open (cameraIndex + 1);
    camerasTriedWithoutPicture = tried;
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
