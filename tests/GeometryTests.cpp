#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/dsp/Geometry.h"

using Catch::Matchers::WithinAbs;
using namespace geometry;

TEST_CASE ("distance and delay")
{
    const Point listener { 1.0f, -2.0f };

    CHECK_THAT (distance ({ 4.0f, 2.0f }, listener), WithinAbs (5.0, 1e-5));
    CHECK_THAT (delayMs ({ 4.0f, 2.0f }, listener), WithinAbs (5.0 / 343.0 * 1000.0, 1e-3));
    CHECK_THAT (delayMs ({ 1.005f, -2.0f }, listener), WithinAbs (0.0, 1e-9));
}

TEST_CASE ("azimuth: 0 ahead, +90 right, rear folds to front")
{
    const Point l {};

    CHECK_THAT (azimuthDegrees ({ 0.0f, 5.0f }, l), WithinAbs (0.0, 1e-4));
    CHECK_THAT (azimuthDegrees ({ 5.0f, 0.0f }, l), WithinAbs (90.0, 1e-4));
    CHECK_THAT (azimuthDegrees ({ -5.0f, 0.0f }, l), WithinAbs (-90.0, 1e-4));
    CHECK_THAT (azimuthDegrees ({ 5.0f, -5.0f }, l), WithinAbs (135.0, 1e-4));

    CHECK_THAT (foldedAzimuth (135.0f), WithinAbs (45.0, 1e-4));
    CHECK_THAT (foldedAzimuth (-170.0f), WithinAbs (-10.0, 1e-4));
    CHECK_THAT (foldedAzimuth (180.0f), WithinAbs (0.0, 1e-4));
}

TEST_CASE ("pan maps folded azimuth to -1..+1")
{
    const Point l {};

    CHECK_THAT (pan ({ 0.0f, 3.0f }, l), WithinAbs (0.0, 1e-5));
    CHECK_THAT (pan ({ 3.0f, 0.0f }, l), WithinAbs (1.0, 1e-5));
    CHECK_THAT (pan ({ -3.0f, 3.0f }, l), WithinAbs (-0.5, 1e-5));
    CHECK_THAT (pan ({ -3.0f, -3.0f }, l), WithinAbs (-0.5, 1e-5)); // behind-left folds to front-left
    CHECK_THAT (pan ({ 0.0f, 0.0f }, l), WithinAbs (0.0, 1e-5));    // at the listener: centre
}

TEST_CASE ("clampToRoom keeps points inside the square")
{
    const auto p = clampToRoom ({ 12.0f, -30.0f }, 20.0f);
    CHECK_THAT (p.x, WithinAbs (10.0, 1e-6));
    CHECK_THAT (p.y, WithinAbs (-10.0, 1e-6));

    const auto inside = clampToRoom ({ 3.0f, 4.0f }, 20.0f);
    CHECK_THAT (inside.x, WithinAbs (3.0, 1e-6));
    CHECK_THAT (inside.y, WithinAbs (4.0, 1e-6));
}
