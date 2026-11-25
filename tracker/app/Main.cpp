// Agogica Tracker: follows the head with the camera and shares its orientation with every
// Composer instance through the head-tracking file (Source/stage/HeadTrackingFile.h).
// No network: the page and the models are the app's own files. tracker/README.md has the map.
#include <juce_gui_extra/juce_gui_extra.h>
#include "TrackerSettings.h"
#include "TrackerWindow.h"

namespace
{
// A crash leaves its cause next to the settings (Agogica/Tracker-crash.log): the exception that
// ended the app, or the fault, with the stack (module + offset without symbols).
juce::File crashLogFile; // set before the handlers: nothing is looked up while crashing

void writeCrash (const juce::String& what)
{
    crashLogFile.appendText (juce::Time::getCurrentTime().toISO8601 (true) + " " + what + "\n"
                             + juce::SystemStats::getStackBacktrace() + "\n");
}

void installCrashLog()
{
    crashLogFile = tracker::TrackerSettings::folder().getChildFile ("Tracker-crash.log");

    std::set_terminate ([]
    {
        juce::String what = "terminate (no exception)";
        if (auto e = std::current_exception())
        {
            try { std::rethrow_exception (e); }
            catch (const std::exception& x) { what = "uncaught exception: " + juce::String (x.what()); }
            catch (...) { what = "uncaught non-std exception"; }
        }
        writeCrash (what);
        std::abort();
    });
    juce::SystemStats::setApplicationCrashHandler ([] (void*) { writeCrash ("fault"); });
}

class TrackerApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Agogica Tracker"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        installCrashLog();
        window = std::make_unique<tracker::TrackerWindow>();
    }

    void shutdown() override { window.reset(); }

private:
    std::unique_ptr<tracker::TrackerWindow> window;
};
} // namespace

START_JUCE_APPLICATION (TrackerApp)
