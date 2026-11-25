#pragma once

#include <juce_core/juce_core.h>

// Starts the Agogica Tracker app for the page's "Start head tracking", without ever waiting on
// the host's message thread. macOS only: elsewhere the Tracker has no install yet.
class TrackerLauncher
{
public:
    // `done (true)` once the Tracker is starting: at once for the per-user install
    // (~/Applications), otherwise when `open` has found it by its bundle id. `done (false)`
    // if there is no Tracker, or `open` is still running after 5 s.
    void start (std::function<void (bool started)> done);

    // Call regularly (the editor's timer): reports the launches that have finished.
    void poll();

private:
    struct Launch
    {
        std::unique_ptr<juce::ChildProcess> open;
        std::function<void (bool)> done;
        juce::uint32 startedAt;
    };
    std::vector<Launch> launches;
};
