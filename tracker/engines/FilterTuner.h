#pragma once

// Adapts HeadFilter's parameters to the last ~8 s of raw measurements, and lets the user pin
// any of them. Statistics come from the RAW series only - measuring the filter's own output
// would feed back (heavy smoothing -> smaller measured speed/noise -> heavier smoothing ->
// a frozen output).

#include "HeadFilter.h"
#include <deque>

namespace engines
{
enum class Param { sigmaZ, turnSpeed, maxSpeed, reacquireSpeed, flipWeight, outlierWeight, confirmFrames, count };

inline const char* paramName (Param p)
{
    static const char* names[] { "sigmaZ", "turnSpeed", "maxSpeed", "reacquireSpeed", "flipWeight", "outlierWeight", "confirmFrames" };
    return names[(int) p];
}

inline float& field (FilterParams& f, Param p)
{
    switch (p)
    {
        case Param::sigmaZ:         return f.sigmaZ;
        case Param::turnSpeed:      return f.turnSpeed;
        case Param::maxSpeed:       return f.maxSpeed;
        case Param::reacquireSpeed: return f.reacquireSpeed;
        case Param::flipWeight:     return f.flipWeight;
        case Param::outlierWeight:  return f.outlierWeight;
        case Param::confirmFrames:
        case Param::count:          break;
    }
    return f.sigmaZ; // confirmFrames is an int: handled separately
}

inline float field (const FilterParams& f, Param p) { return field (const_cast<FilterParams&> (f), p); }

class FilterTuner
{
public:
    static constexpr double windowSeconds = 8.0;
    static constexpr float minTurnSpeed = 60.0f; // a still listener can always turn suddenly

    // One frame's raw reading (seconds since start, degrees, 0..1 hemisphere certainty).
    void add (double time, std::optional<float> yaw, float certainty)
    {
        samples.push_back ({ time, yaw, certainty });
        while (! samples.empty() && samples.front().time < time - windowSeconds)
            samples.pop_front();
    }

    // The parameters to use now: adapted where not pinned. Each adapted value glides towards
    // its target (~2 s time constant) so the filter's character doesn't wobble.
    FilterParams tuned (double time, const std::array<std::optional<float>, (size_t) Param::count>& pins)
    {
        const auto target = targets();
        const auto dt = (float) (time - lastTime);
        lastTime = time;
        const auto k = 1.0f - std::exp (-std::max (0.0f, dt) / 2.0f);

        FilterParams out = current;
        for (int i = 0; i < (int) Param::count; ++i)
        {
            const auto p = (Param) i;
            if (p == Param::confirmFrames)
            {
                out.confirmFrames = pins[(size_t) i] ? (int) std::lround (*pins[(size_t) i]) : FilterParams {}.confirmFrames;
                continue;
            }
            auto& value = field (out, p);
            if (pins[(size_t) i])
                value = *pins[(size_t) i];
            else if (target)
                value += k * (field (*target, p) - value);
        }
        current = out;
        return out;
    }

    // The statistics' targets, or nothing if there's too little data yet.
    std::optional<FilterParams> targets() const
    {
        std::vector<float> diffs, speeds;
        int doubtful = 0, measured = 0;
        for (size_t i = 1; i < samples.size(); ++i)
        {
            const auto& a = samples[i - 1];
            const auto& b = samples[i];
            if (b.yaw)
            {
                ++measured;
                doubtful += b.certainty < 0.5f;
            }
            if (a.yaw && b.yaw && b.time > a.time)
            {
                const auto d = wrap (*b.yaw - *a.yaw);
                diffs.push_back (d);
                speeds.push_back (std::abs (d) / (float) (b.time - a.time));
            }
        }
        if (diffs.size() < 30)
            return std::nullopt;

        // Robust spread of frame-to-frame changes around the current motion (MAD).
        const auto median = [] (std::vector<float> v)
        {
            std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
            return v[v.size() / 2];
        };
        const auto centre = median (diffs);
        std::vector<float> deviations;
        for (auto d : diffs)
            deviations.push_back (std::abs (d - centre));
        const auto mad = std::max (0.2f, median (deviations));

        // Outliers: changes far outside that spread (the model's jumps).
        std::vector<float> goodSpeeds;
        int outliers = 0;
        for (size_t i = 0; i < diffs.size(); ++i)
        {
            if (std::abs (diffs[i] - centre) > 6.0f * mad + 10.0f)
                ++outliers;
            else
                goodSpeeds.push_back (speeds[i]);
        }
        std::sort (goodSpeeds.begin(), goodSpeeds.end());
        const auto speed95 = goodSpeeds.empty() ? 0.0f : goodSpeeds[(size_t) ((float) (goodSpeeds.size() - 1) * 0.95f)];

        FilterParams t;
        // Frame-to-frame differences of a still, noisy reading spread by sqrt 2 * sigma.
        t.sigmaZ = std::clamp (1.4826f * mad / 1.41421356f, 1.5f, 15.0f);
        t.turnSpeed = std::clamp (1.5f * speed95, minTurnSpeed, 400.0f);
        t.maxSpeed = std::clamp (2.0f * speed95, 150.0f, 540.0f);
        t.reacquireSpeed = FilterParams {}.reacquireSpeed;
        t.flipWeight = std::clamp (0.2f + 2.0f * (float) doubtful / (float) std::max (1, measured), 0.2f, 0.6f);
        t.outlierWeight = std::clamp (0.02f + 2.0f * (float) outliers / (float) diffs.size(), 0.02f, 0.2f);
        return t;
    }

private:
    struct Sample { double time; std::optional<float> yaw; float certainty; };
    std::deque<Sample> samples;
    FilterParams current;
    double lastTime = 0;
};
} // namespace engines
