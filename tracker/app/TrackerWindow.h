#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../tracking/CameraFeed.h"
#include "../tracking/ListenerPoseOutput.h"
#include "../tracking/TrackingLoop.h"
#include "TrackerPage.h"
#include "TrackerSettings.h"

namespace tracker
{
// The app's one window, and where its parts meet: the camera feeds the tracking loop, the loop
// feeds Composer's file (ListenerPoseOutput) and the page; the page's buttons come back here.
// Two views (View > Debugging): compact - a fixed window as tall as the page; debugging - a
// resizable one with the readouts and the plot, its size remembered.
class TrackerWindow final : public juce::DocumentWindow,
                            private juce::MenuBarModel,
                            private juce::Timer
{
public:
    TrackerWindow();
    ~TrackerWindow() override;

    void closeButtonPressed() override;

private:
    TrackerPage::Actions pageActions();
    TrackingLoop::Reports trackingReports();

    void setDebugView (bool on);
    void showDebugViewWindow();
    void fitCompactViewToPage (int pageHeightPixels);
    void saveBounds();

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int menuIndex, const juce::String&) override;
    void menuItemSelected (int itemId, int) override;
    static juce::PopupMenu aboutMenu();

    void timerCallback() override; // the dial: what Composer hears

    bool isDebugView = false;

    // In this order: each is used by the ones after it, which are destroyed first (the
    // tracking thread stops before anything it reports to goes away).
    TrackerSettings settings;
    ListenerPoseOutput listenerPoseOutput;
    TrackerPage page;
    CameraFeed camera;
    TrackingLoop trackingLoop;
    juce::ScopedLowPowerModeDisabler noAppNap; // tracking must keep running in the background
};
} // namespace tracker
