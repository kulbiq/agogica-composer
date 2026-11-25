#pragma once

// Pure decoding maths for the tracker's head-pose estimator (no ONNX, no camera): unit-tested
// in tests/TrackerTests.cpp. Angles in degrees.

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace engines
{
inline float toDegrees (float radians) { return radians * 180.0f / 3.14159265358979f; }

struct Box { float x1 = 0, y1 = 0, x2 = 0, y2 = 0; };
inline float centreX (const Box& b) { return (b.x1 + b.x2) * 0.5f; }
inline float centreY (const Box& b) { return (b.y1 + b.y2) * 0.5f; }

struct Detection { Box box; float score = 0; };

// ---- HFHPE: crop contract and outputs ----------------------------------------------------------
// HFHPE (PINTO0309/High-Angle_Robust_Fast_Head_Pose_Estimation, demo/demo_hfhpe_onnx.py): the head
// box grown by 5 % per side, the square of its longer side centred on it, on whole pixels
// (Python's round: halfway to even = std::nearbyint), pixels outside the frame repeating the
// border; then resized to the model's input.
struct SquareCrop { int x = 0, y = 0, side = 1; };

inline SquareCrop hfhpeCrop (const Box& b, double margin = 0.05)
{
    const double w = b.x2 - b.x1, h = b.y2 - b.y1;
    const double x1 = b.x1 - w * margin, x2 = b.x2 + w * margin, y1 = b.y1 - h * margin, y2 = b.y2 + h * margin;
    const double side = std::max (x2 - x1, y2 - y1), cx = (x1 + x2) / 2, cy = (y1 + y2) / 2;
    return { (int) std::nearbyint (cx - side / 2), (int) std::nearbyint (cy - side / 2),
             std::max (1, (int) std::nearbyint (side)) };
}

// Each angle comes as a unit vector (cos, sin): degrees in (-180, 180].
inline float angleOf (float c, float s) { return toDegrees (std::atan2 (s, c)); }

// The yaw's von Mises concentration (~0.001 ... 100; higher = surer) as 0..1: 0.5 at kappa 5
// (a spread of about 25 degrees). Shown on the page; nothing filters on it.
inline float kappaConfidence (float kappa) { return kappa / (kappa + 5.0f); }

// Resizes `crop` of a width x height image to size x size, channel-first RGB, each value
// rounded to a whole level first (the reference resizes 8-bit pictures), then v / 127.5 - 1
// (HFHPE's "center05"). `rgb (x, y, c)` reads pixel x, y (inside the image) into c[0..2] = r, g, b.
// Shrinking averages the covered area (OpenCV INTER_AREA, as the reference); enlarging - a head
// smaller than the input, rare at 640 x 480 - is bilinear where the reference uses Lanczos.
template <typename ReadRgb>
void squareToTensor (ReadRgb&& rgb, int width, int height, SquareCrop crop, int size, float* out)
{
    const auto plane = (size_t) size * (size_t) size;
    const double f = (double) crop.side / size; // source pixels per output pixel
    auto read = [&] (int sx, int sy, float* c)   // in crop coordinates; both the crop's and the image's border repeat
    {
        sx = std::clamp (sx, 0, crop.side - 1), sy = std::clamp (sy, 0, crop.side - 1);
        rgb (std::clamp (crop.x + sx, 0, width - 1), std::clamp (crop.y + sy, 0, height - 1), c);
    };
    auto store = [&] (int ox, int oy, const double* v)
    {
        for (size_t ch = 0; ch < 3; ++ch)
            out[ch * plane + (size_t) oy * (size_t) size + (size_t) ox] = (float) (std::nearbyint (v[ch]) / 127.5 - 1.0);
    };

    if (f >= 1.0)
    {
        // Per output index: the source pixels it covers and how much of each.
        std::vector<std::vector<std::pair<int, double>>> cover ((size_t) size);
        for (int o = 0; o < size; ++o)
        {
            const double a = o * f, b = (o + 1) * f;
            for (int s = (int) std::floor (a); s < (int) std::ceil (b); ++s)
                if (const auto w = std::min (b, s + 1.0) - std::max (a, (double) s); w > 1e-9)
                    cover[(size_t) o].push_back ({ s, w });
        }
        float c[3];
        for (int oy = 0; oy < size; ++oy)
            for (int ox = 0; ox < size; ++ox)
            {
                double v[3] {};
                for (auto [sy, wy] : cover[(size_t) oy])
                    for (auto [sx, wx] : cover[(size_t) ox])
                    {
                        read (sx, sy, c);
                        for (size_t ch = 0; ch < 3; ++ch)
                            v[ch] += wx * wy * c[ch];
                    }
                for (auto& x : v)
                    x /= f * f;
                store (ox, oy, v);
            }
        return;
    }

    float a[3], b[3], c[3], d[3];
    for (int oy = 0; oy < size; ++oy)
        for (int ox = 0; ox < size; ++ox)
        {
            const double fx = (ox + 0.5) * f - 0.5, fy = (oy + 0.5) * f - 0.5;
            const int x0 = (int) std::floor (fx), y0 = (int) std::floor (fy);
            const double tx = fx - x0, ty = fy - y0;
            read (x0, y0, a), read (x0 + 1, y0, b), read (x0, y0 + 1, c), read (x0 + 1, y0 + 1, d);
            double v[3];
            for (size_t ch = 0; ch < 3; ++ch)
                v[ch] = (a[ch] * (1 - tx) + b[ch] * tx) * (1 - ty) + (c[ch] * (1 - tx) + d[ch] * tx) * ty;
            store (ox, oy, v);
        }
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
