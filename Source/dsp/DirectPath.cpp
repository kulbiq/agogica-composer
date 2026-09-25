#include "DirectPath.h"

void DirectPath::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    delay.prepare (sampleRate);
    placer.prepare (sampleRate);

    // Smoothing times stay in milliseconds whatever the sample rate.
    gain.reset (sampleRate, 0.05);
    airCoefficient.reset (sampleRate, 0.05);
    airState = 0.0f;
    snap = true;
}

void DirectPath::setAcoustics (const Acoustics& a) noexcept
{
    const auto distance = geometry::distance (a.player, a.listener);

    setMode (a.mode);
    // Relative to where the head points: turning right moves everything to the left.
    setTarget (geometry::azimuthDegrees (a.player, a.listener) - a.headYaw,
               geometry::delayMs (a.player, a.listener) / 1000.0f,
               geometry::distanceGain (distance, a.adjust.distanceExponent),
               geometry::airCutoffHz (distance, a.adjust.air));
}

void DirectPath::setTarget (float azimuthDegrees, float delaySeconds, float newGain, float airCutoffHz) noexcept
{
    const auto coefficient = dsp::onePoleCoefficient (airCutoffHz, sampleRate);

    delay.setTarget (delaySeconds * (float) sampleRate);
    placer.setDirection (azimuthDegrees, snap);

    if (snap)
    {
        gain.setCurrentAndTargetValue (newGain);
        airCoefficient.setCurrentAndTargetValue (coefficient);
        snap = false;
        return;
    }

    gain.setTargetValue (newGain);
    airCoefficient.setTargetValue (coefficient);
}

void DirectPath::processSample (const History& history, float& left, float& right) noexcept
{
    airState += airCoefficient.getNextValue() * (delay.read (history) - airState);
    placer.process (airState * gain.getNextValue(), left, right);
}
