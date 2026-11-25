#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../tracking/TrackingReports.h"

namespace tracker
{
// The app's page (tracker/ui) in a web view, and everything the app and the page say to each
// other: the only C++ that knows the names of the page's functions and events and their JSON.
// tracker/ui/src/bridge.ts is the page's side of it - change the two together.
class TrackerPage final : private juce::AsyncUpdater
{
public:
    // What the page needs to draw itself the first time.
    struct InitialState
    {
        bool inverted = false, debugView = false;
        juce::String version;
    };

    // What the page asks the app to do (message thread).
    struct Actions
    {
        std::function<InitialState()> initialState;
        std::function<void (bool)> setInverted, setPaused, setCameraPreviewEnabled;
        std::function<void()> recenter;
        std::function<void (int heightPixels)> fitHeight; // the compact view's content height
    };

    TrackerPage (juce::File pageFolder, Actions);
    ~TrackerPage() override;

    juce::Component& component() { return browser; }

    // From the tracking thread: the page gets the latest of each kind.
    void report (const TrackerStatus&);
    void report (const FrameReport&);
    void report (const PerformanceReport&);
    void reportCameraPreview (const juce::MemoryBlock& jpeg);

    // Message thread.
    void showDebugView (bool on);
    void showAbout();

private:
    juce::WebBrowserComponent::Options options (Actions);
    void handleAsyncUpdate() override;
    void emit (const juce::Identifier& event, const juce::var& payload);

    const juce::File pageFolder;
    bool isPageReady = false; // events sent before the page has loaded assert

    juce::CriticalSection pendingLock;
    std::optional<TrackerStatus> latestStatus; // kept: a page that loads later still gets it
    bool isStatusUnsent = false;
    std::optional<FrameReport> pendingFrame;
    std::optional<PerformanceReport> pendingPerformance;
    juce::MemoryBlock pendingPreviewJpeg;

    juce::WebBrowserComponent browser;
};
} // namespace tracker
