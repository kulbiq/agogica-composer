#include "EarlyReflections.h"

void EarlyReflections::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    for (size_t i = 0; i < placers.size(); ++i)
        placers[i].prepare (sampleRate, i < 5 ? Placer::Binaural::shortHrir : Placer::Binaural::panOnly);

    for (size_t i = 0; i < reflections.size(); ++i)
    {
        // Two short allpasses per reflection, lengths differing between reflections
        // (0.7-3.0 ms and 1.9-5.2 ms at any rate).
        auto& r = reflections[i];
        r.diffusion[0].prepare ((int) std::lround ((0.7 + 0.23 * (double) i) * sampleRate / 1000.0));
        r.diffusion[1].prepare ((int) std::lround ((1.9 + 0.37 * (double) i) * sampleRate / 1000.0));
    }

    diffusion.reset (sampleRate, 0.05);

    for (auto& r : reflections)
    {
        r.delay.prepare (sampleRate);
        r.gain.reset (sampleRate, 0.05);
        r.airCoefficient.reset (sampleRate, 0.05);
        r.airState = 0.0f;
    }

    snap = silent = true;
}

void EarlyReflections::setAcoustics (const Acoustics& a) noexcept
{
    const auto& hall = *a.hall;
    const auto& adjust = a.adjust;
    const auto direct = geometry::distance (a.player, a.listener);
    const auto directGain = geometry::distanceGain (direct, adjust.distanceExponent);
    const auto images = geometry::imageSources (a.player, a.listener, a.roomSize, std::max (hall.ceiling, geometry::earHeight));
    const auto reflectivity = std::sqrt (1.0f - hall.absorption); // amplitude per bounce
    constexpr size_t floorImage = 4; // see geometry::imageSources

    for (size_t i = 0; i < reflections.size(); ++i)
    {
        auto& r = reflections[i];
        const auto& image = images[i];

        // Physical level relative to the direct sound: 1/distance and absorption. Like the
        // direct sound, nothing gets louder inside the 1 m reference (a player standing on
        // a wall is its own image: distance 0 would divide by zero).
        const auto gain = hall.ceiling <= 0.0f ? 0.0f
                                               : directGain * std::max (direct, geometry::referenceDistance)
                                                     / std::max (image.distance, geometry::referenceDistance)
                                                     * (i == floorImage ? std::sqrt (1.0f - hall.floorAbsorption) * adjust.floor
                                                                        : std::pow (reflectivity, (float) image.bounces))
                                                     * adjust.reflections;

        // Air over the path and the surface's own loss of highs: the lower cutoff wins.
        const auto airCutoff = geometry::airCutoffHz (image.distance, adjust.air);
        const auto surfaceCutoff = hall.surfaceCutoffHz * adjust.brightness;
        const auto cutoff = surfaceCutoff <= 0.0f ? airCutoff
                          : airCutoff <= 0.0f     ? surfaceCutoff
                                                  : std::min (airCutoff, surfaceCutoff);
        const auto air = dsp::onePoleCoefficient (cutoff, sampleRate);

        r.delay.setTarget (image.distance / geometry::speedOfSound * (float) sampleRate);

        auto& placer = placers[(size_t) placerOf[i]];
        placer.setMode (a.mode);
        placer.setDirection (geometry::azimuthDegrees (image.position, a.listener) - a.headYaw, snap);

        if (snap)
        {
            r.gain.setCurrentAndTargetValue (gain);
            r.airCoefficient.setCurrentAndTargetValue (air);
        }
        else
        {
            r.gain.setTargetValue (gain);
            r.airCoefficient.setTargetValue (air);
        }
    }

    const auto g = 0.6f * adjust.diffusion;
    if (snap)
        diffusion.setCurrentAndTargetValue (g);
    else
        diffusion.setTargetValue (g);

    snap = false;
    silent = (hall.ceiling <= 0.0f || adjust.reflections <= 0.0f) && ! reflections[0].gain.isSmoothing() && reflections[0].gain.getCurrentValue() <= 0.0f;
}

void EarlyReflections::processSample (const History& history, float& left, float& right) noexcept
{
    if (silent)
        return;

    placerInput.fill (0.0f);
    const auto g = diffusion.getNextValue();

    for (size_t i = 0; i < reflections.size(); ++i)
    {
        auto& r = reflections[i];
        r.airState += r.airCoefficient.getNextValue() * (r.delay.read (history) - r.airState);
        const auto diffused = r.diffusion[1].process (r.diffusion[0].process (r.airState, g), g);
        placerInput[(size_t) placerOf[i]] += diffused * r.gain.getNextValue();
    }

    for (size_t i = 0; i < placers.size(); ++i)
        placers[i].process (placerInput[i], left, right);
}
