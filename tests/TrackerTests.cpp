#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_core/juce_core.h>
#include "../tracker/vision/ImageTensor.h"
#include "../tracker/vision/StillHeadGate.h"

using Catch::Matchers::WithinAbs;
using namespace tracker;

TEST_CASE ("head detector rows: only heads the detector is sure enough of, in the frame's pixels")
{
    // batch, class (0 body, 1 head, 2 hand), score, x1, y1, x2, y2 in the detector's 320 x 192 input
    const float rows[] = { 0, 0, 0.90f, 10, 10, 300, 180,   // a body
                           0, 1, 0.80f, 100, 20, 140, 60,   // a head
                           0, 1, 0.34f, 200, 20, 240, 60,   // a head, too unsure
                           0, 2, 0.95f, 50, 100, 70, 120 }; // a hand
    const auto heads = headsFromDetectorRows (rows, 4, 640.0f / 320.0f, 480.0f / 192.0f);

    REQUIRE (heads.size() == 1);
    CHECK_THAT (heads[0].score, WithinAbs (0.8, 1e-6));
    CHECK_THAT (heads[0].box.x1, WithinAbs (200.0, 1e-4)); // 100 * 2
    CHECK_THAT (heads[0].box.y1, WithinAbs (50.0, 1e-4));  // 20 * 2.5
    CHECK_THAT (heads[0].box.x2, WithinAbs (280.0, 1e-4));
    CHECK_THAT (heads[0].box.y2, WithinAbs (150.0, 1e-4));
    CHECK (headsFromDetectorRows (rows, 0, 2.0f, 2.5f).empty()); // no detections at all
}

TEST_CASE ("head continuity: the head nearest to the previous one")
{
    const std::vector<Detection> heads { { { 0, 0, 10, 10 }, 0.9f }, { { 100, 0, 110, 10 }, 0.5f } };

    CHECK (pickHead (heads, std::nullopt)->score > 0.8f);                    // first frame: the strongest
    CHECK (pickHead (heads, Box { 98, 0, 108, 10 })->box.x1 > 50.0f);         // then: the same person
    CHECK_FALSE (pickHead ({}, std::nullopt).has_value());
}

// ---- The pose model: crop contract, input tensor, outputs ----------------------------------------

TEST_CASE ("pose model crop: the head box +5 % per side, square of the longer side, centred")
{
    const auto crop = squareCropAroundHead (Box { 100, 50, 200, 170 }); // 100 x 120 -> 110 x 132 -> square 132
    CHECK (crop.side == 132);
    CHECK (crop.x == 84);  // centre 150 - 66
    CHECK (crop.y == 44);  // centre 110 - 66
    CHECK (squareCropAroundHead (Box { -4, 2, 20, 27 }).x == -6);  // may start outside the frame (border repeated)
}

TEST_CASE ("pose model input: matches the reference (OpenCV area resize, border repeated)")
{
    // A 40 x 30 picture whose levels wrap (hard for any resampler), a box partly outside the
    // frame, resized to 8 x 8. The expected levels are OpenCV's, as HFHPE's reference demo
    // resizes (uv run --with opencv-python-headless --with numpy):
    //
    //   y, x = np.mgrid[0:30, 0:40]
    //   rgb = np.stack([(x*7 + y*13) % 256, (x*3 + y*29 + 50) % 256, (x*11 + y*5 + 100) % 256], -1).astype(np.uint8)
    //   x0, y0, side = -6, 1, 28                      # squareCropAroundHead (Box { -4, 2, 20, 27 })
    //   ys = np.clip(np.arange(y0, y0 + side), 0, 29); xs = np.clip(np.arange(x0, x0 + side), 0, 39)
    //   cv2.resize(rgb[ys][:, xs], (8, 8), interpolation=cv2.INTER_AREA)
    constexpr int width = 40, height = 30, size = 8;
    auto readRgb = [] (int x, int y, float* rgb)
    {
        rgb[0] = (float) ((x * 7 + y * 13) % 256);
        rgb[1] = (float) ((x * 3 + y * 29 + 50) % 256);
        rgb[2] = (float) ((x * 11 + y * 5 + 100) % 256);
    };
    const int expected[3][size][size] = {
        { { 30, 30, 46, 70, 95, 119, 144, 168 },
          { 74, 74, 90, 114, 139, 163, 188, 212 },
          { 121, 121, 137, 161, 186, 210, 209, 107 },
          { 165, 165, 181, 205, 220, 139, 44, 47 },
          { 212, 212, 222, 132, 42, 45, 70, 94 },
          { 110, 110, 69, 40, 65, 89, 114, 138 },
          { 47, 47, 63, 87, 112, 136, 161, 185 },
          { 91, 91, 107, 131, 156, 180, 205, 229 } },
        { { 116, 116, 123, 133, 144, 154, 165, 175 },
          { 216, 216, 149, 160, 160, 108, 118, 108 },
          { 63, 63, 70, 80, 91, 101, 112, 122 },
          { 163, 163, 170, 180, 191, 201, 212, 159 },
          { 83, 83, 90, 101, 59, 48, 59, 69 },
          { 110, 110, 117, 127, 138, 148, 159, 169 },
          { 177, 177, 184, 194, 131, 142, 121, 90 },
          { 57, 57, 64, 74, 85, 95, 106, 116 } },
        { { 111, 111, 137, 174, 214, 147, 35, 72 },
          { 129, 129, 154, 191, 220, 49, 52, 89 },
          { 146, 146, 172, 209, 181, 30, 70, 107 },
          { 164, 164, 189, 226, 72, 47, 87, 124 },
          { 181, 181, 207, 182, 28, 65, 105, 142 },
          { 199, 199, 224, 94, 45, 82, 122, 159 },
          { 216, 216, 205, 34, 63, 100, 140, 177 },
          { 234, 234, 107, 40, 80, 117, 157, 194 } } };

    std::vector<float> tensor (3 * size * size);
    resizeSquareToTensor (readRgb, width, height, squareCropAroundHead (Box { -4, 2, 20, 27 }), size, tensor.data());

    int levelsOff = 0; // more than a level from OpenCV (its fixed-point rounding differs by one)
    for (int channel = 0; channel < 3; ++channel)
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const auto level = (tensor[(size_t) (channel * size * size + y * size + x)] + 1.0f) * 127.5f;
                levelsOff += std::abs (level - (float) expected[channel][y][x]) > 1.01f;
            }
    CHECK (levelsOff == 0);
}

