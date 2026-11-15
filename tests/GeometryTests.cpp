#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/dsp/Geometry.h"

using Catch::Matchers::WithinAbs;
using namespace geometry;

TEST_CASE ("distance")
{
    const Point listener { 1.0f, -2.0f };

    CHECK_THAT (distance ({ 4.0f, 2.0f }, listener), WithinAbs (5.0, 1e-5));
}

TEST_CASE ("azimuth and pan: 0 ahead, +90 right, rear folds to front")
{
    const Point l {};

    CHECK_THAT (azimuthDegrees ({ 0.0f, 5.0f }, l), WithinAbs (0.0, 1e-4));
    CHECK_THAT (azimuthDegrees ({ 5.0f, 0.0f }, l), WithinAbs (90.0, 1e-4));
    CHECK_THAT (azimuthDegrees ({ -5.0f, 0.0f }, l), WithinAbs (-90.0, 1e-4));
    CHECK_THAT (azimuthDegrees ({ 5.0f, -5.0f }, l), WithinAbs (135.0, 1e-4));

    CHECK_THAT (foldedAzimuth (135.0f), WithinAbs (45.0, 1e-4));
    CHECK_THAT (foldedAzimuth (-170.0f), WithinAbs (-10.0, 1e-4));
    CHECK_THAT (foldedAzimuth (180.0f), WithinAbs (0.0, 1e-4));

    // Pan is the folded azimuth mapped to -1..+1.
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

TEST_CASE ("loudness: -2 dB per doubling from 1 m")
{
    auto db = [] (float gain) { return 20.0 * std::log10 (gain); };

    CHECK_THAT (db (distanceGain (0.5f)), WithinAbs (0.0, 1e-6));
    CHECK_THAT (db (distanceGain (1.0f)), WithinAbs (0.0, 1e-6));
    CHECK_THAT (db (distanceGain (2.0f)), WithinAbs (-2.01, 0.01));
    CHECK_THAT (db (distanceGain (20.0f)), WithinAbs (-8.67, 0.01));
}

TEST_CASE ("air absorption: one-pole cutoff reproduces the 8 kHz loss")
{
    for (const auto metres : { 5.0f, 20.0f, 70.0f })
    {
        const auto fc = airCutoffHz (metres);
        const auto lossAt8k = 10.0 * std::log10 (1.0 + std::pow (8000.0 / fc, 2.0));
        CHECK_THAT (lossAt8k, WithinAbs (0.0766 * metres, 1e-3));
    }

    CHECK_THAT (airCutoffHz (0.001f), WithinAbs (0.0, 0.0));      // negligible: no filter
    CHECK_THAT (airCutoffHz (50.0f, 0.0f), WithinAbs (0.0, 0.0)); // Pro air at 0: no filter at all
}
