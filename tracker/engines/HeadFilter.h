#pragma once

// Temporal gate for the head's yaw (degrees, camera frame: 0 = facing the camera).
//
// 1. Where is the head? A hidden Markov model over yaw in 2° bins (a grid Bayes filter):
//    - transition: drift with the current velocity, blur as far as a head can turn in dt,
//      plus a small "teleport" probability - that's what lets a genuinely new direction win,
//      and it is set so that it takes exactly `confirmFrames` consistent frames;
//    - emission: each measurement is believed by its confidence, with explicit terms for the
//      model's front/back mirror error (heading 180° - z, measured on recorded sessions) and
//      for plain outliers.
//    One wild frame barely moves the probability; consistent evidence elsewhere builds up
//    until that direction wins.
// 2. What does Composer hear? A follower that moves towards the estimate, never faster than
//    `maxSpeed`, and after a switch of direction only at `reacquireSpeed`.
// 3. Lost head: the output holds (0.5 s near the front, 3 s turned away), then releases.

#include "Decode.h"

namespace engines
{
struct FilterParams
{
    float sigmaZ = 5.0f;          // measurement noise, degrees
    float turnSpeed = 150.0f;     // how fast a head can plausibly turn (°/s) - transition blur
    float maxSpeed = 300.0f;      // output speed limit (°/s)
    float reacquireSpeed = 90.0f; // output speed after a switch of direction (°/s)
    float flipWeight = 0.3f;      // share of a doubtful measurement explained as the mirror error
    float outlierWeight = 0.05f;  // share explained as noise anywhere
    int confirmFrames = 6;        // consistent frames until a new direction wins
    float frameRate = 30.0f;      // the rate confirmFrames is meant at (for the teleport calibration)
};

struct Measurement
{
    float yaw = 0;        // heading, degrees
    float confidence = 1; // 0..1 (hemisphere certainty etc.)
};

struct FilterState
{
    float estimate = 0;     // where the head most likely points
    float output = 0;       // what Composer hears (rate-limited follower of the estimate)
    float certainty = 0;    // posterior mass near the estimate, 0..1
    bool tracking = false;  // false once a lost head is released
    bool reacquiring = false;
};

// The model's typical error: the wrong hemisphere, i.e. the heading mirrored across the ear axis.
inline float mirrorOf (float yaw) { return wrap (180.0f - yaw); }

class HeadFilter
{
public:
    static constexpr int bins = 180;
    static constexpr float binDegrees = 360.0f / bins;

    explicit HeadFilter (FilterParams p = {}) { setParams (p); }

    void setParams (FilterParams p)
    {
        params = p;
        teleport = calibrateTeleport (p);
    }

    const FilterParams& getParams() const { return params; }
    float teleportProbability() const { return teleport; }

    // One camera frame: a measurement, or nothing (no head). dt in seconds.
    FilterState update (std::optional<Measurement> m, float dt)
    {
        dt = std::clamp (dt, 1.0e-3f, 0.5f);

        if (! initialised)
        {
            if (! m)
                return state;
            reset (m->yaw);
        }

        predict (dt);

        if (m)
        {
            observe (*m);
            lostFor = 0;
        }
        else
        {
            lostFor += dt;
        }

        const auto previous = state.estimate;
        state.estimate = peakMean (state.certainty);

        // A switch of direction (the posterior's peak moved far at once): the output
        // approaches it slowly, and the velocity isn't a real turn.
        const auto jump = wrap (state.estimate - previous);
        if (std::abs (jump) > 45.0f)
        {
            state.reacquiring = true;
            velocity = 0;
        }
        else if (m)
        {
            velocity += 0.3f * (jump / dt - velocity);
        }

        follow (dt);

        // Lost: hold, then release (near the front: the listener probably left).
        const auto holdFor = std::abs (state.output) < 35.0f ? 0.5f : 3.0f;
        state.tracking = lostFor < holdFor;
        if (! state.tracking)
            initialised = false; // start fresh from the next measurement

        return state;
    }

    const std::array<float, bins>& posterior() const { return prob; }

private:
    static float angleOf (int bin) { return wrap ((float) bin * binDegrees); }
    static int binOf (float degrees) { return ((int) std::lround (wrap (degrees) / binDegrees) % bins + bins) % bins; }

    void reset (float yaw)
    {
        prob.fill (0.0f);
        prob[(size_t) binOf (yaw)] = 1.0f;
        state = { yaw, yaw, 1.0f, true, false };
        velocity = 0;
        lostFor = 0;
        initialised = true;
    }

