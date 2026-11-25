#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>
#include <optional>

namespace engines
{
// Skips the models while the head hasn't moved: the last head box, as a 16x16 grey thumbnail,
// is compared with the same box in the frame of the last full estimate (mean absolute
// difference, 0-255). The pose model is most of the tracker's CPU, and a composer's head is
// often still. Engine worker thread only.
class StillGate
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

    // Grey levels of `area` (normalised), each cell the mean of a 4x4 grid of samples.
    static Thumbnail thumbnail (const juce::Image& image, juce::Rectangle<float> area)
    {
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        const bool argb = image.getFormat() == juce::Image::ARGB;
        auto grey = [&] (int x, int y)
        {
            x = juce::jlimit (0, pixels.width - 1, x);
            y = juce::jlimit (0, pixels.height - 1, y);
            // Camera frames are opaque 24-bit RGB or 32-bit ARGB (byte order: see toTensor).
            const auto* p = pixels.getPixelPointer (x, y);
            const auto luma = [] (const auto* px) { return 0.299f * px->getRed() + 0.587f * px->getGreen() + 0.114f * px->getBlue(); };
            return argb ? luma (reinterpret_cast<const juce::PixelARGB*> (p)) : luma (reinterpret_cast<const juce::PixelRGB*> (p));
        };

        const auto x0 = area.getX() * (float) pixels.width, y0 = area.getY() * (float) pixels.height;
        const auto cw = area.getWidth() * (float) pixels.width / side, ch = area.getHeight() * (float) pixels.height / side;
        Thumbnail t {};
        for (int cy = 0; cy < side; ++cy)
            for (int cx = 0; cx < side; ++cx)
            {
                float sum = 0;
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx)
                        sum += grey ((int) (x0 + ((float) cx + ((float) sx + 0.5f) / 4) * cw),
                                     (int) (y0 + ((float) cy + ((float) sy + 0.5f) / 4) * ch));
                t[(size_t) (cy * side + cx)] = sum / 16;
            }
        return t;
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
} // namespace engines
