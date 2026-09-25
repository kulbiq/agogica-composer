// Agogica Tracker: follows the head with the camera (MediaPipe Face Landmarker, in the web
// page under tracker/ui) and shares its orientation with every Composer instance through
// the head-tracking file (Source/stage/HeadTrackingFile.h). No network: the page, the
// WASM runtime and the model are served from the app bundle.
#include <juce_gui_extra/juce_gui_extra.h>
#include "../Source/bridge/Mime.h"
#include "../Source/stage/HeadTrackingFile.h"

namespace
{
// The built page (tracker/ui/dist), copied into the bundle by the build.
juce::File pageRoot()
{
    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
   #if JUCE_MAC
    return app.getChildFile ("Contents/Resources/ui");
   #else
    return app.getParentDirectory().getChildFile ("ui");
   #endif
}

std::optional<juce::WebBrowserComponent::Resource> serve (const juce::String& url)
{
    const auto path = url == "/" ? juce::String { "index.html" }
                                 : url.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
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
            .withResourceProvider (serve));

        setContentNonOwned (browser.get(), false);
        browser->setSize (380, 620);
        browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
        setUsingNativeTitleBar (true);
        setResizable (false, false);
        centreWithSize (380, 620);
        setVisible (true);
    }

    ~Window() override
    {
        writer.write ({}); // no face: Composer eases back to straight ahead
    }

    void closeButtonPressed() override { juce::JUCEApplication::quit(); }

private:
    headtracking::Writer writer { headtracking::defaultFile() };
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
