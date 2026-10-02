#include "PluginEditor.h"
#include "bridge/Stage.h"
#include "bridge/Mime.h"
#include <BinaryData.h>

namespace
{
juce::ZipFile& getUiZip()
{
    static juce::MemoryInputStream stream { BinaryData::ui_zip, BinaryData::ui_zipSize, false };
    static juce::ZipFile zip { stream };
    return zip;
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
    return juce::WebBrowserComponent::Resource { std::move (data), web::mimeType (path) };
}
juce::var getBuildInfo (const juce::AudioProcessor& processor)
{
    auto info = std::make_unique<juce::DynamicObject>();
    info->setProperty ("version", JucePlugin_VersionString);
    info->setProperty ("format", juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType));
    info->setProperty ("host", juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFileNameWithoutExtension());
    info->setProperty ("credits", "Binaural HRTF: SADIE II database, KU100 (University of York), Apache License 2.0");
    return info.release();
}

// Debug builds configured with COMPOSER_UI_DEV_SERVER load the Vite dev server (hot reload).
#ifdef COMPOSER_UI_DEV_SERVER_URL
const juce::String uiUrl { COMPOSER_UI_DEV_SERVER_URL };
const std::optional<juce::String> resourceOrigin = juce::URL { uiUrl }.getOrigin();
#else
const juce::String uiUrl = juce::WebBrowserComponent::getResourceProviderRoot();
const std::optional<juce::String> resourceOrigin;
#endif
} // namespace

bool ComposerEditor::Browser::pageAboutToLoad (const juce::String& url)
{
    return url == uiUrl;
}

ComposerEditor::ComposerEditor (ComposerProcessor& p)
    : AudioProcessorEditor (&p),
      composer (p),
      browser { juce::WebBrowserComponent::Options{}
                    .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                    .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                                                 .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)))
                    .withNativeIntegrationEnabled()
                    .withNativeFunction ("getStage", [this] (const auto&, auto complete)
                                         {
                                             complete (stage::snapshot (composer));
                                         })
                    .withNativeFunction ("edit", [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 1)
                                                 stage::applyEdit (composer, args[0]);

                                             complete ({});
                                         })
                    .withNativeFunction ("setName", [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 2)
                                                 stage::setName (composer, (int) args[0], args[1].toString());

                                             complete ({});
                                         })
                    .withNativeFunction ("setScene", [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 1)
                                                 stage::setScene (composer, args[0].toString());

                                             complete ({});
                                         })
                    .withNativeFunction ("startTracker", [] (const auto&, auto complete)
                                         {
                                             complete (HeadTracking::startTracker());
                                         })
                    // "Not activated" screen: Agogica Hub signs in and writes the licence.
                    .withNativeFunction ("openHub", [] (const auto&, auto complete)
                                         {
                                             juce::ChildProcess open;
                                             const auto opened = open.start ("/usr/bin/open -b com.agogica.hub")
                                                                 && open.waitForProcessToFinish (5000) && open.getExitCode() == 0;
                                             if (! opened) // the hub isn't installed: get it from the website
                                                 juce::URL ("https://agogica.app/dashboard").launchInDefaultBrowser();
                                             complete (opened);
                                         })
                    .withNativeFunction ("checkLicence", [this] (const auto&, auto complete)
                                         {
                                             composer.licence->refresh();
                                             complete (composer.licence->isValid());
                                         })
                    .withNativeFunction ("getBuildInfo", [this] (const auto&, auto complete)
                                         {
                                             complete (getBuildInfo (composer));
                                         })
                    // The web page draws its own resize grip (JUCE's corner resizer would sit
                    // under the native webview); hosts like Ableton rely on it.
                    .withNativeFunction ("setEditorSize", [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 2)
                                                 setBoundsConstrained (getBounds().withSize ((int) args[0], (int) args[1]));

                                             complete ({});
                                         })
                    .withResourceProvider (getResource, resourceOrigin) }
{
    addAndMakeVisible (browser);

    setResizable (true, false);
    setResizeLimits (640, 400, 2560, 1600);
    setSize (960, 600);

    browser.onPageLoaded = [this] { startTimerHz (30); };
    browser.goToURL (uiUrl);
}

void ComposerEditor::timerCallback()
{
    // Every track's input level in dB, by player id; its own event so the stage diff
    // below stays quiet while audio plays.
    auto levels = std::make_unique<juce::DynamicObject>();
    const auto now = juce::Time::getMillisecondCounter();

    composer.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
            levels->setProperty (juce::String (p->id), juce::Decibels::gainToDecibels (p->readInputLevel (now), -60.0f));
    });

    browser.emitEventIfBrowserIsVisible ("levels", levels.release());

    // Automation playback and other changes reach the UI here; it only draws them.
    const auto stage = stage::snapshot (composer);

    if (auto json = juce::JSON::toString (stage, true); json != lastStage)
    {
        lastStage = std::move (json);
        browser.emitEventIfBrowserIsVisible ("stage", stage);
    }
}

void ComposerEditor::resized()
{
    browser.setBounds (getLocalBounds());
}
