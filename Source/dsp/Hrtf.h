#pragma once

#include <vector>

// Head-related impulse responses for binaural output: SADIE II, KU100 dummy head,
// horizontal plane, 1-degree steps (see Resources/hrir/NOTICE, tools/make_hrir.py).
// One table per supported sample rate, shared by every instance in the process.
class Hrtf
{
public:
    static constexpr int numDirections = 360;

    // The table for this sample rate, loaded on first use. Call from prepareToPlay,
    // never from the audio thread (the first call copies the tables).
    static const Hrtf& forSampleRate (double sampleRate);

    // Index for a Composer azimuth (0 = ahead, +90 = right, clockwise). The SOFA source
    // is counter-clockwise (+90 = left); make_hrir.py already converted it.
    static int nearestIndex (float azimuthDegrees) noexcept;

    int taps() const noexcept               { return numTaps; }
    int shortTaps() const noexcept          { return numShortTaps; }
    double sampleRate() const noexcept      { return rate; }

    // Impulse response for direction `index`, ear 0 = left, 1 = right, stored
    // time-reversed so FIR output is a forward dot product with the input history.
    const float* reversedIr (int index, int ear) const noexcept
    {
        return data.data() + ((size_t) index * 2 + (size_t) ear) * (size_t) numTaps;
    }

    // The first ~2 ms of each response (96 taps at 48 kHz) with a short fade-out, for the
    // many quieter paths (reflections). Keeps the interaural time difference and the main
    // spectral cues (>= 93 % of each response's energy at 48 kHz) at a fraction of the cost.
    const float* shortReversedIr (int index, int ear) const noexcept
    {
        return shortData.data() + ((size_t) index * 2 + (size_t) ear) * (size_t) numShortTaps;
    }

    Hrtf (const void* binary, int size);

private:
    int numTaps = 0, numShortTaps = 0;
    double rate = 0.0;
    std::vector<float> data, shortData;
};
