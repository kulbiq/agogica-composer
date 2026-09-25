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

TEST_CASE ("DEIMv2: direction classes to a smooth angle")
{
    const Box head { 10, 10, 50, 60 };

    // One class: its 45° step.
    CHECK_THAT (deimDirection ({ { 8 + 2, head, 0.9f } })->yaw, WithinAbs (90, 1e-3));   // Right_Side
    CHECK_THAT (std::abs (deimDirection ({ { 8 + 4, head, 0.9f } })->yaw), WithinAbs (180, 1e-3)); // Back (±180)

    // Two neighbouring classes on the same head: in between, towards the stronger one.
    const auto between = deimDirection ({ { 8, head, 0.8f }, { 9, head, 0.8f } });
    CHECK_THAT (between->yaw, WithinAbs (22.5, 1e-3));

    // Across ±180 (Back and Left_Back) the circular mean doesn't collapse to 0.
    const auto back = deimDirection ({ { 12, head, 0.8f }, { 13, head, 0.8f } });
    CHECK_THAT (std::abs (back->yaw), WithinAbs (157.5, 1e-3));

    // Weak or non-direction classes: nothing.
    CHECK_FALSE (deimDirection ({ { 7, head, 0.9f } }).has_value());
    CHECK_FALSE (deimDirection ({ { 8, head, 0.1f } }).has_value());

    // A direction box on another head doesn't pull the angle.
    const auto other = deimDirection ({ { 8, head, 0.9f }, { 12, { 200, 200, 240, 250 }, 0.8f } });
    CHECK_THAT (other->yaw, WithinAbs (0, 1e-3));
}

TEST_CASE ("head continuity: the head nearest to the previous one")
{
    const std::vector<Detection> heads { { 1, { 0, 0, 10, 10 }, 0.9f }, { 1, { 100, 0, 110, 10 }, 0.5f } };

    CHECK (pickHead (heads, std::nullopt)->score > 0.8f);                    // first frame: the strongest
    CHECK (pickHead (heads, Box { 98, 0, 108, 10 })->box.x1 > 50.0f);         // then: the same person
    CHECK_FALSE (pickHead ({}, std::nullopt).has_value());
}

TEST_CASE ("YOLOv8-Pose: best person and a coarse yaw from keypoints")
{
    constexpr int anchors = 3;
    std::vector<float> out (56 * anchors, 0.0f);
    auto set = [&] (int row, int anchor, float v) { out[(size_t) (row * anchors + anchor)] = v; };

    // Anchor 1 is the person: box, score, and keypoints for a face turned to the image right.
    set (0, 1, 320), set (1, 1, 300), set (2, 1, 200), set (3, 1, 400), set (4, 1, 0.9f), set (4, 0, 0.5f);
    auto kp = [&] (int k, float x, float y, float c) { set (5 + 3 * k, 1, x), set (6 + 3 * k, 1, y), set (7 + 3 * k, 1, c); };
    kp (0, 330, 150, 0.9f);                      // nose, 30 px right of the ear midpoint
    kp (3, 260, 150, 0.9f), kp (4, 340, 150, 0.9f); // ears: midpoint 300, half spread 40
    kp (5, 240, 250, 0.9f), kp (6, 360, 250, 0.9f);

    const auto person = yoloBestPerson (out.data(), anchors);
    REQUIRE (person.has_value());
    CHECK_THAT (person->score, WithinAbs (0.9, 1e-6));
    CHECK_THAT (person->box.x1, WithinAbs (220, 1e-3));
    CHECK_THAT (*keypointYaw (*person), WithinAbs (toDegrees (std::asin (30.0f / 40.0f)), 1e-3)); // ~48.6°

    // Seen from behind (no nose or eyes): around 180°.
    kp (0, 0, 0, 0.0f);
    const auto behind = keypointYaw (*yoloBestPerson (out.data(), anchors));
    CHECK (std::abs (*behind) > 90.0f);

    // Nobody above the threshold.
    set (4, 1, 0.1f), set (4, 0, 0.1f);
    CHECK_FALSE (yoloBestPerson (out.data(), anchors).has_value());
}
