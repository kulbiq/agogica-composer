#include "Telemetry.h"
#include "../stage/AgogicaFolder.h"

namespace telemetry
{
namespace
{
constexpr auto endpoint = "https://eu.i.posthog.com/batch/";

#ifndef COMPOSER_TELEMETRY_KEY
 #define COMPOSER_TELEMETRY_KEY ""
#endif

bool doNotTrackIsSet()
{
    const auto value = juce::SystemStats::getEnvironmentVariable ("DO_NOT_TRACK", {});
    return value.isNotEmpty() && value != "0";
}

// A page error's text: cut to size, and without any line that names a folder of the user's
// (a stack normally holds only the page's own addresses).
juce::String scrubbed (const juce::String& text)
{
    juce::StringArray kept;

    for (const auto& line : juce::StringArray::fromLines (text))
        if (! (line.contains ("/Users/") || line.contains ("\\Users\\") || line.contains ("/home/")))
            kept.add (line);

    return kept.joinIntoString ("\n").substring (0, Telemetry::maxErrorLength);
}

juce::Array<juce::var> eventsIn (const juce::File& file)
{
    const auto parsed = juce::JSON::parse (file);
    return parsed.isArray() ? *parsed.getArray() : juce::Array<juce::var>();
}
} // namespace

// ---- Settings -------------------------------------------------------------------------

juce::var Settings::read() const
{
    return juce::JSON::parse (file);
}

Consent Settings::consent() const
{
    const auto answer = read()["telemetry"].toString();
    return answer == "on" ? Consent::on : answer == "off" ? Consent::off : Consent::ask;
}

juce::String Settings::installId() const
{
    const auto saved = read();
    return saved["telemetry"].toString() == "on" ? saved["installId"].toString() : juce::String();
}

void Settings::answer (bool yes)
{
    auto saved = std::make_unique<juce::DynamicObject>();
    saved->setProperty ("telemetry", yes ? "on" : "off");

    if (yes)
    {
        // The id stays the same from one yes to the next host; a no forgets it for good.
        const auto id = installId();
        saved->setProperty ("installId", id.isNotEmpty() ? id : juce::Uuid().toDashedString());
    }
    else
    {
        pendingFolder().deleteRecursively();
    }

    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (saved.release()))); // a temp file, then a rename
}

// ---- HttpPoster -----------------------------------------------------------------------

bool HttpPoster::post (const juce::String& body)
{
    juce::WebInputStream stream (juce::URL (url).withPOSTData (body), true);
    stream.withExtraHeaders ("Content-Type: application/json").withConnectionTimeout (timeoutMs).withNumRedirectsToFollow (0);

    {
        const juce::ScopedLock sl (lock);
        if (cancelled)
            return false;
        waiting = &stream;
    }

    const auto taken = stream.connect (nullptr) && stream.getStatusCode() == 200;

    const juce::ScopedLock sl (lock);
    waiting = nullptr;
    return taken;
}

void HttpPoster::cancel()
{
    const juce::ScopedLock sl (lock);
    cancelled = true;

    if (waiting != nullptr)
        waiting->cancel();
}

// ---- Telemetry ------------------------------------------------------------------------

Telemetry::Telemetry()
    : Telemetry (COMPOSER_TELEMETRY_KEY, agogicaFolder().getChildFile ("Composer.settings"), {}, {}, doNotTrackIsSet())
{
    const auto poster = std::make_shared<HttpPoster> (endpoint);
    post = [poster] (const juce::String& body) { return poster->post (body); };
    cancelPost = [poster] { poster->cancel(); };
}

Telemetry::Telemetry (juce::String projectKey, juce::File settingsFile, Post p, std::function<void()> cancel, bool doNotTrack)
    : Thread ("Composer usage data"),
      key (std::move (projectKey)),
      settings (std::move (settingsFile)),
      post (std::move (p)),
      cancelPost (std::move (cancel)),
      disabled (doNotTrack || key.isEmpty())
{
}

Telemetry::~Telemetry()
{
    writeSummaryNow(); // a file; the next session sends it

    signalThreadShouldExit();
    if (cancelPost)
        cancelPost();
    notify();
    stopThread (HttpPoster::timeoutMs + 2000); // longer than a request can wait: the thread ends by itself
}

Consent Telemetry::consent() const
{
    if (disabled)
        return Consent::unavailable;

    // Every window asks 30 times a second; another host may have changed the answer meanwhile.
    const juce::ScopedLock sl (lock);
    const auto now = juce::Time::getMillisecondCounter();

    if (! consentRead || now - consentReadAt > 2000)
    {
        knownConsent = settings.consent();
        consentReadAt = now;
        consentRead = true;
    }

    return knownConsent;
}

