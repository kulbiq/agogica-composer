#include "PluginEditor.h"
#include "bridge/StageSnapshot.h"
#include "stage/Scenes.h"
#include "bridge/PageProtocol.h"
#include "bridge/UiResources.h"

namespace
{
juce::String hostName()
{
    return juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFileNameWithoutExtension();
}

juce::String formatName (const juce::AudioProcessor& processor)
{
    return juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType);
}

juce::var getBuildInfo (const ComposerProcessor& processor)
{
    auto info = std::make_unique<juce::DynamicObject>();
    info->setProperty ("version", COMPOSER_VERSION_DISPLAY); // with "-beta.1" or "-dev", see CMakeLists.txt
    info->setProperty ("format", formatName (processor));
    info->setProperty ("host", hostName());

    if (const auto id = processor.telemetry->installId(); id.isNotEmpty())
        info->setProperty ("installId", id); // shown in "About & privacy" while sharing is on
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
                    .withNativeFunction (page::getStage, [this] (const auto&, auto complete)
                                         {
                                             complete (stage::snapshot (composer));
                                         })
                    .withNativeFunction (page::edit, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 1)
                                                 gesturesBegunHere.applyEdit (composer, args[0]);

                                             complete ({});
                                         })
                    .withNativeFunction (page::setName, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 2 && args[0].isInt() && args[1].isString())
                                                 stage::setName (composer, (int) args[0], args[1].toString());

                                             complete ({});
                                         })
                    .withNativeFunction (page::setTrackName, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 2 && args[0].isInt() && args[1].isString())
                                                 stage::setTrackName (composer, (int) args[0], args[1].toString());

                                             complete ({});
                                         })
                    .withNativeFunction (page::setIcon, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 2 && args[0].isInt() && args[1].isString())
                                                 stage::setIcon (composer, (int) args[0], args[1].toString());

                                             complete ({});
                                         })
                    .withNativeFunction (page::setHeadTracking, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 1 && args[0].isBool())
                                                 stage::setHeadTracking (composer, (bool) args[0]);

                                             complete ({});
                                         })
                    .withNativeFunction (page::setAudience, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 1 && args[0].isString())
                                                 stage::setAudience (composer, args[0].toString());

                                             complete ({});
                                         })
                    .withNativeFunction (page::joinPlayer, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 2 && args[0].isInt() && args[1].isInt())
                                                 stage::joinPlayer (composer, (int) args[0], (int) args[1]);

                                             complete ({});
                                         })
                    .withNativeFunction (page::splitPlayer, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 1 && args[0].isInt())
                                                 stage::splitPlayer (composer, (int) args[0]);

                                             complete ({});
                                         })
                    .withNativeFunction (page::setScene, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 1)
                                                 stage::setScene (composer, args[0].toString());

                                             complete ({});
                                         })
                    .withNativeFunction (page::startTracker, [this] (const auto&, auto complete)
                                         {
                                             trackerLauncher.start ([complete] (bool started) { complete (started); });
                                         })
                    .withNativeFunction (page::getBuildInfo, [this] (const auto&, auto complete)
                                         {
                                             complete (getBuildInfo (composer));
                                         })
                    // The web page draws its own resize grip (JUCE's corner resizer would sit
                    // under the native webview); hosts like Ableton rely on it.
                    .withNativeFunction (page::setEditorSize, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             // Two numbers that are numbers, within what an int holds (the
                                             // resize limits then apply).
                                             const auto isSize = [] (const juce::var& v)
                                             {
                                                 return (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite ((double) v)
                                                        && (double) v >= 0.0 && (double) v <= 100000.0;
                                             };

                                             if (args.size() == 2 && isSize (args[0]) && isSize (args[1]))
                                                 setBoundsConstrained (getBounds().withSize ((int) args[0], (int) args[1]));

                                             complete ({});
                                         })
                    .withNativeFunction (page::setTelemetry, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 1 && args[0].isBool())
                                                 composer.telemetry->setConsent ((bool) args[0]);

                                             complete ({});
                                         })
                    .withNativeFunction (page::reportError, [this] (const juce::Array<juce::var>& args, auto complete)
                                         {
                                             if (args.size() == 3 && args[0].isString() && args[1].isString() && args[2].isString())
                                                 composer.telemetry->reportError (args[0].toString(), args[1].toString(), args[2].toString());

                                             complete ({});
                                         })
                    .withResourceProvider (web::uiResource, resourceOrigin) }
{
    // A window: this host process is a session now (scans and renders open none).
    composer.telemetry->editorOpened (COMPOSER_VERSION_DISPLAY, formatName (composer), hostName());

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

void ComposerEditor::timerCallback()
{
    trackerLauncher.poll();

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

    browser.emitEventIfBrowserIsVisible (page::levels, levels.release());

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
        noteUsage (all, tracks);

        auto event = std::make_unique<juce::DynamicObject>();
        event->setProperty ("self", reading (self).release());
        auto everyone = reading (all);
        everyone->setProperty ("tracks", tracks);
        event->setProperty ("all", everyone.release());
        browser.emitEventIfBrowserIsVisible (page::load, event.release());
    }

    // The listener's head (degrees), only when it moved: 30 times a second while tracking.
    if (const auto yaw = composer.headTracking->yaw(); std::abs (yaw - lastHeadYaw) > 0.01f)
    {
        lastHeadYaw = yaw;
        browser.emitEventIfBrowserIsVisible (page::headYaw, yaw);
    }

    // Automation playback and other changes reach the UI here; it only draws them.
    const auto stage = stage::snapshot (composer);

    if (auto json = juce::JSON::toString (stage, true); json != lastStage)
    {
        lastStage = std::move (json);
        browser.emitEventIfBrowserIsVisible (page::stage, stage);
    }
}

// What the session's summary is made of (telemetry/Telemetry.h; nothing happens without a yes).
void ComposerEditor::noteUsage (const LoadMeter::Reading& load, int tracks)
{
    if (composer.telemetry->consent() != telemetry::Consent::on)
        return;

    telemetry::Usage usage;
    usage.instances = tracks;
    usage.binaural = composer.getParam (params::outputMode) > 0.5f;
    usage.roomSize = composer.getParam (params::roomSize);
    usage.headTracking = composer.headTracking->isLive() && composer.isHeadTrackingOn();
    usage.sampleRate = composer.getSampleRate();
    usage.blockSize = composer.getBlockSize();
    usage.loadAverage = (float) load.average;
    usage.loadPeak = (float) load.peak;
    usage.overruns = (int) load.overruns;

    composer.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        juce::StringArray scenes, players;

        for (auto* p : members)
        {
            scenes.addArray (p->getStoredScenes());
            scenes.add (p->getScene());

            if (const auto key = p->getPlayerKey(); key.isNotEmpty())
                players.add (key);
        }

        scenes.removeDuplicates (false);
        usage.scenes = scenes.size();
        usage.tracksInLargestPlayer = 1;

        for (const auto& key : players)
            usage.tracksInLargestPlayer = std::max (usage.tracksInLargestPlayer, (int) std::count (players.begin(), players.end(), key));
    });

    composer.telemetry->noteUsage (usage);
}

void ComposerEditor::resized()
{
    browser.setBounds (getLocalBounds());
}
