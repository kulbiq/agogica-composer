#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_core/juce_core.h>
#include "../tracker/engines/Decode.h"

using Catch::Matchers::WithinAbs;
using namespace engines;

TEST_CASE ("head continuity: the head nearest to the previous one")
{
    const std::vector<Detection> heads { { 1, { 0, 0, 10, 10 }, 0.9f }, { 1, { 100, 0, 110, 10 }, 0.5f } };

    CHECK (pickHead (heads, std::nullopt)->score > 0.8f);                    // first frame: the strongest
    CHECK (pickHead (heads, Box { 98, 0, 108, 10 })->box.x1 > 50.0f);         // then: the same person
    CHECK_FALSE (pickHead ({}, std::nullopt).has_value());
}

// ---- HFHPE: crop contract, input tensor, outputs ----------------------------------------------

TEST_CASE ("hfhpe: the crop contract - box +5 % per side, square of the longer side, centred")
{
    const auto c = hfhpeCrop (Box { 100, 50, 200, 170 }); // 100 x 120 -> 110 x 132 -> square 132
    CHECK (c.side == 132);
    CHECK (c.x == 84);  // centre 150 - 66
    CHECK (c.y == 44);  // centre 110 - 66
    CHECK (hfhpeCrop (Box { -4, 2, 20, 27 }).x == -6);  // may start outside the frame (border repeated)
}

TEST_CASE ("hfhpe: input tensor matches the reference (OpenCV area resize, border repeated)")
{
    // A 40 x 30 picture whose levels wrap (hard for any resampler), a box partly outside the
    // frame, resized to 8 x 8. Expected: research/head-tracking (headtrack.models.hfhpe_crop,
    // the reference demo's square_crop + resize_square), red and blue channels as 0-255 levels.
    constexpr int w = 40, h = 30, size = 8;
    auto rgb = [] (int x, int y, float* c)
    {
        c[0] = (float) ((x * 7 + y * 13) % 256);
        c[1] = (float) ((x * 3 + y * 29 + 50) % 256);
        c[2] = (float) ((x * 11 + y * 5 + 100) % 256);
    };
    const int red[size][size] = {
        { 30, 30, 46, 70, 95, 119, 144, 168 },
        { 74, 74, 90, 114, 139, 163, 188, 212 },
        { 121, 121, 137, 161, 186, 210, 209, 107 },
        { 165, 165, 181, 205, 220, 139, 44, 47 },
        { 212, 212, 222, 132, 42, 45, 70, 94 },
        { 110, 110, 69, 40, 65, 89, 114, 138 },
        { 47, 47, 63, 87, 112, 136, 161, 185 },
        { 91, 91, 107, 131, 156, 180, 205, 229 } };
    const int blue[size][size] = {
        { 111, 111, 137, 174, 214, 147, 35, 72 },
        { 129, 129, 154, 191, 220, 49, 52, 89 },
        { 146, 146, 172, 209, 181, 30, 70, 107 },
        { 164, 164, 189, 226, 72, 47, 87, 124 },
        { 181, 181, 207, 182, 28, 65, 105, 142 },
        { 199, 199, 224, 94, 45, 82, 122, 159 },
        { 216, 216, 205, 34, 63, 100, 140, 177 },
        { 234, 234, 107, 40, 80, 117, 157, 194 } };

    std::vector<float> out (3 * size * size);
    squareToTensor (rgb, w, h, hfhpeCrop (Box { -4, 2, 20, 27 }), size, out.data());
    int off = 0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const auto level = [&] (int ch) { return (out[(size_t) (ch * size * size + y * size + x)] + 1.0f) * 127.5f; };
            off += std::abs (level (0) - (float) red[y][x]) > 1.01f; // a level apart where OpenCV's
            off += std::abs (level (2) - (float) blue[y][x]) > 1.01f; // fixed-point rounding differs
        }
    CHECK (off == 0);
}

TEST_CASE ("hfhpe: angles from (cos, sin), confidence from kappa")
{
    CHECK_THAT (angleOf (1, 0), WithinAbs (0, 1e-5));
    CHECK_THAT (angleOf (0, 1), WithinAbs (90, 1e-4));
    CHECK_THAT (std::abs (angleOf (-1, 0)), WithinAbs (180, 1e-4));
    CHECK_THAT (angleOf (0.5f, -0.866f), WithinAbs (-60, 0.01));
    CHECK_THAT (kappaConfidence (5), WithinAbs (0.5, 1e-6));
    CHECK (kappaConfidence (100) > 0.9f);
    CHECK (kappaConfidence (0.001f) < 0.01f);
}

#include "../tracker/engines/StillGate.h"

TEST_CASE ("still gate: a still head reuses the estimate, a moving one or an old one doesn't")
{
    using engines::StillGate;
    // A "head": a bright disc with a dark eye at `eyeX`, plus camera noise.
    auto frame = [] (float eyeX, int noiseSeed)
    {
        juce::Image image (juce::Image::RGB, 160, 120, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colours::darkgrey);
        g.setColour (juce::Colours::bisque);
        g.fillEllipse (50, 30, 60, 60);
        g.setColour (juce::Colours::black);
        g.fillEllipse (eyeX, 50, 10, 10);
        juce::Random random (noiseSeed);
        for (int i = 0; i < 2000; ++i)
        {
            const auto x = random.nextInt (160), y = random.nextInt (120);
            image.setPixelAt (x, y, image.getPixelAt (x, y).brighter (0.05f));
        }
        return image;
    };
    const juce::Rectangle<float> head { 50 / 160.0f, 30 / 120.0f, 60 / 160.0f, 60 / 120.0f };

    StillGate gate;
    CHECK_FALSE (gate.unchanged (frame (70, 1), 0.0)); // nothing to compare with yet
    gate.reset (frame (70, 1), head, 0.0);

    CHECK (gate.unchanged (frame (70, 2), 0.1));       // only noise
    CHECK_FALSE (gate.unchanged (frame (85, 3), 0.2)); // the head turned
    CHECK_FALSE (gate.unchanged (frame (70, 4), StillGate::maxAgeSeconds)); // due for a full estimate

    gate.clear();                                      // head lost
    CHECK_FALSE (gate.unchanged (frame (70, 5), 0.3));
}

TEST_CASE ("hfhpe: input tensor cost on a camera-sized frame", "[.perf]")
{
    // Release: build/release/ComposerTests "[perf]" -s. A 640 x 480 frame, a typical head box.
    juce::Image image (juce::Image::RGB, 640, 480, true);
    const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
    std::vector<float> out (3 * 128 * 128);
    const auto crop = hfhpeCrop (Box { 230, 120, 410, 400 });
    const auto start = juce::Time::getMillisecondCounterHiRes();
    constexpr int runs = 50;
    for (int i = 0; i < runs; ++i)
        squareToTensor ([&] (int x, int y, float* c)
        {
            const auto* px = reinterpret_cast<const juce::PixelRGB*> (pixels.getPixelPointer (x, y));
            c[0] = px->getRed(), c[1] = px->getGreen(), c[2] = px->getBlue();
        }, 640, 480, crop, 128, out.data());
    const auto ms = (juce::Time::getMillisecondCounterHiRes() - start) / runs;
    WARN ("squareToTensor, crop " << crop.side << " px -> 128: " << ms << " ms");
    CHECK (ms < 1000.0);
}
