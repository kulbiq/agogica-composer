#pragma once

#include <juce_events/juce_events.h>
#include "CameraSource.h"

namespace tracker
{
// A camera, and its latest frame for whoever asks: frames arrive on the camera's own thread
// faster than they may be used, so a newer frame replaces one nobody took.
//
// Which camera: the one the user chose (Camera menu), or - with no choice - each camera once, in
// the system's order, until one gives a picture.
// A camera that gives no picture is tried a few times and then left alone (`hasGivenUp`): the
// app doesn't keep waking a camera that won't answer (a phone offered as one and out of reach,
// a laptop's own camera under a closed lid).
class CameraFeed final : private juce::Timer
{
public:
    // `chosenCamera`: a name from `availableCameras()`; empty, or a camera that isn't there = no choice.
    explicit CameraFeed (const juce::String& chosenCamera); // message thread
    ~CameraFeed() override;

    static juce::StringArray availableCameras() { return CameraSource::availableCameras(); }

    // The user's choice: opened now, and tried afresh. Message thread.
    void choose (const juce::String& cameraName);

    // Waits for a frame nobody has taken yet; an invalid image when none came in time. Any thread.
    juce::Image waitForFrame (int timeoutMs);

    // Every frame the cameras have delivered, taken or not. Any thread.
    int framesReceived() const { return received.load (std::memory_order_relaxed); }

    // The camera in use or last tried; empty when the computer has none. Any thread.
    juce::String cameraName() const;

    // No camera gave a picture and none is being tried any more (until `choose`). Any thread.
    bool hasGivenUp() const { return gaveUp.load (std::memory_order_relaxed); }

private:
    void frameArrivedFromCamera (const juce::Image&);
    void timerCallback() override;
    void retryIfNoPicture();
    void startTrying (int cameraIndex, bool isUsersChoice);
    void open (int cameraIndex);
    void close();
    void giveUp();

    // Development: AGOGICA_TRACKER_STILL_FRAME=<picture file> stands in for the camera, 30 times
    // a second - the whole app can then be run and checked on a computer without a camera.
    const juce::Image developmentFrame;

    std::unique_ptr<CameraSource> camera;
    int cameraIndex = 0;
    bool isUsersChoice = false;
    bool hasCurrentCameraDelivered = false;
    int attemptsWithoutPicture = 0; // on the current camera
    int camerasTriedWithoutPicture = 0;
    int receivedAtLastLook = 0;
    std::atomic<int> received { 0 };
    std::atomic<bool> gaveUp { false };

    juce::CriticalSection frameLock; // latestFrame and name
    juce::Image latestFrame;
    juce::String name;
    juce::WaitableEvent frameArrived;
};
} // namespace tracker
