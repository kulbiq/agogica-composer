// Agogica Tracker: follows the head with the camera (HFHPE, run natively by EngineHost)
// and shares its orientation with every Composer instance through the head-tracking file
// (Source/stage/HeadTrackingFile.h, written by PoseOutput on its own thread); the page
// (tracker/ui) shows the status and the dial and sends the buttons. No network: the page and
// the models are served from the app's own files.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_video/juce_video.h>
#include "../Source/bridge/Mime.h"
#include "EngineHost.h"
#include "PoseOutput.h"

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

// Width of the compact (debugging off) window; its height fits the page (fitHeight).
constexpr int compactWidth = 380;

class Window final : public juce::DocumentWindow,
                     private juce::MenuBarModel,
                     private juce::Timer
{
public:
    Window() : DocumentWindow ("Agogica Tracker", juce::Colours::black, closeButton | minimiseButton)
    {
        browser = std::make_unique<juce::WebBrowserComponent> (juce::WebBrowserComponent::Options{}
           #if JUCE_WINDOWS
            // The page remembers the camera section in localStorage: a lasting profile folder,
            // per PC like the licence.
            .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                .withUserDataFolder (juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                                         .getChildFile ("Agogica/Tracker/WebView2")))
           #endif
            .withNativeIntegrationEnabled()
            .withNativeFunction ("getInvert", [] (const auto&, auto complete)
            {
                complete (settings().getBoolValue ("invert", false));
            })
            .withNativeFunction ("setInvert", [this] (const juce::Array<juce::var>& args, auto complete)
            {
                if (args.size() == 1)
                {
                    settings().setValue ("invert", (bool) args[0]);
                    output.setInvert ((bool) args[0]);
                }
                complete ({});
            })
            .withNativeFunction ("recenter", [this] (const auto&, auto complete)
            {
                output.recenter();
                complete ({});
            })
            .withNativeFunction ("setPaused", [this] (const juce::Array<juce::var>& args, auto complete)
            {
                if (args.size() == 1)
                    output.setPaused ((bool) args[0]);
                complete ({});
            })
            .withNativeFunction ("setPreview", [this] (const juce::Array<juce::var>& args, auto complete)
            {
                if (args.size() == 1)
                    engines.setPreview ((bool) args[0]);
                complete ({});
            })
            .withNativeFunction ("getDebug", [this] (const auto&, auto complete)
            {
                pageReady = true; // events sent before the page has loaded assert
                startTimerHz (60); // the dial follows what Composer hears
                complete (debug);
            })
            .withNativeFunction ("fitHeight", [this] (const juce::Array<juce::var>& args, auto complete)
            {
                if (args.size() == 1 && ! debug)
                    fitCompact ((int) args[0]);
                complete ({});
            })
            .withNativeFunction ("start", [this] (const auto&, auto complete)
            {
                engines.start(); // once the page listens: the first status events are not repeated
                complete ({});
            })
            .withResourceProvider (serve));

        output.setInvert (settings().getBoolValue ("invert", false));
        setContentNonOwned (browser.get(), false);
        browser->setSize (380, 680);
        browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
        setUsingNativeTitleBar (true);
        // Resizable once, here: switching it later recreates the native window, and WebView2
        // goes on drawing into the old one (a frozen page - measured on Windows). The compact
        // view is fixed by its size limits instead (fitCompact).
        setResizable (true, false);

        // No always-on-top: the engine and PoseOutput run natively, so Composer keeps hearing
        // the head while a browser engine pauses the hidden page.
        if (const auto saved = settings().getValue ("windowState"); saved.isEmpty() || ! restoreWindowStateFromString (saved))
            centreWithSize (compactWidth, 600);
        setDebug (settings().getBoolValue ("debug", false));
        setVisible (true);

       #if JUCE_MAC
        static const auto about = aboutMenu();
        juce::MenuBarModel::setMacMainMenu (this, &about);
       #else
        setMenuBar (this);
       #endif
    }

    ~Window() override
    {
        stopTimer();
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #else
        setMenuBar (nullptr);
       #endif
        saveBounds();
    }

    void closeButtonPressed() override { juce::JUCEApplication::quit(); }

