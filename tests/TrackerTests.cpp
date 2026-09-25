#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../tracker/engines/Decode.h"

using Catch::Matchers::WithinAbs;
using namespace engines;

TEST_CASE ("6DRepNet360: full heading from the Euler output")
{
    // The plan's convention cases: R = Rz(roll)·Ry(yaw)·Rx(pitch), heading = atan2(-R20, R00).
    CHECK_THAT (sixDRepNetHeading (30, 0, 0), WithinAbs (30, 1e-3));
    CHECK_THAT (sixDRepNetHeading (0, 180, 180), WithinAbs (180, 1e-3));
    CHECK_THAT (sixDRepNetHeading (-30, 180, 180), WithinAbs (-150, 1e-3));
    CHECK_THAT (sixDRepNetHeading (30, 180, 180), WithinAbs (150, 1e-3));
    CHECK_THAT (sixDRepNetHeading (-60, 5, -3), WithinAbs (-60, 0.1)); // small pitch/roll: nearly unchanged
}

TEST_CASE ("head continuity: the head nearest to the previous one")
{
    const std::vector<Detection> heads { { 1, { 0, 0, 10, 10 }, 0.9f }, { 1, { 100, 0, 110, 10 }, 0.5f } };

    CHECK (pickHead (heads, std::nullopt)->score > 0.8f);                    // first frame: the strongest
    CHECK (pickHead (heads, Box { 98, 0, 108, 10 })->box.x1 > 50.0f);         // then: the same person
    CHECK_FALSE (pickHead ({}, std::nullopt).has_value());
}
