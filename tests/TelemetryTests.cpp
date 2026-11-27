// Anonymous usage data (Source/telemetry): what is sent, and above all what never is.
// Nothing here touches the network or the user's own settings: a temp file, a fake request.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <thread>
#include "../Source/telemetry/Telemetry.h"

using namespace telemetry;

namespace
{
juce::File freshFolder (const char* name)
{
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("composer-tests").getChildFile (name);
    folder.deleteRecursively();
    folder.createDirectory();
    return folder;
}

// Stands in for the server: keeps every body it is handed.
struct Server
{
    Telemetry::Post post()
    {
        return [this] (const juce::String& body)
        {
            const juce::ScopedLock sl (lock);
            bodies.add (body);
            return up.load();
        };
    }

    juce::StringArray received() const
    {
        const juce::ScopedLock sl (lock);
        return bodies;
    }

    // Every event of every body, in order.
    juce::Array<juce::var> events() const
    {
        juce::Array<juce::var> all;
        for (const auto& body : received())
            all.addArray (*juce::JSON::parse (body)["batch"].getArray());
        return all;
    }

    juce::CriticalSection lock;
    juce::StringArray bodies;
    std::atomic<bool> up { true };
};

Usage someUsage()
{
    Usage u;
    u.instances = 12;
    u.binaural = true;
    u.roomSize = 20.0f;
    u.scenes = 2;
    u.headTracking = true;
    u.tracksInLargestPlayer = 3;
    u.sampleRate = 48000.0;
    u.blockSize = 256;
    u.loadAverage = 0.04f;
    u.loadPeak = 0.11f;
    u.overruns = 1;
    return u;
}

// A session's files look left alone (as after a restart) so the next one sends them.
void age (const juce::File& folder)
{
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.json"))
        f.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::minutes (10));
}
} // namespace

