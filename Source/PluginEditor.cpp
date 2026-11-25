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

    const auto length = in->getTotalLength();

    if (length < 0)
        return std::nullopt;

    std::vector<std::byte> data ((size_t) length);

    if (in->read (data.data(), data.size()) != (juce::int64) data.size())
        return std::nullopt;

    return juce::WebBrowserComponent::Resource { std::move (data), web::mimeType (path) };
}
juce::var getBuildInfo (const juce::AudioProcessor& processor)
{
    auto info = std::make_unique<juce::DynamicObject>();
    info->setProperty ("version", JucePlugin_VersionString);
    info->setProperty ("format", juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType));
    info->setProperty ("host", juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFileNameWithoutExtension());
    info->setProperty ("credits", "Binaural HRTF: SADIE II database, KU100 (University of York), Apache License 2.0");
   #if JUCE_DEBUG
    info->setProperty ("debug", true); // the footer shows the DSP load
   #endif
    return info.release();
}

// WebView2's profile: our own folder (like the Tracker's), not the temp folder that every app
// or plugin built from JUCE's WebView demo shares.
juce::File webViewProfile()
{
   #if JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Agogica/Composer/WebView2");
   #else
    return {}; // WebView2 is Windows only
   #endif
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
                                                 .withUserDataFolder (webViewProfile()))
                    .withNativeIntegrationEnabled()
                    .withNativeFunction ("getStage", [this] (const auto&, auto complete)
                                         {
                                             complete (stage::snapshot (composer));
                                         })
                    .withNativeFunction ("edit", [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 1)
                                                 gesturesBegunHere.applyEdit (composer, args[0]);

                                             complete ({});
                                         })
                    .withNativeFunction ("setName", [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 2 && args[0].isInt() && args[1].isString())
                                                 stage::setName (composer, (int) args[0], args[1].toString());

                                             complete ({});
                                         })
                    .withNativeFunction ("setScene", [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 1)
                                                 stage::setScene (composer, args[0].toString());

                                             complete ({});
                                         })
                    .withNativeFunction ("startTracker", [this] (const auto&, auto complete)
                                         {
                                             if (HeadTracking::startInstalledTracker())
                                                 complete (true);
                                             else
                                                 openApp ("com.agogica.tracker", std::move (complete));
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

    browser.onPageLoaded = [this]
    {
        gesturesBegunHere.endAll (composer); // a page that reloaded in the middle of a drag

        // A fresh page knows nothing: what was last sent to the old one doesn't count.
        lastStage.clear();
        lastHeadYaw = 0.0f;
        startTimerHz (30);
    };
    browser.goToURL (uiUrl);
}

ComposerEditor::~ComposerEditor()
{
    gesturesBegunHere.endAll (composer);
}

void ComposerEditor::openApp (const juce::String& bundleId, juce::WebBrowserComponent::NativeFunctionCompletion complete)
{
   #if JUCE_MAC
    if (auto process = std::make_unique<juce::ChildProcess>(); process->start (juce::StringArray { "/usr/bin/open", "-b", bundleId }))
    {
        opening.push_back ({ std::move (process), std::move (complete), juce::Time::getMillisecondCounter() });
        return;
    }
   #else
    juce::ignoreUnused (bundleId);
   #endif

    complete (false);
}

void ComposerEditor::finishOpening()
{
    const auto now = juce::Time::getMillisecondCounter();

    for (auto it = opening.begin(); it != opening.end();)
    {
        const auto running = it->process->isRunning();
        if (running && now - it->startedAt < 5000)
        {
            ++it;
            continue;
        }

        it->complete (! running && it->process->getExitCode() == 0);
        it = opening.erase (it);
    }
}

void ComposerEditor::timerCallback()
{
    finishOpening();

    // Nothing is sent to a page that isn't shown, so nothing is worked out for it either:
    // what it last saw stays "last sent", and it catches up when it is shown again.
    if (! browser.isVisible())
        return;

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

    // Development monitoring (LoadMeter), ~4 times a second: this track, and all Composer
    // tracks in this process (the sum is their share of one core; host threads may spread it).
    if (++loadTicks >= 8)
    {
        loadTicks = 0;
        auto reading = [] (const LoadMeter::Reading& r)
        {
            auto o = std::make_unique<juce::DynamicObject>();
            o->setProperty ("average", r.average);
            o->setProperty ("peak", r.peak);
            o->setProperty ("overruns", r.overruns);
            return o;
        };
        LoadMeter::Reading self, all;
        int tracks = 0;
        composer.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
        {
            for (auto* p : members)
            {
                const auto r = p->load.read (juce::Time::getMillisecondCounter());
                if (p == &composer)
                    self = r;
                all.average += r.average;
                all.peak = std::max (all.peak, r.peak);
                all.overruns += r.overruns;
                ++tracks;
            }
        });
        auto event = std::make_unique<juce::DynamicObject>();
        event->setProperty ("self", reading (self).release());
        auto everyone = reading (all);
        everyone->setProperty ("tracks", tracks);
        event->setProperty ("all", everyone.release());
        browser.emitEventIfBrowserIsVisible ("load", event.release());
    }

    // The listener's head (degrees), only when it moved: 30 times a second while tracking.
    if (const auto yaw = composer.headTracking->yaw(); std::abs (yaw - lastHeadYaw) > 0.01f)
    {
        lastHeadYaw = yaw;
        browser.emitEventIfBrowserIsVisible ("headYaw", yaw);
    }

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
