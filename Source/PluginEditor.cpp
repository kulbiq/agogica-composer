#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
juce::ZipFile& getUiZip()
{
    static juce::MemoryInputStream stream { BinaryData::ui_zip, BinaryData::ui_zipSize, false };
    static juce::ZipFile zip { stream };
    return zip;
}

const char* getMimeType (const juce::String& path)
{
    static const std::unordered_map<juce::String, const char*> types {
        { "html",  "text/html" },
        { "js",    "text/javascript" },
        { "css",   "text/css" },
        { "json",  "application/json" },
        { "svg",   "image/svg+xml" },
        { "png",   "image/png" },
        { "woff2", "font/woff2" },
        { "woff",  "font/woff" },
    };

    const auto it = types.find (path.fromLastOccurrenceOf (".", false, false).toLowerCase());
    return it != types.end() ? it->second : "application/octet-stream";
}

std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url)
{
    const auto path = url == "/" ? juce::String { "index.html" }
                                 : url.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);

    auto& zip = getUiZip();
    const auto* entry = zip.getEntry (path);

    if (entry == nullptr)
        return std::nullopt;

    std::unique_ptr<juce::InputStream> in { zip.createStreamForEntry (*entry) };

    if (in == nullptr)
        return std::nullopt;

    std::vector<std::byte> data ((size_t) in->getTotalLength());
    in->read (data.data(), data.size());
    return juce::WebBrowserComponent::Resource { std::move (data), getMimeType (path) };
}
} // namespace

ComposerEditor::ComposerEditor (ComposerProcessor& p)
    : AudioProcessorEditor (&p),
      browser { juce::WebBrowserComponent::Options{}
                    .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                    .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                                                 .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)))
                    .withNativeIntegrationEnabled()
                    // The web page draws its own resize grip (JUCE's corner resizer would sit
                    // under the native webview); hosts like Ableton rely on it.
                    .withNativeFunction ("setEditorSize", [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 2)
                                                 setBoundsConstrained (getBounds().withSize ((int) args[0], (int) args[1]));

                                             complete ({});
                                         })
                    .withResourceProvider (getResource) }
{
    addAndMakeVisible (browser);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, false);
    setResizeLimits (640, 400, 2560, 1600);
    setSize (960, 600);
}

void ComposerEditor::resized()
{
    browser.setBounds (getLocalBounds());
}
