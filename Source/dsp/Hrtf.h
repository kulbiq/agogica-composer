#pragma once

#include <vector>

// Head-related impulse responses for binaural output: SADIE II, KU100 dummy head,
// horizontal plane, 1-degree steps (see Resources/hrir/NOTICE, tools/make_hrir.py).
// One table per sample rate, shared by every instance in the process: the six embedded ones
// (44.1 to 192 kHz), and for any other rate one resampled from them when it is first asked for.
class Hrtf
{
public:
    static constexpr int numDirections = 360;

    // The table for this sample rate, loaded (or resampled) on first use. Call from
    // prepareToPlay, never from the audio thread: the first call allocates and may lock.
    static const Hrtf& forSampleRate (double sampleRate);

    // Index for a Composer azimuth (0 = ahead, +90 = right, clockwise). The SOFA source
    // is counter-clockwise (+90 = left); make_hrir.py already converted it.
    static int nearestIndex (float azimuthDegrees) noexcept;

    int taps() const noexcept               { return numTaps; }
    double sampleRate() const noexcept      { return rate; }

    // Impulse response for direction `index`, ear 0 = left, 1 = right, stored
    // time-reversed so FIR output is a forward dot product with the input history.
    const float* reversedIr (int index, int ear) const noexcept
    {
        return data.data() + ((size_t) index * 2 + (size_t) ear) * (size_t) numTaps;
    }

    Hrtf (const void* binary, int size);
    Hrtf (const Hrtf& measured, double newRate); // the same responses at another sample rate

private:
    int numTaps = 0;
    double rate = 0.0;
    std::vector<float> data;
};
