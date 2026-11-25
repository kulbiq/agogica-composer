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

    const auto cameras = availableCameras();
    const bool isChosenThere = cameras.contains (chosenCamera);
    startTrying (isChosenThere ? chosenCamera : cameras[0], isChosenThere);
}

CameraFeed::~CameraFeed()
{
    stopTimer();
    close();
}

void CameraFeed::choose (const juce::String& cameraName)
{
    if (! developmentFrame.isValid())
        startTrying (cameraName, true);
}

void CameraFeed::startTrying (const juce::String& cameraToOpen, bool usersChoice)
{
    isUsersChoice = usersChoice;
    camerasTried = 1;
    gaveUp = false;
    open (cameraToOpen);
    startTimer (noPictureTimeoutMs);
}

void CameraFeed::open (const juce::String& cameraToOpen)
{
    close();
    receivedAtLastLook = framesReceived();
    {
        const juce::ScopedLock sl (frameLock);
        name = cameraToOpen;
    }
    camera = CameraSource::open (cameraToOpen, [this] (const juce::Image& frame) { frameArrivedFromCamera (frame); });
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

    const auto cameras = availableCameras();
    if (isUsersChoice || camerasTried >= cameras.size())
        return giveUp();

    ++camerasTried;
    open (cameras[(cameras.indexOf (cameraName()) + 1) % cameras.size()]); // the one after the last tried
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
