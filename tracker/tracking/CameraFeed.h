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
// A camera that gives no picture for `noPictureTimeoutMs` is let go (`hasGivenUp`): the app
// doesn't keep waking a camera that won't answer (a phone offered as one and out of reach, a
// laptop's own camera under a closed lid).
class CameraFeed final : private juce::Timer
{
public:
    // A saved ID that isn't present falls back to automatic selection. A legacy name
    // is used only for settings saved before camera IDs were stored.
    CameraFeed (const juce::String& chosenId, const juce::String& legacyName); // message thread
    ~CameraFeed() override;

    static std::vector<CameraDeviceInfo> availableCameras() { return CameraSource::availableCameras(); }

    // The user's choice: opened now, and tried afresh. A camera that has gone since it was
    // listed is never replaced by another: it ends as "no picture". Message thread.
    void choose (const CameraDeviceInfo&);
    const juce::String& cameraId() const { return currentId; } // message thread

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
    void moveOnIfNoPicture();
    void startTrying (const CameraDeviceInfo&, bool isUsersChoice);
    void open (const CameraDeviceInfo&);
    void close();
    void giveUp();

    // Development: AGOGICA_TRACKER_STILL_FRAME=<picture file> stands in for the camera, 30 times
    // a second - the whole app can then be run and checked on a computer without a camera.
    const juce::Image developmentFrame;

    std::unique_ptr<CameraSource> camera;
    bool isUsersChoice = false;
    juce::String currentId;
    juce::StringArray attemptedIds; // since the last picture or choice
    int receivedAtLastLook = 0;
    std::atomic<int> received { 0 };
    std::atomic<bool> gaveUp { false };

    juce::CriticalSection frameLock; // latestFrame and name
    juce::Image latestFrame;
    juce::String name;
    juce::WaitableEvent frameArrived;
};
} // namespace tracker
