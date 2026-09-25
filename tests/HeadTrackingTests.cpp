#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/PluginProcessor.h"
#include "../Source/dsp/DirectPath.h"

using Catch::Matchers::WithinAbs;
using namespace headtracking;

namespace
{
juce::File tempFile (const char* name)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("composer-tests").getChildFile (name);
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    return f;
}

void setTrackingFileOverride (const char* path)
{
   #if JUCE_WINDOWS
    _putenv_s ("AGOGICA_HEAD_TRACKING_FILE", path);
   #else
    if (*path != 0)
        setenv ("AGOGICA_HEAD_TRACKING_FILE", path, 1);
    else
        unsetenv ("AGOGICA_HEAD_TRACKING_FILE");
   #endif
}

Pose pose (float yaw, bool face = true)
{
    return { yaw, 0.0f, 0.0f, (std::uint64_t) juce::Time::currentTimeMillis(), face };
}
} // namespace

TEST_CASE ("head tracking file: write, read")
{
    const auto file = tempFile ("roundtrip");
    Writer writer (file);
    REQUIRE (writer.isOpen());
    CHECK (file.getSize() == fileSize);

    Reader reader (file);
    writer.write (pose (12.5f));
    const auto read = reader.read();
    REQUIRE (read.has_value());
    CHECK_THAT (read->yaw, WithinAbs (12.5, 0.0));
    CHECK (read->face);
}

TEST_CASE ("head tracking file: never a torn read (seqlock)")
{
    const auto file = tempFile ("seqlock");
    Writer writer (file);
    Reader reader (file);

    std::atomic<bool> stop { false };
    std::thread hammer ([&]
    {
        for (float k = 0.0f; ! stop; k += 1.0f)
            writer.write ({ k, k, k, (std::uint64_t) k, true }); // all fields equal in every write
    });

    int consistent = 0;
    for (int i = 0; i < 200000; ++i)
        if (const auto p = reader.read())
        {
            REQUIRE (juce::exactlyEqual (p->yaw, p->pitch));
            REQUIRE (juce::exactlyEqual (p->yaw, p->roll));
            REQUIRE ((std::uint64_t) p->yaw == p->timestampMs);
            ++consistent;
        }

    stop = true;
    hammer.join();
    CHECK (consistent > 1000);
}

TEST_CASE ("head tracking file: missing, short and foreign files are ignored")
{
    const auto file = tempFile ("garbage");
    Reader reader (file);
    CHECK_FALSE (reader.read().has_value()); // missing

    file.replaceWithText ("short");
    CHECK_FALSE (reader.read().has_value());

    juce::MemoryBlock junk (fileSize);
    juce::Random (3).fillBitsRandomly (junk.getData(), junk.getSize());
    file.replaceWithData (junk.getData(), junk.getSize());
    CHECK_FALSE (reader.read().has_value());
}

TEST_CASE ("head tracking file: a file replaced under the reader is picked up")
{
    const auto file = tempFile ("replaced");
    Reader reader (file);

    {
        Writer first (file);
        first.write (pose (10.0f));
    }
    REQUIRE (reader.read().has_value());
    CHECK_THAT (reader.read()->yaw, WithinAbs (10.0, 0.0));

    // A new tracker run: a new file (new identity) renamed over the old one.
    const auto other = tempFile ("replaced-new");
    Writer second (other);
    second.write (pose (-40.0f));
    REQUIRE (other.moveFileTo (file));

    const auto read = reader.read();
    REQUIRE (read.has_value());
    CHECK_THAT (read->yaw, WithinAbs (-40.0, 0.0));
}

TEST_CASE ("turning the head rotates the scene (binaural)")
{
    // A player 30 degrees to the right, head turned 30 degrees right, sounds like a
    // player straight ahead with the head straight.
    auto render = [] (geometry::Point player, float headYaw)
    {
        History history;
        history.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * 48000.0));
        DirectPath path;
        path.prepare (48000.0);

        Acoustics a;
        a.mode = OutputMode::binaural;
        a.player = player;
        a.headYaw = headYaw;
        path.setAcoustics (a);

        juce::Random rng (8);
        std::vector<float> out;
        for (int i = 0; i < 4800; ++i)
        {
            float l = 0.0f, r = 0.0f;
            history.write (rng.nextFloat() - 0.5f);
            path.processSample (history, l, r);
            out.push_back (l), out.push_back (r);
        }
        return out;
    };

    const auto sin30 = 5.0f * std::sin (juce::degreesToRadians (30.0f));
    const auto cos30 = 5.0f * std::cos (juce::degreesToRadians (30.0f));
    const auto turned = render ({ sin30, cos30 }, 30.0f);
    const auto ahead = render ({ 0.0f, 5.0f }, 0.0f);

    for (size_t i = 0; i < turned.size(); ++i)
        REQUIRE_THAT (turned[i], WithinAbs (ahead[i], 1e-3));
}

TEST_CASE ("head tracking: live only in binaural, never in exports")
{
    const auto file = tempFile ("processor");
    Writer writer (file);
    setTrackingFileOverride (file.getFullPathName().toRawUTF8());

    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    auto set = [&] (const char* id, float v) { auto* q = p.state.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    set ("hall", 0.0f); // the direct sound alone
    set ("playerY", 5.0f); // straight ahead
    set ("outputMode", 1.0f);

    writer.write (pose (90.0f)); // head turned right: the player is now on the left
    p.headTracking->pollNow();
    REQUIRE (p.headTracking->isLive());

    auto leftToRight = [&] (bool offline)
    {
        p.setNonRealtime (offline);
        p.setPlayConfigDetails (2, 2, 48000.0, 480);
        p.prepareToPlay (48000.0, 480);

        juce::Random rng (4);
        juce::MidiBuffer midi;
        double left = 0.0, right = 0.0;
        for (int b = 0; b < 20; ++b)
        {
            juce::AudioBuffer<float> buffer (2, 480);
            for (int i = 0; i < 480; ++i)
                buffer.setSample (0, i, rng.nextFloat() - 0.5f), buffer.setSample (1, i, 0.0f);
            p.processBlock (buffer, midi);
            for (int i = 0; i < 480; ++i)
                left += std::pow (buffer.getSample (0, i), 2.0f), right += std::pow (buffer.getSample (1, i), 2.0f);
        }
        return 10.0 * std::log10 (left / right);
    };

    CHECK (leftToRight (false) > 6.0);                 // live: the player moved to the left
    CHECK (std::abs (leftToRight (true)) < 3.0);       // export: still straight ahead

    set ("outputMode", 0.0f);
    CHECK (std::abs (leftToRight (false)) < 1.0);      // stereo: tracking ignored

    setTrackingFileOverride ("");
}

// Manual interop check with tools/fake_tracker.py (hidden):
//   AGOGICA_HEAD_TRACKING_FILE=/tmp/x python3 tools/fake_tracker.py --yaw 33 &
//   AGOGICA_HEAD_TRACKING_FILE=/tmp/x build/debug/ComposerTests "[.interop]" -s
TEST_CASE ("head tracking: read the file written by another process", "[.interop]")
{
    Reader reader (defaultFile());
    const auto p = reader.read();
    REQUIRE (p.has_value());
    UNSCOPED_INFO ("yaw " << p->yaw << ", face " << (int) p->face << ", age " << juce::Time::currentTimeMillis() - (juce::int64) p->timestampMs << " ms");
    CHECK (p->face);
}

// Manual: starts the installed Agogica Tracker (opens a window, uses the camera).
TEST_CASE ("head tracking: start the tracker app", "[.start]")
{
    CHECK (HeadTracking::startTracker());
}