void Telemetry::setConsent (bool yes)
{
    if (disabled)
        return;

    settings.answer (yes);

    const juce::ScopedLock sl (lock);
    knownConsent = yes ? Consent::on : Consent::off;
    consentReadAt = juce::Time::getMillisecondCounter();
    consentRead = true;

    if (! yes)
    {
        queue.clear();
        sessionStarted = false;
    }
    else if (editorSeen && ! sessionStarted)
    {
        startSession();
    }
}

void Telemetry::editorOpened (const juce::String& v, const juce::String& f, const juce::String& h)
{
    if (disabled)
        return;

    const juce::ScopedLock sl (lock);
    version = v;
    format = f;
    host = h;
    editorSeen = true;

    if (! sessionStarted && settings.consent() == Consent::on)
        startSession();
}

void Telemetry::startSession()
{
    sessionStarted = true;
    sessionStartMs = lastUsageMs = juce::Time::currentTimeMillis();
    sessionName = juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()); // its files' name: says nothing
    summaryWrittenMs = 0;
    errorsReported = 0;
    capture ("session_start", new juce::DynamicObject());
}

void Telemetry::capture (const juce::String& event, juce::DynamicObject::Ptr properties)
{
    // What every event says about where it comes from. No person is made of it on the server.
    properties->setProperty ("composer_version", version);
    properties->setProperty ("plugin_format", format);
    properties->setProperty ("host", host);
    properties->setProperty ("os", juce::SystemStats::getOperatingSystemName());
   #if JUCE_ARM
    properties->setProperty ("cpu_architecture", "arm64");
   #else
    properties->setProperty ("cpu_architecture", "x86_64");
   #endif
    properties->setProperty ("$process_person_profile", false);

    auto e = std::make_unique<juce::DynamicObject>();
    e->setProperty ("event", event);
    e->setProperty ("timestamp", juce::Time::getCurrentTime().toISO8601 (true));
    e->setProperty ("properties", juce::var (properties.get()));

    const juce::ScopedLock sl (lock);

    if (queue.size() < maxPendingEvents)
        queue.add (juce::var (e.release()));

    wake();
}

void Telemetry::wake()
{
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::background);

    notify();
}

void Telemetry::reportError (const juce::String& message, const juce::String& stack, const juce::String& where)
{
    if (disabled)
        return;

    {
        const juce::ScopedLock sl (lock);

        if (! sessionStarted || errorsReported >= maxErrors || settings.consent() != Consent::on)
            return;

        ++errorsReported;
    }

    juce::DynamicObject::Ptr properties = new juce::DynamicObject();
    properties->setProperty ("message", scrubbed (message));
    properties->setProperty ("stack", scrubbed (stack));
    properties->setProperty ("where", where.substring (0, 32));
    capture ("ui_error", properties);
}

void Telemetry::noteUsage (const Usage& u)
{
    if (disabled)
        return;

    auto due = false;

    {
        const juce::ScopedLock sl (lock);

        if (! sessionStarted)
            return;

        mostInstances = std::max (mostInstances, u.instances);
        mostTracksInPlayer = std::max (mostTracksInPlayer, u.tracksInLargestPlayer);
        (u.binaural ? usedBinaural : usedStereo) = true;
        headTracking = headTracking || u.headTracking;
        roomSize = u.roomSize;
        scenes = u.scenes;
        sampleRate = u.sampleRate;
        blockSize = u.blockSize;
        loadSum += (double) u.loadAverage;
        loadPeak = std::max (loadPeak, u.loadPeak);
        overruns += u.overruns;
        ++usageNotes;

        lastUsageMs = juce::Time::currentTimeMillis();
        due = lastUsageMs - summaryWrittenMs > 60000;
    }

    if (due)
        writeSummaryNow();
}

