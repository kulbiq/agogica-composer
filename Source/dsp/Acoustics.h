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
};

inline constexpr std::array<Hall, 5> halls { {
    { "Dry",          0.0f,  0.0f,  0.0f, 0.0f,  0.0f, 0.0f  },
    { "Studio",       3.0f,  0.6f,  0.3f, 0.2f,  5.0f, 0.10f },
    { "Chamber",      5.0f,  0.35f, 1.0f, 0.6f, 10.0f, 0.15f },
    { "Concert hall", 15.0f, 0.2f,  2.0f, 1.2f, 20.0f, 0.20f },
    { "Church",       20.0f, 0.1f,  4.0f, 2.2f, 30.0f, 0.25f },
} };

inline constexpr int defaultHall = 2; // Chamber

// Everything a processing stage needs to know about the scene, set once per block.
struct Acoustics
{
    geometry::Point player, listener; // metres, already clamped to the room
    float roomSize = 20.0f;
    OutputMode mode = OutputMode::stereo;
    const Hall* hall = &halls[defaultHall];

    // Longest delay any stage asks the History for: the longest reflection path
    // (geometry::maxImageDistance) plus the tail pre-delay, with margin.
    static constexpr double maxDelaySeconds = 0.5;

    // Longest audible tail after the input stops: the longest reverb (Church, 4 s) plus the
    // longest delay. Reported to the host; also how long a silent track keeps processing.
    static constexpr double tailSeconds = 5.0;
};
