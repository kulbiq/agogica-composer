#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../tracker/ListenerPose.h"

using Catch::Matchers::WithinAbs;
using tracker::HeadOrientation;
using tracker::ListenerPose;

namespace
{
HeadOrientation facing (float yawDegrees) { return { yawDegrees, 0.0f, 0.0f }; }
float heardYaw (const ListenerPose& pose, double nowSeconds = 0.0) { return pose.at (nowSeconds).listenerHead.yawDegrees; }
} // namespace

TEST_CASE ("listener pose: Composer hears the head the camera sees")
{
    ListenerPose pose;
    CHECK_FALSE (pose.at (0.0).live); // nothing seen yet

    pose.observe (HeadOrientation { 30.0f, -5.0f, 2.0f }, 0.0);
    const auto heard = pose.at (0.0);
    CHECK (heard.live);
    CHECK_THAT (heard.listenerHead.yawDegrees, WithinAbs (30.0, 1e-4));
    CHECK_THAT (heard.listenerHead.pitchDegrees, WithinAbs (-5.0, 1e-4));
    CHECK_THAT (heard.listenerHead.rollDegrees, WithinAbs (2.0, 1e-4));
}

TEST_CASE ("listener pose: Invert flips the yaw of a mirrored camera")
{
    ListenerPose pose;
    pose.observe (facing (30.0f), 0.0);
    pose.setInverted (true);
    CHECK_THAT (heardYaw (pose), WithinAbs (-30.0, 1e-4));
}

TEST_CASE ("listener pose: Recenter makes the current direction straight ahead")
{
    ListenerPose pose;
    pose.observe (facing (40.0f), 0.0);
    pose.recenter();
    CHECK_THAT (heardYaw (pose), WithinAbs (0.0, 1e-4));

    pose.observe (facing (55.0f), 0.0);
    CHECK_THAT (heardYaw (pose), WithinAbs (15.0, 1e-4));

    SECTION ("across the +-180 seam the short way round")
    {
        pose.observe (facing (170.0f), 0.0);
        pose.recenter();
        pose.observe (facing (-170.0f), 0.0); // 20 degrees further to the right
        CHECK_THAT (heardYaw (pose), WithinAbs (20.0, 1e-4));
    }

    SECTION ("inverted")
    {
        pose.setInverted (true);
        pose.observe (facing (10.0f), 0.0);
        pose.recenter();
        pose.observe (facing (25.0f), 0.0);
        CHECK_THAT (heardYaw (pose), WithinAbs (-15.0, 1e-4));
    }
}

TEST_CASE ("listener pose: not live while paused, without a head, or once the frames stop")
{
    ListenerPose pose;
    pose.observe (facing (30.0f), 10.0);
    CHECK (pose.at (10.0).live);

    SECTION ("paused")
    {
        pose.setPaused (true);
        CHECK_FALSE (pose.at (10.0).live);
        CHECK_THAT (heardYaw (pose, 10.0), WithinAbs (0.0, 1e-4)); // straight ahead
        pose.setPaused (false);
        CHECK (pose.at (10.0).live);
    }

    SECTION ("no head in the frame")
    {
        pose.observe (std::nullopt, 10.1);
        CHECK_FALSE (pose.at (10.1).live);
        pose.observe (facing (31.0f), 10.2);
        CHECK (pose.at (10.2).live);
    }

    SECTION ("frames stopped")
    {
        CHECK (pose.at (10.0 + ListenerPose::staleAfterSeconds - 0.01).live);
        CHECK_FALSE (pose.at (10.0 + ListenerPose::staleAfterSeconds).live);
    }
}

// What the tracker does today and should not: pinned here so the refactoring changes nothing,
// each to be turned round by its own fix.
TEST_CASE ("listener pose: known wrong behaviour, kept for now")
{
    ListenerPose pose;

    SECTION ("Invert after Recenter makes the yaw jump")
    {
        pose.observe (facing (30.0f), 0.0);
        pose.recenter();
        pose.setInverted (true);
        CHECK_THAT (heardYaw (pose), WithinAbs (-60.0, 1e-4)); // should stay 0
    }

    SECTION ("Recenter without a head centres on the last head seen")
    {
        pose.observe (facing (30.0f), 0.0);
        pose.observe (std::nullopt, 0.1);
        pose.recenter();
        pose.observe (facing (0.0f), 0.2);
        CHECK_THAT (heardYaw (pose, 0.2), WithinAbs (-30.0, 1e-4)); // should stay 0
    }

    SECTION ("Invert leaves the roll of a mirrored camera as it is")
    {
        pose.observe (HeadOrientation { 30.0f, 5.0f, 8.0f }, 0.0);
        pose.setInverted (true);
        CHECK_THAT (pose.at (0.0).listenerHead.rollDegrees, WithinAbs (8.0, 1e-4)); // should be -8
    }
}