private:
    // View > Debugging: readouts and the plot, in a resizable window whose size is
    // remembered. Off: a fixed compact window with the status, dial, buttons and camera.
    void setDebug (bool on)
    {
        if (on == debug && isVisible())
            return;

        saveBounds();
        debug = on;
        settings().setValue ("debug", on);

        if (on)
        {
            setResizeLimits (340, 200, 1200, 2400);
            const auto saved = settings().getValue ("debugBounds");
            const auto r = juce::Rectangle<int>::fromString (saved);
            auto bounds = saved.isNotEmpty() && ! r.isEmpty() ? r : getBounds().withSize (900, 720);
            // On screen with its frame, clear of the taskbar / Dock (a size saved on a bigger
            // display, or at another scale, would hang off the edge).
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (bounds))
            {
                auto area = display->userBounds.getSmallestIntegerContainer();
                if (auto* peer = getPeer())
                    if (const auto frame = peer->getFrameSizeIfPresent())
                        area = frame->subtractedFrom (area);
                bounds = bounds.constrainedWithin (area);
            }
            setBounds (bounds);
        }
        else
        {
            fitCompact (browser->getHeight()); // the page then fits the height
        }

        if (pageReady)
            browser->emitEventIfBrowserIsVisible ("debug", on);
        menuItemsChanged();
    }

    // The compact window: `compactWidth` wide, as tall as the page, and fixed at that size.
    void fitCompact (int contentHeight)
    {
        setResizeLimits (1, 1, 1 << 15, 1 << 15);
        setContentComponentSize (compactWidth, juce::jlimit (200, 1600, contentHeight));

        // The limits apply to the window with its native frame (JUCE's peers add the frame
        // before constraining): limits of our own size alone shrink it by the frame.
        auto size = getBounds();
        if (auto* peer = getPeer())
            if (const auto frame = peer->getFrameSizeIfPresent())
                size = frame->addedTo (size);
        setResizeLimits (size.getWidth(), size.getHeight(), size.getWidth(), size.getHeight());
    }

    void saveBounds()
    {
        settings().setValue ("windowState", getWindowStateAsString());
        if (debug)
            settings().setValue ("debugBounds", getBounds().toString());
    }

    enum MenuItem { debuggingItem = 1, aboutItem };

    // macOS: About is in the app menu (aboutMenu); elsewhere a Help menu holds it.
    juce::StringArray getMenuBarNames() override
    {
       #if JUCE_MAC
        return { "View" };
       #else
        return { "View", "Help" };
       #endif
    }

    juce::PopupMenu getMenuForIndex (int index, const juce::String&) override
    {
        juce::PopupMenu menu;
        if (index == 0)
            menu.addItem (debuggingItem, "Debugging", true, debug);
        else
            menu.addItem (aboutItem, "About Agogica Tracker");
        return menu;
    }

    void menuItemSelected (int itemId, int) override
    {
        if (itemId == debuggingItem)
            setDebug (! debug);
        else if (itemId == aboutItem)
            showAbout();
    }

    static juce::PopupMenu aboutMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (aboutItem, "About Agogica Tracker");
        return menu;
    }

    // The page shows it (in the app's own style), with the version from here.
    void showAbout()
    {
        if (pageReady)
            browser->emitEventIfBrowserIsVisible ("about", juce::JUCEApplication::getInstance()->getApplicationVersion());
    }

    // The dial: what Composer hears (60 Hz while the page shows).
    void timerCallback() override
    {
        const auto [yaw, live] = output.current();
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("yaw", yaw);
        o->setProperty ("live", live);
        browser->emitEventIfBrowserIsVisible ("pose", juce::var (o.get()));
    }

    bool debug = false, pageReady = false;

    // Declared before the engine, which feeds it from its thread (destroyed after it).
    PoseOutput output { headtracking::defaultFile() };
    EngineHost engines { resources().getChildFile ("models"), [this] (const juce::Identifier& event, const juce::var& v)
    {
        if (browser != nullptr && pageReady)
            browser->emitEventIfBrowserIsVisible (event, v);
    }, [this] (bool found, float yaw, float pitch, float roll) { output.frame (found, yaw, pitch, roll); } };
    juce::ScopedLowPowerModeDisabler noAppNap; // tracking must keep running in the background
    std::unique_ptr<juce::WebBrowserComponent> browser;
};

class App final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Agogica Tracker"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        // A crash leaves its cause next to the settings (Agogica/Tracker-crash.log): the exception
        // that ended the app, or the fault, with the stack (module + offset without symbols).
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
        window = std::make_unique<Window>();
    }
    void shutdown() override                            { window.reset(); }

private:
    static void writeCrash (const juce::String& what)
    {
        settings().getFile().getSiblingFile ("Tracker-crash.log")
            .appendText (juce::Time::getCurrentTime().toISO8601 (true) + " " + what + "\n"
                         + juce::SystemStats::getStackBacktrace() + "\n");
    }

    std::unique_ptr<Window> window;
};
} // namespace

START_JUCE_APPLICATION (App)
