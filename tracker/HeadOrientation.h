#pragma once

namespace tracker
{
// Where a head points, in degrees. Yaw: 0 = straight ahead, + = turned to the person's right.
struct HeadOrientation
{
    float yawDegrees = 0, pitchDegrees = 0, rollDegrees = 0;
};
} // namespace tracker
