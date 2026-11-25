#pragma once

#include <algorithm>
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

// Stereo pan for a direction: -1 = hard left, +1 = hard right.
inline float pan (float azimuth)
{
    return foldedAzimuth (azimuth) / 90.0f;
}

inline float pan (Point player, Point listener)
{
    return pan (azimuthDegrees (player, listener));
}

inline float delayMs (Point player, Point listener)
{
    const auto d = distance (player, listener);
    return d < atListener ? 0.0f : d / speedOfSound * 1000.0f;
}

// Loudness, unity at 1 m and closer. The free-field law is -6 dB per doubling, but in
// a real hall the reflections keep far players present, so on the dry signal that sounds far
// too steep (user feedback): the drop is a third of it, -2 dB per doubling.
constexpr float referenceDistance = 1.0f; // m
constexpr float distanceLawExponent = 1.0f / 3.0f; // 1 = free field (-6 dB per doubling)

inline float distanceGain (float metres, float exponent = distanceLawExponent)
{
    return metres <= referenceDistance ? 1.0f : std::pow (referenceDistance / metres, exponent);
}
} // namespace geometry