TEST_CASE ("usage data: nothing before a yes, exactly the listed things after, nothing after a no", "[integration]")
{
    const auto folder = freshFolder ("telemetry");
    const auto file = folder.getChildFile ("Composer.settings");
    Server server;

    SECTION ("not asked yet, or answered no: nothing is collected, written or sent")
    {
        {
            Telemetry t ("key", file, server.post());
            CHECK (t.consent() == Consent::ask);
            CHECK (t.installId().isEmpty());

            const auto use = [&]
            {
                t.editorOpened ("0.1.0-beta.1", "VST3", "Dorico");
                t.noteUsage (someUsage());
                t.reportError ("TypeError", "at render", "render");
                t.writeSummaryNow();
                CHECK (t.waitUntilIdle (1000));
            };

            use();
            CHECK_FALSE (file.existsAsFile());

            t.setConsent (false);
            CHECK (t.consent() == Consent::off);
            use();
        }

        CHECK (server.received().isEmpty());
        CHECK_FALSE (Settings (file).pendingFolder().exists());
        CHECK_FALSE (file.loadFileAsString().contains ("installId"));
    }

    SECTION ("a build without a key, or with DO_NOT_TRACK: unavailable, whatever the file says")
    {
        Settings (file).answer (true);

        for (const auto doNotTrack : { false, true })
        {
            Telemetry t (doNotTrack ? "key" : "", file, server.post(), {}, doNotTrack);
            CHECK (t.consent() == Consent::unavailable);
            t.setConsent (true);
            t.editorOpened ("0.1.0", "AU", "Logic Pro");
            t.reportError ("x", "y", "render");
            CHECK (t.waitUntilIdle (1000));
        }

        CHECK (server.received().isEmpty());
    }

    SECTION ("a plugin scan (instances, no window) is no session")
    {
        Settings (file).answer (true);
        {
            Telemetry t ("key", file, server.post());
            t.noteUsage (someUsage());
            t.reportError ("x", "y", "render");
            CHECK (t.waitUntilIdle (1000));
        }
        CHECK (server.received().isEmpty());
        CHECK_FALSE (Settings (file).pendingFolder().exists());
    }

    SECTION ("after a yes: the session's start now, its summary with the next session")
    {
        juce::String id;
        {
            Telemetry t ("key", file, server.post());
            t.editorOpened ("0.1.0-beta.1", "VST3", "Dorico"); // the window that asks
            t.setConsent (true);
            id = t.installId();
            CHECK (id.length() == 36);
            CHECK (t.waitUntilIdle (2000));

            const auto sent = server.events();
            REQUIRE (sent.size() == 1);
            CHECK (sent[0]["event"] == "session_start");
            CHECK (sent[0]["distinct_id"] == id);
            CHECK (juce::JSON::parse (server.received()[0])["api_key"] == "key");

            // Exactly these properties: a new one is a change to PRIVACY.md and the popup's list.
            juce::StringArray names;
            for (const auto& p : sent[0]["properties"].getDynamicObject()->getProperties())
                names.add (p.name.toString());
            names.sort (false);
            CHECK (names.joinIntoString (",") == "$process_person_profile,composer_version,cpu_architecture,host,os,plugin_format");
            CHECK (sent[0]["properties"]["composer_version"] == "0.1.0-beta.1");
            CHECK (sent[0]["properties"]["host"] == "Dorico");
            CHECK_FALSE ((bool) sent[0]["properties"]["$process_person_profile"]);

            t.noteUsage (someUsage());
            auto stereo = someUsage();
            stereo.binaural = false;
            stereo.instances = 14;
            t.noteUsage (stereo);
        } // the host closes: the summary is a file, no request

        CHECK (server.events().size() == 1);
        age (Settings (file).pendingFolder());

        Telemetry next ("key", file, server.post());
        next.editorOpened ("0.1.0-beta.1", "VST3", "Dorico");
        CHECK (next.installId() == id); // the same id from one session to the next
        CHECK (next.waitUntilIdle (2000));
        juce::Thread::sleep (50);
        CHECK (next.waitUntilIdle (2000));

        juce::var summary, audio;
        for (const auto& e : server.events())
        {
            if (e["event"] == "session_summary") summary = e["properties"];
            if (e["event"] == "audio_performance") audio = e["properties"];
        }

        REQUIRE (summary.isObject());
        REQUIRE (audio.isObject());
        CHECK ((int) summary["most_instances"] == 14);
        CHECK (summary["output_mode"] == "both");
        CHECK ((int) summary["scenes"] == 2);
        CHECK ((bool) summary["head_tracking"]);
        CHECK ((int) summary["most_tracks_in_player"] == 3);
        CHECK ((int) audio["block_size"] == 256);
        CHECK ((int) audio["overruns"] == 2);

        juce::StringArray names;
        for (const auto& p : summary.getDynamicObject()->getProperties())
            names.add (p.name.toString());
        for (const auto& p : audio.getDynamicObject()->getProperties())
            names.add (p.name.toString());
        names.sort (false);
        names.removeDuplicates (false);
        CHECK (names.joinIntoString (",")
               == "$process_person_profile,block_size,composer_version,head_tracking,host,load_average,load_peak,minutes,"
                  "most_instances,most_tracks_in_player,output_mode,overruns,plugin_format,room_size,sample_rate,scenes");

        CHECK (Settings (file).pendingFolder().findChildFiles (juce::File::findFiles, false, "*summary.json").size() <= 1); // only the new session's, if any
    }

    SECTION ("page errors: a few per session, cut to size, never a path of the user's")
    {
        Settings (file).answer (true);
        Telemetry t ("key", file, server.post());
        t.editorOpened ("0.1.0", "VST3", "MuseScore");

        const auto stack = "at paint (juce://juce.backend/assets/index.js:1:2)\n"
                           "at /Users/somebody/Projects/Secret Symphony/thing.js:3\n"
                           "at C:\\Users\\somebody\\file.js\n"
                         + juce::String::repeatedString ("x", 5000);

        for (int i = 0; i < 20; ++i)
            t.reportError ("TypeError: undefined is not an object " + juce::String (i), stack, "render");

        CHECK (t.waitUntilIdle (2000));

        int errors = 0;
        for (const auto& e : server.events())
        {
            if (e["event"] != "ui_error")
                continue;

            ++errors;
            const auto text = e["properties"]["stack"].toString();
            CHECK (text.length() <= Telemetry::maxErrorLength);
            CHECK (text.contains ("juce://juce.backend"));
            CHECK_FALSE (text.contains ("somebody"));
            CHECK_FALSE (text.contains ("Secret Symphony"));
            CHECK (e["properties"]["where"] == "render");
        }
        CHECK (errors == Telemetry::maxErrors);
    }

    SECTION ("no network: events wait in a file, and a no deletes them along with the id")
    {
        Settings (file).answer (true);
        server.up = false;
        {
            Telemetry t ("key", file, server.post());
            t.editorOpened ("0.1.0", "VST3", "Dorico");
            CHECK (t.waitUntilIdle (2000));
            juce::Thread::sleep (50);
        }

        const auto pending = Settings (file).pendingFolder();
        REQUIRE (pending.findChildFiles (juce::File::findFiles, false, "*events.json").size() == 1);

        SECTION ("the next session sends them")
        {
            age (pending);
            server.up = true;
            const auto before = server.received().size();
            Telemetry next ("key", file, server.post());
            next.editorOpened ("0.1.0", "VST3", "Dorico");
            CHECK (next.waitUntilIdle (2000));
            juce::Thread::sleep (100);
            CHECK (server.received().size() >= before + 2); // the waiting start, and this session's
            CHECK (pending.findChildFiles (juce::File::findFiles, false, "*events.json").isEmpty());
        }

        SECTION ("a no forgets them")
        {
            Telemetry next ("key", file, server.post());
            next.setConsent (false);
            CHECK_FALSE (pending.exists());
            CHECK (next.installId().isEmpty());
            CHECK_FALSE (file.loadFileAsString().contains ("installId"));
        }
    }

    SECTION ("two hosts share the answer: a no in one stops the other, and nothing brings the yes back")
    {
        Settings (file).answer (true);
        Telemetry dorico ("key", file, server.post()), museScore ("key", file, server.post());
        dorico.editorOpened ("0.1.0", "VST3", "Dorico");
        museScore.editorOpened ("0.1.0", "VST3", "MuseScore");
        CHECK (dorico.waitUntilIdle (2000));
        CHECK (museScore.waitUntilIdle (2000));
        juce::Thread::sleep (50);
        const auto before = server.received().size();

        dorico.setConsent (false);

        // The other host goes on as before: it notes usage, writes its summary, reports an error.
        museScore.noteUsage (someUsage());
        museScore.writeSummaryNow();
        museScore.reportError ("x", "y", "render");
        CHECK (museScore.waitUntilIdle (2000));
        juce::Thread::sleep (50);

        CHECK (server.received().size() == before);
        CHECK (Settings (file).consent() == Consent::off);
        CHECK (Settings (file).installId().isEmpty());
        CHECK_FALSE (Settings (file).pendingFolder().exists());
    }

    folder.deleteRecursively();
}

