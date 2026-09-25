// Agogica Tracker: follows the head with the camera (MediaPipe Face Landmarker, in the web
// page under tracker/ui) and shares its orientation with every Composer instance through
// the head-tracking file (Source/stage/HeadTrackingFile.h). No network: the page, the
// WASM runtime and the model are served from the app bundle.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_video/juce_video.h>
#include "../Source/bridge/Mime.h"
#include "../Source/stage/HeadTrackingFile.h"
#include "EngineHost.h"

namespace
{
// The built page (tracker/ui/dist), copied into the bundle by the build.
juce::File resources()
{
    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
   #if JUCE_MAC
    return app.getChildFile ("Contents/Resources");
   #else
    return app.getParentDirectory();
   #endif
}

juce::File pageRoot() { return resources().getChildFile ("ui"); }

std::optional<juce::WebBrowserComponent::Resource> serve (const juce::String& url)
{
    auto path = url.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    if (path.isEmpty())
        path = "index.html";
    const auto root = pageRoot();
    const auto file = root.getChildFile (path);

    // Only files inside the page folder.
    if (! file.isAChildOf (root) || ! file.existsAsFile())
        return std::nullopt;

    juce::MemoryBlock data;
    file.loadFileAsData (data);
    const auto* bytes = static_cast<const std::byte*> (data.getData());
    return juce::WebBrowserComponent::Resource { { bytes, bytes + data.getSize() }, web::mimeType (path) };
}

// ~/Library/Application Support/Agogica/Tracker.settings
juce::PropertiesFile& settings()
{
    static juce::PropertiesFile file ([]
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Tracker";
        o.folderName = "Agogica";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }());
    return file;
}

class Window final : public juce::DocumentWindow
{
public:
    Window() : DocumentWindow ("Agogica Tracker", juce::Colours::black, closeButton | minimiseButton)
    {
        browser = std::make_unique<juce::WebBrowserComponent> (juce::WebBrowserComponent::Options{}
            .withNativeIntegrationEnabled()
            .withNativeFunction ("pose", [this] (const juce::Array<juce::var>& args, auto complete)
            {
                if (args.size() == 4)
                    writer.write ({ (float) args[0], (float) args[1], (float) args[2],
                                    (std::uint64_t) juce::Time::currentTimeMillis(), (bool) args[3] });
                complete ({});
            })
            .withNativeFunction ("setEngine", [this] (const juce::Array<juce::var>& args, auto complete)
            {
                if (args.size() == 1)
                    engines.setEngine (args[0].toString());
                complete ({});
            })
            .withResourceProvider (serve));

        setContentNonOwned (browser.get(), false);
        browser->setSize (380, 680);
        // Development: AGOGICA_TRACKER_ENGINE=whenet starts on that engine.
        const auto engine = juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_ENGINE", {});
        browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + (engine.isNotEmpty() ? "?engine=" + engine : juce::String()));
        setUsingNativeTitleBar (true);
        // Resizable (taller shows the camera preview and the angle together); the size and
        // position are remembered between launches.
        setResizable (true, false);
        setResizeLimits (340, 420, 1200, 2400);

        // WebKit pauses a page it thinks isn't visible, which would stop tracking whenever
        // Dorico is in front: keep the window above other windows.
        setAlwaysOnTop (true);
        if (const auto saved = settings().getValue ("windowState"); saved.isEmpty() || ! restoreWindowStateFromString (saved))
            centreWithSize (380, 680);
        setVisible (true);
    }

    ~Window() override
    {
        settings().setValue ("windowState", getWindowStateAsString());
        writer.write ({}); // no face: Composer eases back to straight ahead
    }

    void closeButtonPressed() override { juce::JUCEApplication::quit(); }

private:
    headtracking::Writer writer { headtracking::defaultFile() };
    EngineHost engines { resources().getChildFile ("models"), [this] (const juce::var& v)
    {
        if (browser != nullptr)
            browser->emitEventIfBrowserIsVisible ("estimate", v);
    } };
    juce::ScopedLowPowerModeDisabler noAppNap; // tracking must keep running in the background
    std::unique_ptr<juce::WebBrowserComponent> browser;
};

class App final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Agogica Tracker"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }
    void initialise (const juce::String&) override      { window = std::make_unique<Window>(); }
    void shutdown() override                            { window.reset(); }

private:
    std::unique_ptr<Window> window;
};
} // namespace

START_JUCE_APPLICATION (App)