    void predict (float dt)
    {
        // Drift by velocity·dt, blur by turnSpeed·dt: one wrapped Gaussian kernel.
        const auto shift = velocity * dt / binDegrees;
        const auto sigma = std::max (0.6f, params.turnSpeed * dt / binDegrees);
        const auto reach = (int) std::ceil (3.0f * sigma + std::abs (shift));

        std::array<float, bins> next {};
        for (int from = 0; from < bins; ++from)
        {
            if (prob[(size_t) from] < 1.0e-12f)
                continue;
            for (int d = -reach; d <= reach; ++d)
            {
                const auto x = ((float) d - shift) / sigma;
                next[(size_t) ((from + d) % bins + bins) % bins] += prob[(size_t) from] * std::exp (-0.5f * x * x);
            }
        }

        // Teleport: a small chance of being anywhere.
        normalise (next);
        for (auto& v : next)
            v = (1.0f - teleport) * v + teleport / (float) bins;
        prob = next;
    }

    void observe (const Measurement& m)
    {
        const auto c = std::clamp (m.confidence, 0.0f, 1.0f);
        const auto mirror = mirrorOf (m.yaw);
        const auto s2 = 2.0f * params.sigmaZ * params.sigmaZ;
        const auto uniform = 1.0f / 360.0f;
        const auto norm = 1.0f / (params.sigmaZ * 2.5066283f);

        for (int b = 0; b < bins; ++b)
        {
            const auto a = angleOf (b);
            const auto dz = wrap (a - m.yaw), dm = wrap (a - mirror);
            const auto direct = norm * std::exp (-dz * dz / s2);
            const auto mirrored = norm * std::exp (-dm * dm / s2);
            // Confident: the reading. Doubtful: partly the mirror error, partly anywhere.
            const auto likelihood = c * direct
                                  + (1.0f - c) * (params.flipWeight * mirrored + (1.0f - params.flipWeight) * uniform)
                                  + params.outlierWeight * uniform;
            prob[(size_t) b] *= likelihood;
        }
        normalise (prob);
    }

    // Circular mean of the posterior within ±15° of its peak; `mass` = the mass there.
    float peakMean (float& mass) const
    {
        const auto peak = (int) std::distance (prob.begin(), std::max_element (prob.begin(), prob.end()));
        const auto reach = (int) (15.0f / binDegrees);
        float s = 0, c = 0;
        mass = 0;
        for (int d = -reach; d <= reach; ++d)
        {
            const auto b = ((peak + d) % bins + bins) % bins;
            const auto a = toRadians (angleOf (b));
            s += prob[(size_t) b] * std::sin (a);
            c += prob[(size_t) b] * std::cos (a);
            mass += prob[(size_t) b];
        }
        return toDegrees (std::atan2 (s, c));
    }

    void follow (float dt)
    {
        const auto error = wrap (state.estimate - state.output);
        if (state.reacquiring && std::abs (error) < 5.0f)
            state.reacquiring = false;

        const auto limit = (state.reacquiring ? params.reacquireSpeed : params.maxSpeed) * dt;
        const auto smooth = error * (1.0f - std::exp (-dt / 0.05f)); // ~50 ms follower
        state.output = wrap (state.output + std::clamp (smooth, -limit, limit));
    }

    static void normalise (std::array<float, bins>& a)
    {
        float sum = 0;
        for (auto v : a)
            sum += v;
        if (sum > 0)
            for (auto& v : a)
                v /= sum;
    }

    // The teleport probability for which a converged filter (at 0°) takes exactly
    // `confirmFrames` fully confident frames at 120° to move its estimate there.
    static float calibrateTeleport (const FilterParams& params)
    {
        auto framesToSwitch = [&] (float t)
        {
            HeadFilter f (params, t);
            const auto dt = 1.0f / params.frameRate;
            for (int i = 0; i < 30; ++i)
                f.update (Measurement { 0.0f, 1.0f }, dt);
            for (int i = 1; i <= 60; ++i)
                if (std::abs (wrap (f.update (Measurement { 120.0f, 1.0f }, dt).estimate - 120.0f)) < 15.0f)
                    return i;
            return 61;
        };

        float lo = 1.0e-12f, hi = 0.5f; // more teleport = faster switching
        for (int i = 0; i < 40; ++i)
        {
            const auto mid = std::sqrt (lo * hi); // search on a log scale
            if (framesToSwitch (mid) > params.confirmFrames)
                lo = mid;
            else
                hi = mid;
        }
        return hi;
    }

    HeadFilter (FilterParams p, float t) : params (p), teleport (t) {} // no calibration (used by it)

    FilterParams params;
    float teleport = 1.0e-3f;
    std::array<float, bins> prob {};
    FilterState state;
    float velocity = 0, lostFor = 0;
    bool initialised = false;
};

// How sure 6DRepNet360 is about front vs back: its Euler pitch and roll sit near 0° (facing
// the camera) or near ±180° (facing away); halfway means it can't tell (seen on every
// suspicious frame of the recorded sessions).
inline float hemisphereCertainty (float pitch, float roll)
{
    return std::abs (std::cos (toRadians (pitch)) * std::cos (toRadians (roll)));
}
} // namespace engines