juce::var Telemetry::summaryEvents() const
{
    const auto event = [this] (const char* name, std::unique_ptr<juce::DynamicObject> properties)
    {
        properties->setProperty ("composer_version", version);
        properties->setProperty ("plugin_format", format);
        properties->setProperty ("host", host);
        properties->setProperty ("$process_person_profile", false);

        auto e = std::make_unique<juce::DynamicObject>();
        e->setProperty ("event", name);
        e->setProperty ("timestamp", juce::Time (lastUsageMs).toISO8601 (true));
        e->setProperty ("properties", juce::var (properties.release()));
        return juce::var (e.release());
    };

    auto session = std::make_unique<juce::DynamicObject>();
    session->setProperty ("minutes", juce::roundToInt ((double) (lastUsageMs - sessionStartMs) / 60000.0));
    session->setProperty ("most_instances", mostInstances);
    session->setProperty ("output_mode", usedStereo && usedBinaural ? "both" : usedBinaural ? "binaural" : "stereo");
    session->setProperty ("room_size", roomSize);
    session->setProperty ("scenes", scenes);
    session->setProperty ("head_tracking", headTracking);
    session->setProperty ("most_tracks_in_player", mostTracksInPlayer);

    auto audio = std::make_unique<juce::DynamicObject>();
    audio->setProperty ("sample_rate", sampleRate);
    audio->setProperty ("block_size", blockSize);
    audio->setProperty ("load_average", usageNotes > 0 ? loadSum / usageNotes : 0.0);
    audio->setProperty ("load_peak", loadPeak);
    audio->setProperty ("overruns", overruns);

    return juce::Array<juce::var> { event ("session_summary", std::move (session)), event ("audio_performance", std::move (audio)) };
}

juce::File Telemetry::sessionFile (const char* kind) const
{
    return settings.pendingFolder().getChildFile (sessionName + "-" + kind + ".json");
}

void Telemetry::writeSummaryNow()
{
    if (disabled)
        return;

    const juce::ScopedLock sl (lock);

    if (! sessionStarted || usageNotes == 0 || settings.consent() != Consent::on)
        return;

    summaryWrittenMs = juce::Time::currentTimeMillis();
    settings.pendingFolder().createDirectory();
    sessionFile ("summary").replaceWithText (juce::JSON::toString (summaryEvents()));
}

bool Telemetry::send (const juce::Array<juce::var>& events)
{
    // The answer as it is on disk now: another host may have withdrawn it.
    const auto id = settings.installId();

    if (id.isEmpty() || events.isEmpty() || ! post)
        return false;

    juce::Array<juce::var> batch;

    for (const auto& e : events)
    {
        if (auto* object = e.getDynamicObject())
        {
            auto copy = object->clone();
            copy->setProperty ("distinct_id", id);
            batch.add (juce::var (copy.release()));
        }
    }

    auto body = std::make_unique<juce::DynamicObject>();
    body->setProperty ("api_key", key);
    body->setProperty ("batch", batch);
    return post (juce::JSON::toString (juce::var (body.release()), true));
}

void Telemetry::keepForLater (const juce::Array<juce::var>& events)
{
    if (settings.consent() != Consent::on)
        return;

    const auto file = [this] { const juce::ScopedLock sl (lock); return sessionFile ("events"); }();
    auto all = eventsIn (file);
    all.addArray (events);
    all.removeRange (0, all.size() - maxPendingEvents); // the newest, if there are too many

    settings.pendingFolder().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (all)));
}

void Telemetry::sendLeftovers()
{
    const auto ours = [this] { const juce::ScopedLock sl (lock); return sessionName + "-"; }();
    const auto now = juce::Time::getCurrentTime();

    for (const auto& file : settings.pendingFolder().findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        if (threadShouldExit() || settings.consent() != Consent::on)
            return;

        if (file.getFileName().startsWith (ours) || (now - file.getLastModificationTime()).inMilliseconds() < leftAloneMs)
            continue;

        const auto events = eventsIn (file);

        // Sent, or not events at all: either way the file has done its job.
        if (events.isEmpty() || send (events))
            file.deleteFile();
        else
            return; // no network now: the rest waits too
    }
}

void Telemetry::run()
{
    sendLeftovers();

    while (! threadShouldExit())
    {
        juce::Array<juce::var> batch;

        {
            const juce::ScopedLock sl (lock);
            batch.swapWith (queue);
            busy = ! batch.isEmpty();
        }

        if (batch.isEmpty())
        {
            wait (-1);
            continue;
        }

        if (! send (batch) && ! threadShouldExit())
            keepForLater (batch);

        const juce::ScopedLock sl (lock);
        busy = false;
    }
}

bool Telemetry::waitUntilIdle (int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (juce::Time::getMillisecondCounter() < until)
    {
        {
            const juce::ScopedLock sl (lock);
            if (queue.isEmpty() && ! busy)
                return true;
        }
        juce::Thread::sleep (2);
    }

    return false;
}
} // namespace telemetry
