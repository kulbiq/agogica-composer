#pragma once

// Pure decoding maths for the tracker's head-pose engines (no ONNX, no camera): unit-tested
// in tests/TrackerTests.cpp. Angles in degrees.

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace engines
{
constexpr float pi = 3.14159265358979f;
inline float toRadians (float d) { return d * pi / 180.0f; }
inline float toDegrees (float r) { return r * 180.0f / pi; }
inline float wrap (float d) { return std::remainder (d, 360.0f); } // (-180, 180]

struct Box { float x1 = 0, y1 = 0, x2 = 0, y2 = 0; };
inline float centreX (const Box& b) { return (b.x1 + b.x2) * 0.5f; }
inline float centreY (const Box& b) { return (b.y1 + b.y2) * 0.5f; }

inline float iou (const Box& a, const Box& b)
{
    const auto w = std::max (0.0f, std::min (a.x2, b.x2) - std::max (a.x1, b.x1));
    const auto h = std::max (0.0f, std::min (a.y2, b.y2) - std::max (a.y1, b.y1));
    const auto inter = w * h;
    const auto uni = (a.x2 - a.x1) * (a.y2 - a.y1) + (b.x2 - b.x1) * (b.y2 - b.y1) - inter;
    return uni > 0.0f ? inter / uni : 0.0f;
}

struct Detection { int label = 0; Box box; float score = 0; };

// ---- 6DRepNet360: full heading from its Euler output --------------------------------------
// Upstream decodes R = Rz(roll)·Ry(yaw)·Rx(pitch) with pitch = atan2(R21, R22),
// yaw = atan2(-R20, sy) ∈ [-90, 90], roll = atan2(R10, R00). A head facing away therefore
// comes out as a mirrored yaw with pitch and roll near ±180. Rebuild R and take the heading
// of the forward axis: atan2(-R20, R00).
inline float sixDRepNetHeading (float yaw, float pitch, float roll)
{
    const auto y = toRadians (yaw), x = toRadians (pitch), z = toRadians (roll);
    const auto r00 = std::cos (z) * std::cos (y);
    const auto r20 = -std::sin (y);
    (void) x; // R00 and R20 don't depend on pitch; kept for the documented convention
    // For |yaw| < 90 cos(y) > 0, so the sign of R00 comes from cos(roll): roll ≈ ±180 flips it.
    return wrap (toDegrees (std::atan2 (-r20, r00)));
}

// ---- Head continuity: the candidate nearest to where the head was ------------------------
inline std::optional<Detection> pickHead (const std::vector<Detection>& heads, const std::optional<Box>& previous)
{
    if (heads.empty())
        return std::nullopt;

    if (! previous)
        return *std::max_element (heads.begin(), heads.end(), [] (auto& a, auto& b) { return a.score < b.score; });

    return *std::min_element (heads.begin(), heads.end(), [&] (auto& a, auto& b)
    {
        auto distance = [&] (const Detection& d) { return std::hypot (centreX (d.box) - centreX (*previous), centreY (d.box) - centreY (*previous)); };
        return distance (a) < distance (b);
    });
}

} // namespace engines
