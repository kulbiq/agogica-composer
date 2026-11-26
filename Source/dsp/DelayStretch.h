#pragma once

#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Stretched delays (user decision, 2026-11-26: the physical delays sound far too small next to
// a live ensemble). A player's delay is the physical one times a multiplier between `from` and
// `to` (Pro "Delay stretch"), so the far ones are stretched most. The multiplier comes from a
// smooth random field over the stage and the song's time: players near each other get nearly
// the same one, so a section never plays raggedly; players far apart differ.
// A track takes a new value of the field only after a pause long enough that nothing of the
// last phrase is still on its way through the delay: the delay then jumps (nothing is
// sounding in it) instead of fading under a note. The field depends only on the place, the
// song's time and the parameters, so every playback and every export is the same.
namespace stretch
{
constexpr float spanMetres = 12.0f;  // the field changes over about this distance (1 m: a few %)
constexpr double spanSeconds = 60.0; // and over about this much of the song
constexpr float quietAbsolute = 1.0e-3f; // -60 dBFS: below this the input is silent
constexpr float quietRelative = 3.16e-3f; // or 50 dB below the phrase's loudest
constexpr double minPauseSeconds = 0.25;  // shorter gaps are between notes, not a pause
constexpr double marginSeconds = 0.05;

// A hash of a lattice point, 0..1.
inline float lattice (std::int32_t x, std::int32_t y, std::int32_t t) noexcept
{
    auto h = (std::uint32_t) x * 0x8da6b343u ^ (std::uint32_t) y * 0xd8163841u ^ (std::uint32_t) t * 0xcb1ab31fu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return (float) (h & 0xffffffu) / (float) 0xffffffu;
}

// Smooth value noise, 0..1, at a place (metres) and a time (seconds of the song).
inline float field (geometry::Point p, double songSeconds) noexcept
{
    const auto fx = p.x / spanMetres, fy = p.y / spanMetres;
    const auto ft = (float) (songSeconds / spanSeconds);
    const auto x0 = std::floor (fx), y0 = std::floor (fy), t0 = std::floor (ft);
    const auto smooth = [] (float u) { return u * u * (3.0f - 2.0f * u); };
    const auto u = smooth (fx - x0), v = smooth (fy - y0), w = smooth (ft - t0);
    const auto ix = (std::int32_t) x0, iy = (std::int32_t) y0, it = (std::int32_t) t0;
    const auto lerp = [] (float a, float b, float k) { return a + (b - a) * k; };
    const auto plane = [&] (std::int32_t t)
    {
        return lerp (lerp (lattice (ix, iy, t), lattice (ix + 1, iy, t), u),
                     lerp (lattice (ix, iy + 1, t), lattice (ix + 1, iy + 1, t), u), v);
    };
    return lerp (plane (it), plane (it + 1), w);
}
} // namespace stretch

// One track's multiplier: held while it plays, taken anew after each pause.
class DelayStretch
{
public:
    // Also after a gap in the input (bypass): the next sound takes a new multiplier.
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        taken = false;
        quietFor = 0;
        phrasePeak = 0.0f;
    }

    // Once per block, with the block's input peak. `physical`: the delay in seconds before
    // stretching. True when a new multiplier was taken: the delay should jump, not fade.
    bool update (float peak, int n, geometry::Point player, double songSeconds, float physical, float from, float to) noexcept
    {
        if (peak < std::max (stretch::quietAbsolute, phrasePeak * stretch::quietRelative))
        {
            quietFor = std::min (quietFor + n, 1 << 30); // hours of silence: long enough
            return false;
        }

        const auto candidate = stretch::field (player, songSeconds);
        const auto delayOf = [&] (float value) { return physical * multiplier (value, from, to); };
        const auto quietSeconds = (double) quietFor / sampleRate;
        const auto paused = quietSeconds >= stretch::minPauseSeconds;
        const auto drained = quietSeconds >= std::max (delayOf (held), delayOf (candidate)) + stretch::marginSeconds;
        const auto take = ! taken || (paused && drained);

        if (take)
        {
            held = candidate;
            taken = true;
        }

        phrasePeak = paused ? peak : std::max (phrasePeak, peak);
        quietFor = 0;
        return take;
    }

    float multiplier (float from, float to) const noexcept { return multiplier (held, from, to); }

private:
    static float multiplier (float value, float from, float to) noexcept
    {
        const auto lo = std::min (from, to), hi = std::max (from, to);
        return lo + (hi - lo) * value;
    }

    double sampleRate = 48000.0;
    float held = 0.5f;
    bool taken = false;
    int quietFor = 0;
    float phrasePeak = 0.0f;
};
