#pragma once

#include <array>
#include "Geometry.h"

enum class OutputMode { stereo, binaural };

// Room character presets (the `hall` parameter). Starting values, to be tuned by ear.
struct Hall
{
    const char* name;
    float ceiling;       // m; 0 = no reflections at all ("Dry")
    float absorption;    // wall/floor/ceiling energy absorption, 0..1
    float rt60;          // s, reverb tail decay time (mid frequencies)
    float rt60High;      // s, at high frequencies (shorter: air and soft surfaces)
    float preDelayMs;    // tail start after the direct sound
    float tailLevel;     // tail gain relative to a player at 1 m
    float floorAbsorption = 0.0f; // audience, seats, carpet absorb far more than walls
    float surfaceCutoffHz = 0.0f; // surfaces absorb highs: low-pass per reflection (0 = none)
};

inline constexpr std::array<Hall, 5> halls { {
    //  name           ceiling absorb  rt60  high  pre   tail   floor  surface Hz
    { "Dry",           0.0f,  0.0f,   0.0f, 0.0f,  0.0f, 0.0f,  0.0f,  0.0f    },
    { "Studio",        3.0f,  0.6f,   0.3f, 0.2f,  5.0f, 0.10f, 0.7f,  4000.0f },
    { "Chamber",       5.0f,  0.35f,  1.0f, 0.6f, 10.0f, 0.15f, 0.6f,  6000.0f },
    { "Concert hall", 15.0f,  0.2f,   2.0f, 1.2f, 20.0f, 0.20f, 0.7f,  5000.0f },
    { "Church",       20.0f,  0.1f,   4.0f, 2.2f, 30.0f, 0.25f, 0.3f,  7000.0f },
} };

inline constexpr int defaultHall = 2; // Chamber

// The Pro panel: project-wide adjustments on top of the hall, in natural units.
// The defaults are neutral (the hall exactly as designed).
struct Adjustments
{
    float reflections = 1.0f;      // early reflection gain (0 = off)
    float diffusion = 1.0f;        // 0 = clean copies (no allpass), 1 = the designed scattering
    float floor = 1.0f;            // floor reflection gain
    float brightness = 1.0f;       // surface cutoff multiplier
    float reverb = 1.0f;           // tail gain (0 = off)
    float decay = 1.0f;            // RT60 multiplier, 0.5..2
    float preDelay = 1.0f;         // tail pre-delay multiplier, 0.5..2
    float distanceExponent = geometry::distanceLawExponent; // 1 = free field (-6 dB per doubling)
    float air = 1.0f;              // air absorption multiplier, 0..2

    static constexpr float maxDecay = 2.0f;
};

// Everything a processing stage needs to know about the scene, set once per block.
struct Acoustics
{
    geometry::Point player, listener; // metres, already clamped to the room
    float roomSize = 20.0f;
    OutputMode mode = OutputMode::stereo;
    const Hall* hall = &halls[defaultHall];
    Adjustments adjust;

    // Longest delay any stage asks the History for: the longest reflection path
    // (geometry::maxImageDistance) plus the tail pre-delay, with margin.
    static constexpr double maxDelaySeconds = 0.5;

    // Longest audible tail after the input stops: the longest reverb (Church, 4 s) at the
    // Pro panel's maximum decay (x2), plus the longest delay, with margin. Reported to the
    // host; also how long a silent track keeps processing.
    static constexpr double tailSeconds = 10.0;
};
