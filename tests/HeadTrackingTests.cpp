#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "../Source/PluginProcessor.h"
#include "../Source/dsp/DirectPath.h"
#include "../Source/dsp/EarlyReflections.h"
#include "TrackingFileOverride.h"

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

Pose pose (float yaw, bool face = true, std::uint64_t ageMs = 0)
{
    return { yaw, 0.0f, 0.0f, (std::uint64_t) juce::Time::currentTimeMillis() - ageMs, face };
}

void set (ComposerProcessor& p, const char* id, float v)
{
    auto* q = p.state.getParameter (id);
    q->setValueNotifyingHost (q->convertTo0to1 (v));
}
} // namespace

TEST_CASE ("head tracking file: round trip, replaced file picked up, missing/short/foreign ignored", "[integration]")
{
    SECTION ("write, read")
    {
        const auto file = tempFile ("roundtrip");
        Writer writer (file);
        REQUIRE (writer.isOpen());
        CHECK (file.getSize() == fileSize);

        Reader reader (file, 0); // looks at the file on every read
        writer.write (pose (12.5f));
        const auto read = reader.read();
        REQUIRE (read.has_value());
        CHECK_THAT (read->yaw, WithinAbs (12.5, 0.0));
        CHECK (read->face);
    }

    SECTION ("a file replaced under the reader is picked up")
    {
        const auto file = tempFile ("replaced");
        Reader reader (file, 0); // looks at the file on every read

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

    SECTION ("a writer killed mid-write (odd sequence left in the file) doesn't block readers")
    {
        const auto file = tempFile ("odd-sequence");
        {
            Writer crashed (file);
            crashed.write (pose (5.0f));
        }

        // Leave the counter odd, as a tracker killed between its two stores would.
        {
            juce::MemoryMappedFile map (file, juce::MemoryMappedFile::readWrite, false);
            REQUIRE (map.getData() != nullptr);
            auto* sequence = static_cast<std::uint32_t*> (map.getData()) + 2; // offset 8
            *sequence |= 1u;
        }

        Writer restarted (file);
        Reader reader (file, 0); // looks at the file on every read
        restarted.write (pose (20.0f));
        const auto read = reader.read();
        REQUIRE (read.has_value());
        CHECK_THAT (read->yaw, WithinAbs (20.0, 0.0));
    }

    SECTION ("the file is looked at only every so often (not on every 10 ms poll)")
    {
        const auto file = tempFile ("throttled");
        Reader reader (file, 50);
        CHECK_FALSE (reader.read().has_value()); // missing: looked at now

        Writer writer (file);
        writer.write (pose (7.0f));
        CHECK_FALSE (reader.read().has_value()); // not looked at again yet

        juce::Thread::sleep (60);
        const auto read = reader.read();
        REQUIRE (read.has_value());
        CHECK_THAT (read->yaw, WithinAbs (7.0, 0.0));
    }

    SECTION ("missing, short and foreign files are ignored")
    {
        const auto file = tempFile ("garbage");
        Reader reader (file, 0); // looks at the file on every read
        CHECK_FALSE (reader.read().has_value()); // missing

        file.replaceWithText ("short");
        CHECK_FALSE (reader.read().has_value());

        juce::MemoryBlock junk (fileSize);
        juce::Random (3).fillBitsRandomly (junk.getData(), junk.getSize());
        file.replaceWithData (junk.getData(), junk.getSize());
        CHECK_FALSE (reader.read().has_value());
    }
}

TEST_CASE ("head tracking file: never a torn read (seqlock)", "[integration]")
{
    const auto file = tempFile ("seqlock");
    Writer writer (file);
    Reader reader (file, 0); // looks at the file on every read

    std::atomic<bool> stop { false };
    std::atomic<int> writes { 0 };
    std::thread hammer ([&]
    {
        for (float k = 0.0f; ! stop; k += 1.0f, ++writes)
            writer.write ({ k, k, k, (std::uint64_t) k, true }); // all fields equal in every write
    });

    while (writes < 100) // the writer is running
        std::this_thread::yield();

    // Most reads land mid-write and return nothing; read until enough complete ones were seen.
    int consistent = 0;
    for (int i = 0; i < 5'000'000 && consistent < 1000; ++i)
        if (const auto p = reader.read())
        {
            REQUIRE (juce::exactlyEqual (p->yaw, p->pitch));
            REQUIRE (juce::exactlyEqual (p->yaw, p->roll));
            REQUIRE ((std::uint64_t) p->yaw == p->timestampMs);
            ++consistent;
        }

    stop = true;
    hammer.join();
    CHECK (consistent == 1000);
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
            path.process (history, &l, &r, 1);
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

TEST_CASE ("head yaw: any angle is the same direction modulo 360 degrees (reflections included)")
{
    // Stages place sources at azimuth - yaw, which can leave -180..180: a player behind and a
    // head turned far must still put every reflection (the pan-only corners too) on its side.
    auto render = [] (float headYaw)
    {
        History history;
        history.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * 48000.0));
        EarlyReflections reflections;
        reflections.prepare (48000.0);

        Acoustics a;
        a.mode = OutputMode::binaural;
        a.hall = &halls.back(); // Church: strong corner reflections
        a.player = { 3.0f, -6.0f };
        a.headYaw = headYaw;
        reflections.setAcoustics (a);

        juce::Random rng (8);
        std::vector<float> out;
        for (int i = 0; i < 9600; ++i)
        {
            float l = 0.0f, r = 0.0f;
            history.write (rng.nextFloat() - 0.5f);
            reflections.processSample (history, l, r);
            out.push_back (l), out.push_back (r);
        }
        return out;
    };

    const auto [yaw, same] = GENERATE (table<float, float> ({ { 170.0f, -190.0f }, { -150.0f, 210.0f }, { 100.0f, -260.0f } }));
    const auto a = render (yaw), b = render (same);

    for (size_t i = 0; i < a.size(); ++i)
        REQUIRE_THAT (a[i], WithinAbs (b[i], 1e-5));

    SECTION ("a pan-only placer folds a wrapped angle to the correct side")
    {
        auto gains = [] (float azimuth)
        {
            Placer placer;
            placer.prepare (48000.0, Placer::Binaural::panOnly);
            placer.setDirection (azimuth, true);
            float l = 0.0f, r = 0.0f;
            placer.process (1.0f, l, r);
            return std::pair { l, r };
        };

        const auto [l1, r1] = gains (260.0f); // = -100: behind, to the left
        const auto [l2, r2] = gains (-100.0f);
        CHECK_THAT (l1, WithinAbs (l2, 1e-6));
        CHECK_THAT (r1, WithinAbs (r2, 1e-6));
        CHECK (l1 > r1);
    }
}

