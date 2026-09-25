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

// ---- DEIMv2-Wholebody34: 8 head-direction classes (45° steps) ----------------------------
// Classes 8-15: Front, Right_Front, Right_Side, Right_Back, Back, Left_Back, Left_Side,
// Left_Front. The angle is the score-weighted circular mean of the direction detections
// overlapping the best one, so it moves between the 45° steps.
constexpr int deimFirstDirection = 8;
constexpr std::array<float, 8> deimDirectionDegrees { 0, 45, 90, 135, 180, -135, -90, -45 };

struct Direction { float yaw = 0; Box box; float confidence = 0; };

inline std::optional<Direction> deimDirection (const std::vector<Detection>& detections, float minScore = 0.3f)
{
    const Detection* best = nullptr;

    for (const auto& d : detections)
        if (d.label >= deimFirstDirection && d.label < deimFirstDirection + 8 && d.score >= minScore
            && (best == nullptr || d.score > best->score))
            best = &d;

    if (best == nullptr)
        return std::nullopt;

    float s = 0, c = 0;
    for (const auto& d : detections)
        if (d.label >= deimFirstDirection && d.label < deimFirstDirection + 8 && iou (d.box, best->box) > 0.5f)
        {
            const auto a = toRadians (deimDirectionDegrees[(size_t) (d.label - deimFirstDirection)]);
            s += d.score * std::sin (a);
            c += d.score * std::cos (a);
        }

    return Direction { toDegrees (std::atan2 (s, c)), best->box, best->score };
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

// ---- YOLOv8n-Pose: best person + a coarse yaw from keypoints -----------------------------
struct Keypoint { float x = 0, y = 0, confidence = 0; };
struct Person { Box box; float score = 0; std::array<Keypoint, 17> keypoints; };

// output: [56][anchors] = cx, cy, w, h, score, 17 × (x, y, confidence), in input pixels.
inline std::optional<Person> yoloBestPerson (const float* output, int anchors, float minScore = 0.4f)
{
    int best = -1;
    for (int i = 0; i < anchors; ++i)
        if (output[4 * anchors + i] >= minScore && (best < 0 || output[4 * anchors + i] > output[4 * anchors + best]))
            best = i;

    if (best < 0)
        return std::nullopt;

    const auto at = [&] (int row) { return output[row * anchors + best]; };
    Person p;
    p.box = { at (0) - at (2) / 2, at (1) - at (3) / 2, at (0) + at (2) / 2, at (1) + at (3) / 2 };
    p.score = at (4);
    for (int k = 0; k < 17; ++k)
        p.keypoints[(size_t) k] = { at (5 + 3 * k), at (6 + 3 * k), at (7 + 3 * k) };
    return p;
}

// COCO: 0 nose, 1/2 left/right eye, 3/4 left/right ear, 5/6 left/right shoulder. Image x.
// Facing the camera, the nose sits between the ears; its offset from their midpoint (relative
// to half their spread) is the sine of the yaw. Facing away (no nose or eyes), the same from
// behind, around 180°. Coarse by nature - left for the user to compare.
inline std::optional<float> keypointYaw (const Person& p, float visible = 0.5f)
{
    const auto& k = p.keypoints;
    const auto seen = [&] (int i) { return k[(size_t) i].confidence >= visible; };

    const bool ears = seen (3) && seen (4);
    const bool shoulders = seen (5) && seen (6);
    if (! ears && ! shoulders)
        return std::nullopt;

    const auto midX = ears ? (k[3].x + k[4].x) / 2 : (k[5].x + k[6].x) / 2;
    const auto half = std::max (1.0f, std::abs (ears ? k[4].x - k[3].x : k[6].x - k[5].x) / 2);
    const bool facing = seen (0) || seen (1) || seen (2);

    if (facing)
    {
        const auto noseX = seen (0) ? k[0].x : (seen (1) ? k[1].x : k[2].x);
        return toDegrees (std::asin (std::clamp ((noseX - midX) / half, -1.0f, 1.0f)));
    }

    // From behind the subject's left ear is on the image left; the ear/shoulder offset tells
    // which way the back of the head is turned.
    const auto shoulderMid = shoulders ? (k[5].x + k[6].x) / 2 : midX;
    const auto s = std::clamp ((midX - shoulderMid) / half, -1.0f, 1.0f);
    return wrap (180.0f - toDegrees (std::asin (s)));
}
} // namespace engines
