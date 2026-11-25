#pragma once

// Pure maths around the head in a camera frame (no ONNX, no camera, no images): unit-tested in
// tests/TrackerTests.cpp. Angles in degrees.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace tracker
{
// A rectangle by its corners, in pixels.
struct Box { float x1 = 0, y1 = 0, x2 = 0, y2 = 0; };
inline float centreX (const Box& b) { return (b.x1 + b.x2) * 0.5f; }
inline float centreY (const Box& b) { return (b.y1 + b.y2) * 0.5f; }

struct Detection { Box box; float score = 0; };

// ---- The head detector's output ----------------------------------------------------------------
// PINTO's YOLOX Body-Head-Hand gives rows of 7: batch, class (0 body, 1 head, 2 hand), score,
// x1, y1, x2, y2 in its input's pixels.
constexpr int detectorRowLength = 7;
constexpr int detectorHeadClass = 1;
constexpr float minimumHeadScore = 0.35f;

// The heads among `rowCount` detector rows, scaled from the detector's input to the frame.
inline std::vector<Detection> headsFromDetectorRows (const float* rows, std::int64_t rowCount, float frameToInputX, float frameToInputY)
{
    std::vector<Detection> heads;
    for (std::int64_t i = 0; i < rowCount; ++i)
    {
        const auto* row = rows + i * detectorRowLength;
        const auto detectedClass = (int) row[1];
        const auto score = row[2];
        if (detectedClass == detectorHeadClass && score >= minimumHeadScore)
            heads.push_back ({ { row[3] * frameToInputX, row[4] * frameToInputY, row[5] * frameToInputX, row[6] * frameToInputY }, score });
    }
    return heads;
}

// Which head to follow: the strongest at first, then the one nearest to where the head was.
inline std::optional<Detection> pickHead (const std::vector<Detection>& heads, const std::optional<Box>& previousHead)
{
    if (heads.empty())
        return std::nullopt;

    if (! previousHead)
        return *std::max_element (heads.begin(), heads.end(), [] (auto& a, auto& b) { return a.score < b.score; });

    auto distanceFromPrevious = [&] (const Detection& d)
    {
        return std::hypot (centreX (d.box) - centreX (*previousHead), centreY (d.box) - centreY (*previousHead));
    };
    return *std::min_element (heads.begin(), heads.end(), [&] (auto& a, auto& b) { return distanceFromPrevious (a) < distanceFromPrevious (b); });
}

// ---- The pose model (HFHPE): its crop and its outputs -------------------------------------------
// The crop HFHPE was trained on (PINTO0309/High-Angle_Robust_Fast_Head_Pose_Estimation,
// demo/demo_hfhpe_onnx.py): the head box grown by 5 % per side, the square of its longer side
// centred on it, on whole pixels (Python's round: halfway to even = std::nearbyint). It may reach
// outside the frame; the pixels there repeat the border.
struct SquareCrop { int x = 0, y = 0, side = 1; };

inline SquareCrop squareCropAroundHead (const Box& head)
{
    constexpr double marginPerSide = 0.05;
    const double width = head.x2 - head.x1, height = head.y2 - head.y1;
    const double left = head.x1 - width * marginPerSide, right = head.x2 + width * marginPerSide;
    const double top = head.y1 - height * marginPerSide, bottom = head.y2 + height * marginPerSide;
    const double side = std::max (right - left, bottom - top);
    const double middleX = (left + right) / 2, middleY = (top + bottom) / 2;
    return { (int) std::nearbyint (middleX - side / 2), (int) std::nearbyint (middleY - side / 2),
             std::max (1, (int) std::nearbyint (side)) };
}

// The model gives each angle as a unit vector (cos, sin): degrees in [-180, 180].
inline float angleFromCosSinDegrees (float cosine, float sine)
{
    constexpr float degreesPerRadian = 180.0f / 3.14159265358979f;
    return std::atan2 (sine, cosine) * degreesPerRadian;
}

// Kappa is the model's own certainty about an angle: the concentration of a von Mises
// distribution (a bell curve on a circle), ~0.001 ... 100, higher = surer.
// As 0..1: 0.5 at kappa 5 (a spread of about 25 degrees). Shown on the page; nothing filters on it.
inline float confidenceFromKappa (float kappa)
{
    constexpr float kappaAtHalfConfidence = 5.0f;
    return kappa / (kappa + kappaAtHalfConfidence);
}
} // namespace tracker
