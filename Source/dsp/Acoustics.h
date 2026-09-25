#pragma once

#include "Geometry.h"

enum class OutputMode { stereo, binaural };

// Everything a processing stage needs to know about the scene, set once per block.
struct Acoustics
{
    geometry::Point player, listener; // metres, already clamped to the room
    float roomSize = 20.0f;
    OutputMode mode = OutputMode::stereo;

    // Longest delay any stage may ask the History for.
    static constexpr double maxDelaySeconds = 0.25;
};