TEST_CASE ("head tracking: live only in binaural, never in exports", "[integration]")
{
    const auto file = tempFile ("processor");
    Writer writer (file);
    const TrackingFileOverride trackingFile (file);

    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    set (p, "hall", 0.0f);    // the direct sound alone
    set (p, "playerY", 5.0f); // straight ahead
    set (p, "outputMode", 1.0f);

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

    CHECK (leftToRight (false) > 6.0);           // live: the player moved to the left
    CHECK (std::abs (leftToRight (true)) < 3.0); // export: still straight ahead

    set (p, "outputMode", 0.0f);
    CHECK (std::abs (leftToRight (false)) < 1.0); // stereo: tracking ignored
}

TEST_CASE ("head tracking: stale or faceless data is not live, the scene eases back ahead", "[integration]")
{
    const auto file = tempFile ("stale");
    Writer writer (file);
    const TrackingFileOverride trackingFile (file);

    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    set (p, "outputMode", 1.0f); // tracking is only read while some instance is binaural
    auto& tracking = *p.headTracking;

    writer.write (pose (90.0f));
    tracking.pollNow();
    REQUIRE (tracking.isLive());
    CHECK_THAT (tracking.yaw(), WithinAbs (90.0, 1e-4));

    SECTION ("older than 0.5 s: the tracker stopped")
    {
        writer.write (pose (60.0f, true, 1000));
    }

    SECTION ("no face: the listener left")
    {
        writer.write (pose (60.0f, false));
    }

    tracking.pollNow();
    CHECK_FALSE (tracking.isLive());
    CHECK (tracking.yaw() < 90.0f); // never jumps to the stale reading (60)
    CHECK (tracking.yaw() > 60.0f);

    for (int i = 0; i < 100; ++i) // 1 s of polls at 10 ms
        tracking.pollNow();
    CHECK_THAT (tracking.yaw(), WithinAbs (0.0, 0.01));
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
