#pragma once

#include "../PluginProcessor.h"

// A new track joining the stage, and where its player sits.
namespace stage
{
// Copies the stage's active scene and room/listener into self and moves
// self's player to a free seat (see freeSeat). Marks self placed.
void join (ComposerProcessor& self);

// Where a new player sits, given the (clamped) positions already taken: rows of 7 in front of
// the listener, centre-out, spaced to fit the room (2 m in a 20 m room, down to 0.75 m), at
// least 0.5 m from the walls; when the front is full, wider rows, then rows nearer the
// listener, then beside and behind. A full room gets the point farthest from everyone.
geometry::Point freeSeat (geometry::Point listener, float roomSize, const std::vector<geometry::Point>& taken);
} // namespace stage
