#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../tracker/tracking/ListenerPose.h"

using Catch::Matchers::WithinAbs;
using tracker::HeadOrientation;
using tracker::ListenerPose;

namespace
{
HeadOrientation facing (float yawDegrees) { return { yawDegrees, 0.0f, 0.0f }; }
float heardYaw (const ListenerPose& pose) { return pose.current().listenerHead.yawDegrees; }
} // namespace

TEST_CASE ("listener pose: Composer hears the head the camera sees")
{
    ListenerPose pose;
    CHECK_FALSE (pose.current().live); // nothing seen yet

    pose.observe (HeadOrientation { 30.0f, -5.0f, 2.0f });
    const auto heard = pose.current();
    CHECK (heard.live);
    CHECK_THAT (heard.listenerHead.yawDegrees, WithinAbs (30.0, 1e-4));
    CHECK_THAT (heard.listenerHead.pitchDegrees, WithinAbs (-5.0, 1e-4));
    CHECK_THAT (heard.listenerHead.rollDegrees, WithinAbs (2.0, 1e-4));
}

TEST_CASE ("listener pose: Invert undoes a mirrored camera - yaw and roll flip, pitch stays")
{
    ListenerPose pose;
    pose.observe (HeadOrientation { 30.0f, 5.0f, 8.0f });
    pose.setInverted (true);

    const auto heard = pose.current().listenerHead;
    CHECK_THAT (heard.yawDegrees, WithinAbs (-30.0, 1e-4));
    CHECK_THAT (heard.pitchDegrees, WithinAbs (5.0, 1e-4));
    CHECK_THAT (heard.rollDegrees, WithinAbs (-8.0, 1e-4));
}

TEST_CASE ("listener pose: Recenter makes the current direction straight ahead")
{
    ListenerPose pose;
    pose.observe (facing (40.0f));
    CHECK (pose.recenter());
    CHECK_THAT (heardYaw (pose), WithinAbs (0.0, 1e-4));

    pose.observe (facing (55.0f));
    CHECK_THAT (heardYaw (pose), WithinAbs (15.0, 1e-4));

    SECTION ("across the +-180 seam the short way round")
    {
        pose.observe (facing (170.0f));
        pose.recenter();
        pose.observe (facing (-170.0f)); // 20 degrees further to the right
        CHECK_THAT (heardYaw (pose), WithinAbs (20.0, 1e-4));
        pose.observe (facing (150.0f));  // 20 degrees to the left
        CHECK_THAT (heardYaw (pose), WithinAbs (-20.0, 1e-4));
    }

    SECTION ("Invert afterwards keeps the centre: no jump")
    {
        pose.setInverted (true);
        CHECK_THAT (heardYaw (pose), WithinAbs (-15.0, 1e-4));
        pose.observe (facing (40.0f));
        CHECK_THAT (heardYaw (pose), WithinAbs (0.0, 1e-4));
    }

    SECTION ("Invert first, then Recenter: the same")
    {
        ListenerPose inverted;
        inverted.setInverted (true);
        inverted.observe (facing (40.0f));
        inverted.recenter();
        inverted.observe (facing (55.0f));
        CHECK_THAT (heardYaw (inverted), WithinAbs (-15.0, 1e-4));
    }

    SECTION ("without a head there is nothing to centre on")
    {
        pose.observe (std::nullopt);
        CHECK_FALSE (pose.recenter());
        pose.observe (facing (40.0f));
        CHECK_THAT (heardYaw (pose), WithinAbs (0.0, 1e-4)); // the centre from before
    }
}

TEST_CASE ("listener pose: not live while paused or without a head")
{
    ListenerPose pose;
    pose.observe (facing (30.0f));
    CHECK (pose.current().live);

    SECTION ("paused")
    {
        pose.setPaused (true);
        CHECK_FALSE (pose.current().live);
        CHECK_THAT (heardYaw (pose), WithinAbs (0.0, 1e-4)); // straight ahead
        pose.setPaused (false);
        CHECK (pose.current().live);
        CHECK_THAT (heardYaw (pose), WithinAbs (30.0, 1e-4));
    }

    SECTION ("no head in the frame")
    {
        pose.observe (std::nullopt);
        CHECK_FALSE (pose.current().live);
        pose.observe (facing (31.0f));
        CHECK (pose.current().live);
    }
}

// ---- With the file -------------------------------------------------------------------------------
#include "../tracker/tracking/ListenerPoseOutput.h"

TEST_CASE ("listener pose output: Composer's file follows every frame and every button", "[integration]")
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("composer-tests").getChildFile ("listener-pose-output");
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    headtracking::Reader reader (file, 0);
    {
        tracker::ListenerPoseOutput output (file);
        REQUIRE (output.isWritingToFile());

        CHECK (output.observe (facing (30.0f)).live);
        auto read = reader.read();
        REQUIRE (read.has_value());
        CHECK (read->face);
        CHECK_THAT (read->yaw, WithinAbs (30.0, 1e-4));
        CHECK (juce::Time::currentTimeMillis() - (juce::int64) read->timestampMs < (juce::int64) headtracking::staleAfterMs);

        output.recenter(); // a button, with no new frame: written at once
        CHECK_THAT (reader.read()->yaw, WithinAbs (0.0, 1e-4));

        output.setPaused (true);
        CHECK_FALSE (reader.read()->face);
        output.setPaused (false);
        CHECK (reader.read()->face);

        output.observe (std::nullopt); // the head left, or the camera went silent
        CHECK_FALSE (reader.read()->face);
        output.observe (facing (30.0f));
        CHECK (reader.read()->face);
    }
    CHECK_FALSE (reader.read()->face); // the app has quit
}

TEST_CASE ("listener pose output: a file that can't be created is reported, not ignored", "[integration]")
{
    const auto blocker = juce::File::createTempFile ("not-a-folder");
    REQUIRE (blocker.replaceWithText ("x"));
    tracker::ListenerPoseOutput output (blocker.getChildFile ("head-tracking")); // a file where a folder should be
    CHECK_FALSE (output.isWritingToFile());
    CHECK (output.observe (facing (30.0f)).live); // tracking itself goes on
    blocker.deleteFile();
}
