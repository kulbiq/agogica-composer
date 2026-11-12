#pragma once

#include <algorithm>
#include <cmath>
#include "Decode.h" // wrap

namespace engines
{
// Turns the engine's output (one value per camera frame: ~8 Hz on a slow PC, 30 on a fast one)
// into a heading for every output tick (60 Hz), without the steps a slow frame rate makes,
// for the listener's ears and the dial alike:
// - the target runs on from the last output at its own speed, for at most one frame interval
//   (hides the steps; a stop overshoots by at most one frame's motion, which the follower
//   then takes back smoothly);
// - a critically damped follower moves towards it: no jumps, no velocity jumps, no ringing.
// Degrees, seconds; works across +-180. Not thread-safe (PoseOutput locks around it).
class PoseSmoother
{
public:
    static constexpr float timeConstant = 0.035f; // the follower's lag
    static constexpr float maxSpeed = 720.0f;     // degrees per second, for the run-on

    // A new output of the engine (the filter's heading) at `time`.
    void push (float yaw, double time)
    {
        if (! started)
        {
            position = yaw; // unwrapped
            velocity = 0;
            started = true;
        }
        else if (time > lastTime)
        {
            interval = (float) std::min (time - lastTime, 0.2);
            speed = std::clamp (wrap (yaw - lastYaw) / (float) (time - lastTime), -maxSpeed, maxSpeed);
        }
        lastYaw = yaw;
        lastTime = time;
    }

    // Nothing to follow (no head): the next push starts afresh.
    void reset() { started = false, speed = 0, interval = 0; }

    bool isStarted() const { return started; }

    // The smoothed heading at `time` (call with increasing times).
    float step (double time)
    {
        if (! started)
            return 0;

        const auto runOn = (float) std::clamp (time - lastTime, 0.0, (double) interval);
        const auto target = lastYaw + speed * runOn;

        // Exact critically damped step towards a target held for dt: stable for any dt.
        const auto dt = (float) std::max (0.0, time - stepTime);
        stepTime = time;
        const auto w = 1.0f / timeConstant;
        const auto error = wrap (position - target); // shortest way round
        const auto k = velocity + w * error;
        const auto decay = std::exp (-w * dt);
        position = target + (error + k * dt) * decay;
        velocity = (velocity - w * k * dt) * decay;
        return wrap (position);
    }

private:
    bool started = false;
    float position = 0, velocity = 0;
    float lastYaw = 0, speed = 0, interval = 0;
    double lastTime = 0, stepTime = 0;
};
} // namespace engines
