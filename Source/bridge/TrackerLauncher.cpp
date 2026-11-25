#include "TrackerLauncher.h"

void TrackerLauncher::start (std::function<void (bool)> done)
{
   #if JUCE_MAC
    // The per-user install first: a build folder may hold another copy with the same identity.
    const auto installed = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                               .getChildFile ("Applications/Agogica Tracker.app");

    if (installed.exists() && installed.startAsProcess())
    {
        done (true);
        return;
    }

    if (auto open = std::make_unique<juce::ChildProcess>(); open->start (juce::StringArray { "/usr/bin/open", "-b", "com.agogica.tracker" }))
    {
        launches.push_back ({ std::move (open), std::move (done), juce::Time::getMillisecondCounter() });
        return;
    }
   #endif

    done (false);
}

void TrackerLauncher::poll()
{
    const auto now = juce::Time::getMillisecondCounter();

    for (auto it = launches.begin(); it != launches.end();)
    {
        const auto running = it->open->isRunning();
        if (running && now - it->startedAt < 5000)
        {
            ++it;
            continue;
        }

        it->done (! running && it->open->getExitCode() == 0);
        it = launches.erase (it);
    }
}
