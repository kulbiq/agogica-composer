#include "TrackerPage.h"
#include "../../Source/bridge/Mime.h"

namespace tracker
{
namespace
{
// The page's functions and events (tracker/ui/src/bridge.ts has the same names).
namespace function
{
const juce::Identifier pageReady { "pageReady" }, setInverted { "setInverted" }, setPaused { "setPaused" },
    recenter { "recenter" }, setCameraPreviewEnabled { "setCameraPreviewEnabled" }, fitHeight { "fitHeight" };
}

namespace event
{
const juce::Identifier status { "status" }, trackingFrame { "trackingFrame" }, performance { "performance" },
    cameraPreview { "cameraPreview" }, listenerPose { "listenerPose" }, debugView { "debugView" }, about { "about" };
}

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    for (const auto& [name, value] : properties)
        o->setProperty (name, value);
    return juce::var (o.get());
}

const char* name (TrackerPhase phase)
{
    switch (phase)
    {
        case TrackerPhase::loadingModels:   return "loadingModels";
        case TrackerPhase::waitingForCamera: return "waitingForCamera";
        case TrackerPhase::compilingModels: return "compilingModels";
        case TrackerPhase::tracking:        return "tracking";
        case TrackerPhase::failed:          return "failed";
    }
    return "";
}

juce::var toJson (const TrackerStatus& status)
{
    return object ({ { "phase", name (status.phase) }, { "detail", status.detail } });
}

juce::var toJson (const FrameReport& frame)
{
    auto json = object ({ { "found", frame.head.has_value() },
                          { "reusedStillHead", frame.reusedStillHead },
                          { "changeSinceLastEstimate", frame.changeSinceLastEstimate } });
    auto* o = json.getDynamicObject();

    if (frame.warning.isNotEmpty())
        o->setProperty ("warning", frame.warning);

    if (const auto& head = frame.head)
    {
        const auto& box = head->headBoxNormalised;
        o->setProperty ("cameraYawDegrees", head->orientation.yawDegrees);
        o->setProperty ("cameraPitchDegrees", head->orientation.pitchDegrees);
        o->setProperty ("cameraRollDegrees", head->orientation.rollDegrees);
        o->setProperty ("confidence", head->confidence);
        o->setProperty ("headBox", juce::Array<juce::var> { box.getX(), box.getY(), box.getWidth(), box.getHeight() });
        if (! frame.reusedStillHead) // the models ran on this frame
        {
            o->setProperty ("detectMs", head->detectMs);
            o->setProperty ("poseMs", head->poseMs);
        }
    }
    return json;
}

juce::var toJson (const PerformanceReport& p)
{
    return object ({ { "cameraWidth", p.cameraWidth }, { "cameraHeight", p.cameraHeight },
                     { "cameraFramesPerSecond", p.cameraFramesPerSecond }, { "cpuCores", p.cpuCores },
                     { "estimatedPerSecond", p.estimatedPerSecond }, { "reusedPerSecond", p.reusedPerSecond },
                     { "droppedPerSecond", p.droppedPerSecond } });
}

// Development: AGOGICA_TRACKER_LOG=/path records every status, frame and performance report,
// after the time in ms (to 0.1 ms: with 0 decimal places JUCE writes 1.81068e+08).
void logForDevelopment (const juce::Identifier& event, const juce::var& json)
{
    static const juce::File log (juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_LOG", {}));
    if (log != juce::File())
        log.appendText (juce::String (juce::Time::getMillisecondCounterHiRes(), 1) + " " + event.toString() + " "
                        + juce::JSON::toString (json, true) + "\n");
}

std::optional<bool> boolArgument (const juce::Array<juce::var>& arguments)
{
    return arguments.size() == 1 && arguments[0].isBool() ? std::optional ((bool) arguments[0]) : std::nullopt;
}

std::optional<int> numberArgument (const juce::Array<juce::var>& arguments)
{
    const bool isNumber = arguments.size() == 1 && (arguments[0].isInt() || arguments[0].isInt64() || arguments[0].isDouble());
    return isNumber ? std::optional (juce::roundToInt ((double) arguments[0])) : std::nullopt;
}

// Only files inside the page folder.
std::optional<juce::WebBrowserComponent::Resource> serve (const juce::File& pageFolder, const juce::String& url)
{
    auto path = url.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    if (path.isEmpty())
        path = "index.html";

    const auto file = pageFolder.getChildFile (path);
    juce::MemoryBlock data;
    if (! file.isAChildOf (pageFolder) || ! file.loadFileAsData (data))
        return std::nullopt;

    const auto* bytes = static_cast<const std::byte*> (data.getData());
    return juce::WebBrowserComponent::Resource { { bytes, bytes + data.getSize() }, web::mimeType (path) };
}

juce::WebBrowserComponent::Options platformOptions()
{
    juce::WebBrowserComponent::Options options;
   #if JUCE_WINDOWS
    // WebView2 needs a folder it can write its profile to: per PC, like the licence.
    return options.withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                  .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                      .withUserDataFolder (juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                                               .getChildFile ("Agogica/Tracker/WebView2")));
   #else
    return options;
   #endif
}
} // namespace