TEST_CASE ("pose model output: angles from (cos, sin), confidence from kappa")
{
    CHECK_THAT (angleFromCosSinDegrees (1, 0), WithinAbs (0, 1e-5));
    CHECK_THAT (angleFromCosSinDegrees (0, 1), WithinAbs (90, 1e-4));
    CHECK_THAT (angleFromCosSinDegrees (0.5f, -0.866f), WithinAbs (-60, 0.01));
    // Straight behind is the seam: +180 from one side, -180 from the other.
    CHECK_THAT (angleFromCosSinDegrees (-1, 0.0f), WithinAbs (180, 1e-4));
    CHECK_THAT (angleFromCosSinDegrees (-1, -0.0f), WithinAbs (-180, 1e-4));
    CHECK_THAT (angleFromCosSinDegrees (-1, 0.001f), WithinAbs (179.94, 0.01));
    CHECK_THAT (angleFromCosSinDegrees (-1, -0.001f), WithinAbs (-179.94, 0.01));

    CHECK_THAT (confidenceFromKappa (5), WithinAbs (0.5, 1e-6));
    CHECK (confidenceFromKappa (100) > 0.9f);
    CHECK (confidenceFromKappa (0.001f) < 0.01f);
}

TEST_CASE ("camera frames: RGB and ARGB read the same, anything else is refused")
{
    for (const auto format : { juce::Image::RGB, juce::Image::ARGB })
    {
        juce::Image frame (format, 4, 4, true);
        frame.setPixelAt (1, 2, juce::Colour (200, 100, 50));
        float rgb[3];
        FramePixels (frame).read (1, 2, rgb);
        CHECK_THAT (rgb[0], WithinAbs (200.0, 0.0));
        CHECK_THAT (rgb[1], WithinAbs (100.0, 0.0));
        CHECK_THAT (rgb[2], WithinAbs (50.0, 0.0));
    }
    CHECK_THROWS (FramePixels (juce::Image (juce::Image::SingleChannel, 4, 4, true)));
}

TEST_CASE ("still gate: a still head reuses the estimate, a moving one or an old one doesn't")
{
    using tracker::StillHeadGate;
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

    StillHeadGate gate;
    CHECK_FALSE (gate.unchanged (frame (70, 1), 0.0)); // nothing to compare with yet
    gate.reset (frame (70, 1), head, 0.0);

    CHECK (gate.unchanged (frame (70, 2), 0.1));       // only noise
    CHECK_FALSE (gate.unchanged (frame (85, 3), 0.2)); // the head turned
    CHECK_FALSE (gate.unchanged (frame (70, 4), StillHeadGate::maxAgeSeconds)); // due for a full estimate

    gate.clear();                                      // head lost
    CHECK_FALSE (gate.unchanged (frame (70, 5), 0.3));
}
