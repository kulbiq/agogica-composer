#include "Hrtf.h"
#include <BinaryData.h>
#include <juce_core/juce_core.h>

Hrtf::Hrtf (const void* binary, int size)
{
    juce::MemoryInputStream in (binary, (size_t) size, false);

    char magic[4] {};
    in.read (magic, 4);
    const auto version = in.readInt();
    const auto count = in.readInt();
    numTaps = in.readInt();
    rate = in.readFloat();

    jassert (std::memcmp (magic, "HRIR", 4) == 0 && version == 1 && count == numDirections);
    juce::ignoreUnused (version, count);

    data.resize ((size_t) numDirections * 2 * (size_t) numTaps);

    for (size_t ir = 0; ir < (size_t) numDirections * 2; ++ir)
        for (int tap = numTaps; --tap >= 0;)
            data[ir * (size_t) numTaps + (size_t) tap] = in.readFloat();

    // Short responses: the head of each (reversed, so the end of each reversed array),
    // with a raised-cosine fade over the last 16 taps at 48 kHz (a hard cut adds ripple).
    const auto scale = rate / 48000.0;
    numShortTaps = std::min (numTaps, (int) std::lround (96.0 * scale));
    const auto fade = (int) std::lround (16.0 * scale);
    shortData.resize ((size_t) numDirections * 2 * (size_t) numShortTaps);

    for (size_t ir = 0; ir < (size_t) numDirections * 2; ++ir)
    {
        const auto* full = data.data() + ir * (size_t) numTaps + (size_t) (numTaps - numShortTaps);
        auto* shortIr = shortData.data() + ir * (size_t) numShortTaps;

        for (int j = 0; j < numShortTaps; ++j)
        {
            // j = 0 is the latest tap of the shortened response.
            const auto g = j < fade ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) j / (float) fade) : 1.0f;
            shortIr[j] = full[j] * g;
        }
    }
}

const Hrtf& Hrtf::forSampleRate (double sampleRate)
{
    // Thread-safe one-time initialisation (hosts may prepare instances in parallel).
    static const Hrtf tables[] {
        { BinaryData::sadie2_ku100_44100_bin,  BinaryData::sadie2_ku100_44100_binSize },
        { BinaryData::sadie2_ku100_48000_bin,  BinaryData::sadie2_ku100_48000_binSize },
        { BinaryData::sadie2_ku100_88200_bin,  BinaryData::sadie2_ku100_88200_binSize },
        { BinaryData::sadie2_ku100_96000_bin,  BinaryData::sadie2_ku100_96000_binSize },
        { BinaryData::sadie2_ku100_176400_bin, BinaryData::sadie2_ku100_176400_binSize },
        { BinaryData::sadie2_ku100_192000_bin, BinaryData::sadie2_ku100_192000_binSize },
    };

    // ponytail: other rates use the nearest table, which shifts the HRTF spectrum by the
    // rate ratio. Add a table (tools/make_hrir.py) if a host needs another rate.
    const auto* best = &tables[0];

    for (const auto& t : tables)
        if (std::abs (t.rate - sampleRate) < std::abs (best->rate - sampleRate))
            best = &t;

    return *best;
}

int Hrtf::nearestIndex (float azimuthDegrees) noexcept
{
    const auto i = (int) std::lround (azimuthDegrees);
    return ((i % numDirections) + numDirections) % numDirections;
}
