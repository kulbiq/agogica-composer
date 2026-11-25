#pragma once

#include <juce_graphics/juce_graphics.h>
#include "HeadGeometry.h"

// Camera frames as the models' inputs: channel-first float tensors [3, height, width].
namespace tracker
{
// A camera frame's pixels, read straight from memory (getPixelColour per sample is far too slow
// per frame). Camera frames are opaque 24-bit RGB or 32-bit ARGB; JUCE's byte order for both is
// platform-specific, so the pixel structs do the reading.
class FramePixels
{
public:
    explicit FramePixels (const juce::Image& frame); // throws on any other pixel format

    int width() const  { return pixels.width; }
    int height() const { return pixels.height; }

    // Pixel x, y (inside the frame) as red, green, blue levels 0-255.
    void read (int x, int y, float* rgb) const
    {
        const auto* pixel = pixels.getPixelPointer (x, y);
        if (isArgb)
            store (reinterpret_cast<const juce::PixelARGB*> (pixel), rgb);
        else
            store (reinterpret_cast<const juce::PixelRGB*> (pixel), rgb);
    }

private:
    template <typename Pixel>
    static void store (const Pixel* pixel, float* rgb)
    {
        rgb[0] = pixel->getRed(), rgb[1] = pixel->getGreen(), rgb[2] = pixel->getBlue();
    }

    const juce::Image::BitmapData pixels;
    const bool isArgb;
};

// The head detector's input: the whole frame stretched to width x height (bilinear), blue,
// green, red planes, levels 0-255.
void frameToBgrTensor (const juce::Image& frame, int width, int height, float* out);

// The pose model's input: `crop` of the frame resized to size x size (resizeSquareToTensor).
void headCropToTensor (const juce::Image& frame, SquareCrop crop, int size, float* out);

// Resizes `crop` of a width x height image to size x size: red, green, blue planes, each value
// rounded to a whole level first (the reference resizes 8-bit pictures), then level / 127.5 - 1
// (HFHPE's "center05"). `readRgb (x, y, rgb)` reads pixel x, y (inside the image).
// Shrinking averages the covered area (OpenCV INTER_AREA, as the reference does); enlarging - a
// head smaller than the input, rare at 640 x 480 - is bilinear where the reference uses Lanczos.
// A template so the tests can feed it a formula instead of an image.
template <typename ReadRgb>
void resizeSquareToTensor (ReadRgb&& readRgb, int width, int height, SquareCrop crop, int size, float* out)
{
    const auto planeSize = (size_t) size * (size_t) size;
    const double sourcePerOutputPixel = (double) crop.side / size;

    // In the crop's coordinates; both the crop's and the image's border repeat.
    auto readCropPixel = [&] (int cropX, int cropY, float* rgb)
    {
        cropX = std::clamp (cropX, 0, crop.side - 1), cropY = std::clamp (cropY, 0, crop.side - 1);
        readRgb (std::clamp (crop.x + cropX, 0, width - 1), std::clamp (crop.y + cropY, 0, height - 1), rgb);
    };
    auto storeOutputPixel = [&] (int outputX, int outputY, const double* levels)
    {
        for (size_t channel = 0; channel < 3; ++channel)
            out[channel * planeSize + (size_t) outputY * (size_t) size + (size_t) outputX]
                = (float) (std::nearbyint (levels[channel]) / 127.5 - 1.0);
    };

    if (sourcePerOutputPixel >= 1.0) // shrinking: the average of the area each output pixel covers
    {
        // Per output index (the same along x and y): the source pixels it covers and how much of each.
        struct Share { int sourceIndex; double weight; };
        std::vector<std::vector<Share>> covered ((size_t) size);
        for (int output = 0; output < size; ++output)
        {
            const double from = output * sourcePerOutputPixel, to = (output + 1) * sourcePerOutputPixel;
            for (int source = (int) std::floor (from); source < (int) std::ceil (to); ++source)
                if (const auto weight = std::min (to, source + 1.0) - std::max (from, (double) source); weight > 1e-9)
                    covered[(size_t) output].push_back ({ source, weight });
        }

        const double coveredArea = sourcePerOutputPixel * sourcePerOutputPixel;
        float rgb[3];
        for (int outputY = 0; outputY < size; ++outputY)
            for (int outputX = 0; outputX < size; ++outputX)
            {
                double levels[3] {};
                for (const auto& row : covered[(size_t) outputY])
                    for (const auto& column : covered[(size_t) outputX])
                    {
                        readCropPixel (column.sourceIndex, row.sourceIndex, rgb);
                        for (size_t channel = 0; channel < 3; ++channel)
                            levels[channel] += column.weight * row.weight * rgb[channel];
                    }
                for (auto& level : levels)
                    level /= coveredArea;
                storeOutputPixel (outputX, outputY, levels);
            }
        return;
    }

    // Enlarging: bilinear between the four nearest source pixels.
    float topLeft[3], topRight[3], bottomLeft[3], bottomRight[3];
    for (int outputY = 0; outputY < size; ++outputY)
        for (int outputX = 0; outputX < size; ++outputX)
        {
            const double sourceX = (outputX + 0.5) * sourcePerOutputPixel - 0.5, sourceY = (outputY + 0.5) * sourcePerOutputPixel - 0.5;
            const int left = (int) std::floor (sourceX), top = (int) std::floor (sourceY);
            const double towardsRight = sourceX - left, towardsBottom = sourceY - top;
            readCropPixel (left, top, topLeft), readCropPixel (left + 1, top, topRight);
            readCropPixel (left, top + 1, bottomLeft), readCropPixel (left + 1, top + 1, bottomRight);

            double levels[3];
            for (size_t channel = 0; channel < 3; ++channel)
                levels[channel] = (topLeft[channel] * (1 - towardsRight) + topRight[channel] * towardsRight) * (1 - towardsBottom)
                                + (bottomLeft[channel] * (1 - towardsRight) + bottomRight[channel] * towardsRight) * towardsBottom;
            storeOutputPixel (outputX, outputY, levels);
        }
}
} // namespace tracker
