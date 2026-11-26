#pragma once

#include "Geometry.h"
#include "OutputMode.h"

// The Pro panel: project-wide adjustments, in natural units. The defaults are neutral.
struct Adjustments
{
    float distanceExponent = geometry::distanceLawExponent; // 1 = free field (-6 dB per doubling)
};

// Everything the sound's path needs to know about the scene, set once per block.
struct Acoustics
{
    geometry::Point player, listener; // metres, already clamped to the room
    OutputMode mode = OutputMode::stereo;
    Adjustments adjust;

    // Which way the listener's head points, degrees clockwise (turned right). Binaural only -
    // the processor sets 0 in stereo; the tracked part of it only in realtime.
    float headYaw = 0.0f;

    // The physical delay times this (dsp/DelayStretch.h); `delayJump`: it changed in a pause,
    // so the delay jumps to it instead of fading.
    float delayScale = 1.0f;
    bool delayJump = false;
};
