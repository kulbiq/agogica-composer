#include "DirectPath.h"

void DirectPath::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
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
               geometry::distanceGain (distance, a.adjust.distanceExponent),
               geometry::airCutoffHz (distance, a.adjust.air));
}

void DirectPath::setTarget (float azimuthDegrees, float newGain, float airCutoffHz) noexcept
{
    const auto coefficient = dsp::onePoleCoefficient (airCutoffHz, sampleRate);
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

void DirectPath::process (const float* in, float* left, float* right, int n) noexcept
{
    alignas (32) float signal[dsp::maxBlock];

    // The air filter's state carries from sample to sample; cheap next to the placing.
    for (int i = 0; i < n; ++i)
    {
        airState += airCoefficient.getNextValue() * (in[i] - airState);
        signal[i] = airState * gain.getNextValue();
    }

    placer.process (signal, left, right, n);
}
