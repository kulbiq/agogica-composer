#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Stage geometry in metres: origin at the room centre, +x right, +y front.
// Pure functions shared by the UI bridge (derived values) and the DSP.
namespace geometry
{
constexpr float speedOfSound = 343.0f;   // m/s
constexpr float atListener   = 0.01f;    // m; closer than this counts as "at the listener"

struct Point { float x = 0.0f, y = 0.0f; };

// Positions are stored unclamped and clamped at use, so shrinking the room and
// growing it back restores them.
inline Point clampToRoom (Point p, float roomSize)
{
    const auto half = roomSize * 0.5f;
    return { std::clamp (p.x, -half, half), std::clamp (p.y, -half, half) };
}

inline float distance (Point player, Point listener)
{
    return std::hypot (player.x - listener.x, player.y - listener.y);
}

// 0 = straight ahead (+y), +90 = right, ±180 = behind.
inline float azimuthDegrees (Point player, Point listener)
{
    if (distance (player, listener) < atListener)
        return 0.0f;

    constexpr auto degreesPerRadian = 57.29577951308232f;
    return std::atan2 (player.x - listener.x, player.y - listener.y) * degreesPerRadian;
}

// Stereo can't place sounds behind the listener: mirror rear angles to the front.
inline float foldedAzimuth (float azimuth)
{
    return std::abs (azimuth) > 90.0f ? std::copysign (180.0f - std::abs (azimuth), azimuth)
                                      : azimuth;
}

// -1 = hard left, +1 = hard right.
inline float pan (Point player, Point listener)
{
    return foldedAzimuth (azimuthDegrees (player, listener)) / 90.0f;
}

inline float delayMs (Point player, Point listener)
{
    const auto d = distance (player, listener);
    return d < atListener ? 0.0f : d / speedOfSound * 1000.0f;
}

// Loudness, unity at 1 m and closer. The free-field law is -6 dB per doubling, but in
// a hall the reflections keep far players present, so on the dry signal that sounds far
// too steep (user feedback): the drop is a third of it, -2 dB per doubling.
constexpr float referenceDistance = 1.0f; // m
constexpr float distanceLawExponent = 1.0f / 3.0f; // 1 = free field (-6 dB per doubling)

inline float distanceGain (float metres)
{
    return metres <= referenceDistance ? 1.0f : std::pow (referenceDistance / metres, distanceLawExponent);
}

// Air absorption, ISO 9613-1 at 20 °C / 50 % RH: 0.0766 dB per metre at 8 kHz, far
// less below. Modelled as a one-pole low-pass whose loss at 8 kHz matches.
constexpr float airLossDbPerMetreAt8k = 0.0766f;

inline float airLossDbAt8k (float metres)
{
    return airLossDbPerMetreAt8k * metres;
}

// Cutoff of that low-pass, or 0 when the loss is negligible (no filtering).
inline float airCutoffHz (float metres)
{
    const auto excess = std::pow (10.0f, airLossDbAt8k (metres) / 10.0f) - 1.0f;
    return excess < 1.0e-4f ? 0.0f : 8000.0f / std::sqrt (excess);
}

// ---- Reflections: image sources of a square room --------------------------------
// The floor plan is the square [-room/2, room/2]²; ears and sources are at 1.2 m. A wall
// reflection is heard as if from the source mirrored in that wall ("image source").

constexpr float earHeight = 1.2f; // m
constexpr float maxRoomSize = 50.0f;

struct Image
{
    Point position;   // on the floor plan (floor/ceiling images share the source's)
    float distance;   // path length from the image to the listener, m
    int bounces;      // 1 or 2
};

constexpr int numImages = 10;

// 4 walls + floor + ceiling (first order) and the 4 corner images (a wall of each
// axis: the strongest horizontal second-order paths).
inline std::array<Image, numImages> imageSources (Point player, Point listener, float roomSize, float ceiling)
{
    // A coordinate mirrored in the wall at side · room/2 (side = ±1).
    const auto mirror = [&] (float v, int side) { return (float) side * roomSize - v; };
    const auto horizontal = [&] (Point p) { return distance (p, listener); };

    std::array<Image, numImages> images {};
    int n = 0;

    for (const auto side : { 1, -1 })
    {
        const Point wallX { mirror (player.x, side), player.y };
        const Point wallY { player.x, mirror (player.y, side) };
        images[(size_t) n++] = { wallX, horizontal (wallX), 1 };
        images[(size_t) n++] = { wallY, horizontal (wallY), 1 };
    }

    const auto direct = horizontal (player);
    images[(size_t) n++] = { player, std::hypot (direct, 2.0f * earHeight), 1 };                  // floor
    images[(size_t) n++] = { player, std::hypot (direct, 2.0f * (ceiling - earHeight)), 1 };      // ceiling

    for (const auto sx : { 1, -1 })
        for (const auto sy : { 1, -1 })
        {
            const Point corner { mirror (player.x, sx), mirror (player.y, sy) };
            images[(size_t) n++] = { corner, horizontal (corner), 2 };
        }

    return images;
}

// Longest image path in any room (largest room, any positions inside it): a corner image
// can be 2 rooms away on both axes (2√2 · room), the ceiling image < 3 · room for any
// preset ceiling (≤ 20 m).
constexpr float maxImageDistance = 3.0f * maxRoomSize;

} // namespace geometry