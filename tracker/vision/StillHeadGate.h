#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>
#include <optional>
#include "ImageTensor.h"

namespace tracker
{
// Skips the models while the head hasn't moved: the last head box, as a 16x16 grey thumbnail,
// is compared with the same box in the frame of the last full estimate (mean absolute
// difference, 0-255). The pose model is most of the tracker's CPU, and a composer's head is
// often still. Tracking thread only.
class StillHeadGate
{
public:
    static constexpr float threshold = 4.0f;  // see `difference`: above it, run the models
    static constexpr double maxAgeSeconds = 1.0; // a full estimate at least this often

    // True when `frame` may reuse the estimate made at the last `reset`.
    bool unchanged (const juce::Image& frame, double now)
    {
        if (! reference || now - referenceTime >= maxAgeSeconds)
            return false;
        difference = meanDifference (thumbnail (frame, box), *reference);
        return difference < threshold;
    }

    // After a full estimate: the reference is the head `box` (normalised) in this frame.
    void reset (const juce::Image& frame, juce::Rectangle<float> headBox, double now)
    {
        box = headBox;
        reference = thumbnail (frame, box);
        referenceTime = now;
    }

    // No head: nothing to compare with until the next one.
    void clear() { reference.reset(); }

    float difference = 0; // of the last `unchanged` check (diagnostics)

private:
    static constexpr int side = 16;
    using Thumbnail = std::array<float, side * side>;

    // Grey levels of `area` (normalised), each cell the mean of a grid of samples.
    static Thumbnail thumbnail (const juce::Image& image, juce::Rectangle<float> area)
    {
        constexpr int samplesPerCellSide = 4;
        const FramePixels pixels (image);
        auto grey = [&] (int x, int y)
        {
            float rgb[3];
            pixels.read (juce::jlimit (0, pixels.width() - 1, x), juce::jlimit (0, pixels.height() - 1, y), rgb);
            return 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2]; // luma (ITU-R BT.601)
        };

        const auto left = area.getX() * (float) pixels.width(), top = area.getY() * (float) pixels.height();
        const auto cellWidth = area.getWidth() * (float) pixels.width() / side, cellHeight = area.getHeight() * (float) pixels.height() / side;
        Thumbnail cells {};
        for (int cellY = 0; cellY < side; ++cellY)
            for (int cellX = 0; cellX < side; ++cellX)
            {
                float sum = 0;
                for (int sampleY = 0; sampleY < samplesPerCellSide; ++sampleY)
                    for (int sampleX = 0; sampleX < samplesPerCellSide; ++sampleX)
                        sum += grey ((int) (left + ((float) cellX + ((float) sampleX + 0.5f) / samplesPerCellSide) * cellWidth),
                                     (int) (top + ((float) cellY + ((float) sampleY + 0.5f) / samplesPerCellSide) * cellHeight));
                cells[(size_t) (cellY * side + cellX)] = sum / (samplesPerCellSide * samplesPerCellSide);
            }
        return cells;
    }

    static float meanDifference (const Thumbnail& a, const Thumbnail& b)
    {
        float sum = 0;
        for (size_t i = 0; i < a.size(); ++i)
            sum += std::abs (a[i] - b[i]);
        return sum / (float) a.size();
    }

    std::optional<Thumbnail> reference;
    juce::Rectangle<float> box;
    double referenceTime = 0;
};
} // namespace tracker
