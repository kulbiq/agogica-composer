#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../tracking/ListenerPoseOutput.h"
#include "../tracking/TrackingReports.h"
#include "TrackerSettings.h"

namespace tracker
{
// The app's page (tracker/ui) in a web view, and everything the app and the page say to each
// other: the only C++ that knows the names of the page's functions and events and their JSON.
// tracker/ui/src/bridge.ts is the page's side of it - change the two together.
class TrackerPage final : public TrackingListener,
                          private juce::AsyncUpdater
{
public:
    // `fitHeight`: the page asks for the compact view to be as tall as its content (message thread).
    TrackerPage (juce::File pageFolder, TrackerSettings&, ListenerPoseOutput&, std::function<void (int heightPixels)> fitHeight);
    ~TrackerPage() override;

    juce::Component& component() { return browser; }

    // From the tracking thread: the page gets the latest of each kind.
    void report (const TrackerStatus&) override;
    void report (const FrameReport&) override;
    void report (const PerformanceReport&) override;
    bool wantsCameraPreview() const override { return isCameraPreviewWanted; }
    void reportCameraPreview (const juce::MemoryBlock& jpeg) override;

    // Message thread.
    void showDebugView (bool on);
    void showAbout();

private:
    juce::WebBrowserComponent::Options options (std::function<void (int)> fitHeight);
    void handleAsyncUpdate() override;
    void emit (const juce::Identifier& event, const juce::var& payload);

    const juce::File pageFolder;
    TrackerSettings& settings;
    ListenerPoseOutput& poseOutput;
    bool isPageReady = false; // events sent before the page has loaded assert
    bool isDebugView = false;
    std::atomic<bool> isCameraPreviewWanted { false }; // while the page's Camera section is open

    juce::CriticalSection pendingLock;
    std::optional<TrackerStatus> latestStatus; // kept: a page that loads later still gets it
    bool isStatusUnsent = false;
    std::optional<FrameReport> pendingFrame;
    std::optional<PerformanceReport> pendingPerformance;
    juce::MemoryBlock pendingPreviewJpeg;

    juce::WebBrowserComponent browser;
};
} // namespace tracker
