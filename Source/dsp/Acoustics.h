#pragma once

#include "Geometry.h"

enum class OutputMode { stereo, binaural };

// The Pro panel: project-wide adjustments, in natural units. The defaults are neutral.
struct Adjustments
{
    float distanceExponent = geometry::distanceLawExponent; // 1 = free field (-6 dB per doubling)
    float air = 1.0f;              // air absorption multiplier, 0..2
};

// Everything a processing stage needs to know about the scene, set once per block.
struct Acoustics
{
    geometry::Point player, listener; // metres, already clamped to the room
    float roomSize = 20.0f;
    OutputMode mode = OutputMode::stereo;
    Adjustments adjust;

    // Listener's head rotation, degrees clockwise (head turned right). Binaural only and
    // realtime only - the processor sets 0 otherwise.
    float headYaw = 0.0f;
};
