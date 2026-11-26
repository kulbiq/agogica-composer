#pragma once

#include <functional>
#include <juce_core/juce_core.h>

// Anonymous usage data (PRIVACY.md), only after the user said yes. One per host process.
//
// The rules, all of them here:
// - Nothing is collected, written or sent before a yes. A build without a project key
//   (COMPOSER_TELEMETRY_KEY, CMake), or run with DO_NOT_TRACK set, never asks and never sends.
// - The answer belongs to the user of this computer, not to a project: one small file next to
//   the head-tracking file, shared by every host and plugin format. It is read from disk each
//   time it matters (another host may have changed it) and written only by setConsent.
// - A session is a host process in which a Composer window was opened: plugin scans and
//   offline renders open none, so they are nothing.
// - Never names (tracks, players, projects), places, sound or paths.
// - The network is used by one background thread, never at shutdown: what a session leaves
//   behind (its summary, anything a failed request held) is a file that the next session sends.
// - Everything fails silently: a sandboxed host that allows neither the file nor the network
//   just means nothing is sent.
namespace telemetry
{
enum class Consent { unavailable, ask, on, off };

// The answer and the random id events are sent under (made on a yes, deleted on a no).
class Settings
{
public:
    explicit Settings (juce::File settingsFile) : file (std::move (settingsFile)) {}

    Consent consent() const;       // ask, on or off
    juce::String installId() const; // empty unless on
    void answer (bool yes);

    // Events not sent yet, one file per session (see Telemetry); a no deletes them.
    juce::File pendingFolder() const { return file.getSiblingFile ("telemetry-pending"); }

private:
    juce::var read() const;
    juce::File file;
};

// What the editor's timer tells about the stage now and then; folded into the session's summary.
struct Usage
{
    int instances = 0;
    bool binaural = false;
    float roomSize = 0.0f;
    int scenes = 0;
    bool headTracking = false;
    int tracksInLargestPlayer = 0;
    double sampleRate = 0.0;
    int blockSize = 0;
    float loadAverage = 0.0f, loadPeak = 0.0f; // all tracks' share of one core
    int overruns = 0;
};

// The real request: one HTTPS POST of a JSON body. cancel() lets go of one that is waiting and
// refuses any later one (a host is closing: its thread must end by itself, soon).
class HttpPoster
{
public:
    explicit HttpPoster (juce::String urlToPostTo) : url (std::move (urlToPostTo)) {}
    bool post (const juce::String& body);
    void cancel();

    static constexpr int timeoutMs = 3000;

private:
    const juce::String url;
    juce::CriticalSection lock;
    juce::WebInputStream* waiting = nullptr;
    bool cancelled = false;
};

class Telemetry final : private juce::Thread
{
public:
    // Posts one JSON body; true when the server took it. May block for a few seconds.
    using Post = std::function<bool (const juce::String& body)>;

    Telemetry(); // this build's key, the user's settings file, HTTPS to the project
    // `cancelPost`: called when the process is closing, to let go of a request that is waiting.
    Telemetry (juce::String projectKey, juce::File settingsFile, Post post, std::function<void()> cancelPost = {}, bool doNotTrack = false);
    ~Telemetry() override;

    Consent consent() const;
    juce::String installId() const { return consent() == Consent::on ? settings.installId() : juce::String(); }
    void setConsent (bool yes);

    // A Composer window opened: the session starts with the first one.
    void editorOpened (const juce::String& version, const juce::String& format, const juce::String& host);
    void noteUsage (const Usage&);
    // An error of the page: at most `maxErrors` a session, each text cut to `maxErrorLength`.
    void reportError (const juce::String& message, const juce::String& stack, const juce::String& where);

    static constexpr int maxErrors = 5, maxErrorLength = 2000, maxPendingEvents = 50;

    // The summary written now instead of within a minute (the destructor; tests).
    void writeSummaryNow();
    // Tests: until the thread has nothing left to do; false if that took longer.
    bool waitUntilIdle (int timeoutMs);

    // Files of other sessions are sent once they have been left alone this long (a session
    // still running rewrites its summary every minute).
    static constexpr int leftAloneMs = 3 * 60 * 1000;

private:
    void run() override;
    void startSession();
    void capture (const juce::String& event, juce::DynamicObject::Ptr properties);
    juce::var summaryEvents() const;
    juce::File sessionFile (const char* kind) const;
    bool send (const juce::Array<juce::var>& events);

    void wake();
    void sendLeftovers();
    void keepForLater (const juce::Array<juce::var>& events);

    const juce::String key;
    Settings settings;
    Post post;
    std::function<void()> cancelPost;
    const bool disabled;

    juce::CriticalSection lock; // everything below
    juce::Array<juce::var> queue; // events waiting for the thread
    bool editorSeen = false, sessionStarted = false, busy = false;
    mutable Consent knownConsent = Consent::ask; // the file is read at most every few seconds for drawing
    mutable juce::uint32 consentReadAt = 0;
    mutable bool consentRead = false;
    juce::String version, format, host, sessionName;
    juce::int64 sessionStartMs = 0, lastUsageMs = 0, summaryWrittenMs = 0;
    int errorsReported = 0;

    // The summary so far.
    int mostInstances = 0, mostTracksInPlayer = 0, scenes = 0, blockSize = 0, overruns = 0, usageNotes = 0;
    bool usedStereo = false, usedBinaural = false, headTracking = false;
    float roomSize = 0.0f, loadPeak = 0.0f;
    double loadSum = 0.0, sampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Telemetry)
};
} // namespace telemetry