TrackerPage::TrackerPage (juce::File folder, Actions actions)
    : pageFolder (std::move (folder)), browser (options (std::move (actions)))
{
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
}

TrackerPage::~TrackerPage()
{
    cancelPendingUpdate();
}

juce::WebBrowserComponent::Options TrackerPage::options (Actions actions)
{
    using Arguments = juce::Array<juce::var>;
    using Complete = juce::WebBrowserComponent::NativeFunctionCompletion;

    // A function taking one bool; a call with anything else does nothing.
    auto boolFunction = [] (std::function<void (bool)> action)
    {
        return [set = std::move (action)] (const Arguments& arguments, Complete complete)
        {
            if (const auto on = boolArgument (arguments))
                set (*on);
            complete ({});
        };
    };

    return platformOptions()
        .withNativeIntegrationEnabled()
        .withNativeFunction (function::pageReady, [this, initialState = actions.initialState] (const Arguments&, Complete complete)
        {
            const auto state = initialState();
            complete (object ({ { "inverted", state.inverted }, { "debugView", state.debugView }, { "version", state.version } }));

            isPageReady = true;
            {
                const juce::ScopedLock sl (pendingLock);
                isStatusUnsent = latestStatus.has_value();
            }
            triggerAsyncUpdate();
        })
        .withNativeFunction (function::setInverted, boolFunction (actions.setInverted))
        .withNativeFunction (function::setPaused, boolFunction (actions.setPaused))
        .withNativeFunction (function::setCameraPreviewEnabled, boolFunction (actions.setCameraPreviewEnabled))
        .withNativeFunction (function::recenter, [recenter = actions.recenter] (const Arguments&, Complete complete)
        {
            recenter();
            complete ({});
        })
        .withNativeFunction (function::fitHeight, [fitHeight = actions.fitHeight] (const Arguments& arguments, Complete complete)
        {
            if (const auto heightPixels = numberArgument (arguments))
                fitHeight (*heightPixels);
            complete ({});
        })
        .withResourceProvider ([this] (const juce::String& url) { return serve (pageFolder, url); });
}

void TrackerPage::report (const TrackerStatus& status)
{
    logForDevelopment (event::status, toJson (status));
    {
        const juce::ScopedLock sl (pendingLock);
        latestStatus = status;
        isStatusUnsent = true;
    }
    triggerAsyncUpdate();
}

void TrackerPage::report (const FrameReport& frame)
{
    logForDevelopment (event::trackingFrame, toJson (frame));
    {
        const juce::ScopedLock sl (pendingLock);
        pendingFrame = frame;
    }
    triggerAsyncUpdate();
}

void TrackerPage::report (const PerformanceReport& performance)
{
    logForDevelopment (event::performance, toJson (performance));
    {
        const juce::ScopedLock sl (pendingLock);
        pendingPerformance = performance;
    }
    triggerAsyncUpdate();
}

void TrackerPage::reportCameraPreview (const juce::MemoryBlock& jpeg)
{
    {
        const juce::ScopedLock sl (pendingLock);
        pendingPreviewJpeg = jpeg;
    }
    triggerAsyncUpdate();
}

void TrackerPage::handleAsyncUpdate()
{
    if (! isPageReady)
        return; // kept until pageReady asks again

    std::optional<TrackerStatus> status;
    std::optional<FrameReport> frame;
    std::optional<PerformanceReport> performance;
    juce::MemoryBlock previewJpeg;
    {
        const juce::ScopedLock sl (pendingLock);
        if (std::exchange (isStatusUnsent, false))
            status = latestStatus;
        frame = std::exchange (pendingFrame, std::nullopt);
        performance = std::exchange (pendingPerformance, std::nullopt);
        previewJpeg = std::exchange (pendingPreviewJpeg, {});
    }

    if (status)
        emit (event::status, toJson (*status));
    if (frame)
        emit (event::trackingFrame, toJson (*frame));
    if (performance)
        emit (event::performance, toJson (*performance));
    if (! previewJpeg.isEmpty())
        emit (event::cameraPreview, "data:image/jpeg;base64," + juce::Base64::toBase64 (previewJpeg.getData(), previewJpeg.getSize()));
}

void TrackerPage::showListenerPose (const ListenerPose::Reading& heard)
{
    emit (event::listenerPose, object ({ { "yawDegrees", heard.listenerHead.yawDegrees }, { "live", heard.live } }));
}

void TrackerPage::showDebugView (bool on) { emit (event::debugView, on); }
void TrackerPage::showAbout()             { emit (event::about, {}); }

void TrackerPage::emit (const juce::Identifier& eventName, const juce::var& payload)
{
    if (isPageReady)
        browser.emitEventIfBrowserIsVisible (eventName, payload);
}
} // namespace tracker