TEST_CASE ("usage data: a server that never answers doesn't hold the host up when it closes", "[integration]")
{
    // The real request, to a socket here that accepts and then says nothing.
    juce::StreamingSocket listener;
    REQUIRE (listener.createListener (0, "127.0.0.1"));
    std::atomic<bool> done { false };
    std::unique_ptr<juce::StreamingSocket> accepted;
    std::thread silent ([&]
    {
        while (! done)
            if (listener.waitUntilReady (true, 50) == 1)
                accepted.reset (listener.waitForNextConnection());
    });

    const auto folder = freshFolder ("telemetry-hang");
    const auto file = folder.getChildFile ("Composer.settings");
    Settings (file).answer (true);
    const auto poster = std::make_shared<HttpPoster> ("http://127.0.0.1:" + juce::String (listener.getBoundPort()) + "/");

    // Closing at any moment of the request: before it is out (a cancel that arrives while the
    // stream is still making its connection must not be lost), and while it waits.
    const auto delay = GENERATE (0, 1, 2, 3, 5, 9, 20, 300);
    const auto start = juce::Time::getMillisecondCounter();
    {
        Telemetry t ("key", file, [poster] (const juce::String& body) { return poster->post (body); }, [poster] { poster->cancel(); });
        t.editorOpened ("0.1.0", "VST3", "Dorico");
        juce::Thread::sleep (delay);
    }
    const auto took = juce::Time::getMillisecondCounter() - start;

    CHECK (took < (juce::uint32) delay + 1500); // let go at once, not after the request's own 3 s

    done = true;
    silent.join();
    folder.deleteRecursively();
}

TEST_CASE ("usage data: the real request is one POST of the JSON body, taken on a 200", "[integration]")
{
    juce::StreamingSocket listener;
    REQUIRE (listener.createListener (0, "127.0.0.1"));
    juce::String request;
    const auto status = GENERATE (200, 500);

    std::thread server ([&]
    {
        if (listener.waitUntilReady (true, 3000) != 1)
            return;

        std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());
        char bytes[4096];

        // Headers, then as many bytes of body as they announce.
        while (client != nullptr && client->waitUntilReady (true, 2000) == 1)
        {
            const auto got = client->read (bytes, sizeof (bytes), false);
            if (got <= 0)
                break;
            request += juce::String::fromUTF8 (bytes, got);

            const auto bodyAt = request.indexOf ("\r\n\r\n");
            const auto length = request.fromFirstOccurrenceOf ("Content-Length:", false, true).getIntValue();
            if (bodyAt >= 0 && request.length() - (bodyAt + 4) >= length)
                break;
        }

        const auto answer = "HTTP/1.1 " + juce::String (status) + " X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        if (client != nullptr)
            client->write (answer.toRawUTF8(), (int) answer.getNumBytesAsUTF8());
    });

    HttpPoster poster ("http://127.0.0.1:" + juce::String (listener.getBoundPort()) + "/batch/");
    const auto taken = poster.post (R"({"api_key":"key","batch":[]})");
    server.join();

    CHECK (taken == (status == 200));
    CHECK (request.startsWith ("POST /batch/ "));
    CHECK (request.containsIgnoreCase ("Content-Type: application/json"));
    CHECK (request.endsWith (R"({"api_key":"key","batch":[]})"));
}
