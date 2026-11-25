#pragma once

#include "../PluginProcessor.h"

// Where the listener and a track's player are, as everyone sees them: inside the room and on
// the 1 cm grid. (The parameters keep what was set, also outside a room that has shrunk.)
namespace stage
{
// Positions are stored on a 1 cm grid; the float range conversion leaves residue like -5.6e-7.
inline float snapToCentimetre (float metres)
{
    return std::round (metres * 100.0f) / 100.0f;
}

inline geometry::Point listenerPlace (const ComposerProcessor& p)
{
    return geometry::clampToRoom ({ snapToCentimetre (p.getParam (params::listenerX)), snapToCentimetre (p.getParam (params::listenerY)) },
                                  p.getParam (params::roomSize));
}

inline geometry::Point playerPlace (const ComposerProcessor& p, float roomSize)
{
    return geometry::clampToRoom ({ snapToCentimetre (p.getParam (params::playerX)), snapToCentimetre (p.getParam (params::playerY)) }, roomSize);
}
} // namespace stage
