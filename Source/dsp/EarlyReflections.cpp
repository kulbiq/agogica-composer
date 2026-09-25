#include "EarlyReflections.h"

void EarlyReflections::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    for (size_t i = 0; i < placers.size(); ++i)
        placers[i].prepare (sampleRate, i < 5 ? Placer::Binaural::shortHrir : Placer::Binaural::panOnly);

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
    const auto direct = geometry::distance (a.player, a.listener);
    const auto directGain = geometry::distanceGain (direct);
    const auto images = geometry::imageSources (a.player, a.listener, a.roomSize, std::max (hall.ceiling, geometry::earHeight));
    const auto reflectivity = std::sqrt (1.0f - hall.absorption); // amplitude per bounce

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
                                                     * std::pow (reflectivity, (float) image.bounces);
        const auto air = dsp::onePoleCoefficient (geometry::airCutoffHz (image.distance), sampleRate);

        r.delay.setTarget (image.distance / geometry::speedOfSound * (float) sampleRate);

        auto& placer = placers[(size_t) placerOf[i]];
        placer.setMode (a.mode);
        placer.setDirection (geometry::azimuthDegrees (image.position, a.listener), snap);

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

    snap = false;
    silent = hall.ceiling <= 0.0f && ! reflections[0].gain.isSmoothing() && reflections[0].gain.getCurrentValue() <= 0.0f;
}

void EarlyReflections::processSample (const History& history, float& left, float& right) noexcept
{
    if (silent)
        return;

    placerInput.fill (0.0f);

    for (size_t i = 0; i < reflections.size(); ++i)
    {
        auto& r = reflections[i];
        r.airState += r.airCoefficient.getNextValue() * (r.delay.read (history) - r.airState);
        placerInput[(size_t) placerOf[i]] += r.airState * r.gain.getNextValue();
    }

    for (size_t i = 0; i < placers.size(); ++i)
        placers[i].process (placerInput[i], left, right);
}
