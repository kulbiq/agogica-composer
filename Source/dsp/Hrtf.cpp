#include "Hrtf.h"
#include <BinaryData.h>
#include <juce_core/juce_core.h>
#include <map>
#include <mutex>

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
}

Hrtf::Hrtf (const Hrtf& measured, double newRate)
    : numTaps (std::max (1, (int) std::lround (measured.numTaps * newRate / measured.rate))),
      rate (newRate),
      data ((size_t) numDirections * 2 * (size_t) numTaps)
{
    // Windowed-sinc resampling of each response, band-limited to the lower of the two rates.
    // Stored responses are time-reversed, and a reversed response resamples to the reversed
    // result as long as time is counted from the last tap, as here.
    const auto ratio = measured.rate / newRate;        // source samples per new sample
    const auto cutoff = std::min (1.0, newRate / measured.rate);
    constexpr auto halfWidth = 32.0;                    // zero crossings of the sinc on each side
    const auto pi = juce::MathConstants<double>::pi;

    for (size_t ir = 0; ir < (size_t) numDirections * 2; ++ir)
    {
        const auto* source = measured.data.data() + ir * (size_t) measured.numTaps;
        auto* resampled = data.data() + ir * (size_t) numTaps;

        for (int tap = 0; tap < numTaps; ++tap)
        {
            // The same instant in the source, both counted back from the response's start.
            const auto at = (measured.numTaps - 1) - (double) ((numTaps - 1) - tap) * ratio;
            const auto reach = halfWidth / cutoff;
            auto sum = 0.0;

            for (auto k = std::max (0, (int) std::ceil (at - reach)); k <= std::min (measured.numTaps - 1, (int) std::floor (at + reach)); ++k)
            {
                const auto x = (at - k) * cutoff;
                const auto sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
                const auto window = 0.5 + 0.5 * std::cos (pi * x / halfWidth); // Hann
                sum += source[k] * sinc * window;
            }

            // A response sampled more sparsely needs larger samples for the same gain.
            resampled[tap] = (float) (sum * cutoff * ratio);
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

    for (const auto& table : tables)
        if (std::abs (table.rate - sampleRate) < 1.0)
            return table;

    // Any other rate (32 kHz, 384 kHz...): resampled once from the nearest table above it, or
    // from the highest one. Used as measured, the head would shrink or grow by the rate ratio.
    static std::mutex lock;
    static std::map<int, std::unique_ptr<const Hrtf>> resampledTables;
    const std::lock_guard guard (lock);

    auto& resampled = resampledTables[(int) std::lround (sampleRate)];

    if (resampled == nullptr)
    {
        const auto* source = &tables[std::size (tables) - 1];

        for (const auto& table : tables)
            if (table.rate > sampleRate && table.rate < source->rate)
                source = &table;

        resampled = std::make_unique<const Hrtf> (*source, sampleRate);
    }

    return *resampled;
}

int Hrtf::nearestIndex (float azimuthDegrees) noexcept
{
    const auto i = (int) std::lround (azimuthDegrees);
    return ((i % numDirections) + numDirections) % numDirections;
}
