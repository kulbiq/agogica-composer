#include "ImageTensor.h"

namespace tracker
{
FramePixels::FramePixels (const juce::Image& frame)
    : pixels (frame, juce::Image::BitmapData::readOnly), isArgb (frame.getFormat() == juce::Image::ARGB)
{
    if (! isArgb && frame.getFormat() != juce::Image::RGB)
        throw std::runtime_error ("camera frame is neither RGB nor ARGB");
}

void frameToBgrTensor (const juce::Image& frame, int width, int height, float* out)
{
    const FramePixels pixels (frame);
    const auto planeSize = (size_t) width * (size_t) height;
    const auto sourcePerOutputX = (float) pixels.width() / (float) width;
    const auto sourcePerOutputY = (float) pixels.height() / (float) height;

    auto readClamped = [&] (int x, int y, float* rgb) // the frame's border repeats
    {
        pixels.read (juce::jlimit (0, pixels.width() - 1, x), juce::jlimit (0, pixels.height() - 1, y), rgb);
    };

    float topLeft[3], topRight[3], bottomLeft[3], bottomRight[3];
    for (int outputY = 0; outputY < height; ++outputY)
    {
        const auto sourceY = ((float) outputY + 0.5f) * sourcePerOutputY - 0.5f;
        const auto top = (int) std::floor (sourceY);
        const auto towardsBottom = sourceY - (float) top;

        for (int outputX = 0; outputX < width; ++outputX)
        {
            const auto sourceX = ((float) outputX + 0.5f) * sourcePerOutputX - 0.5f;
            const auto left = (int) std::floor (sourceX);
            const auto towardsRight = sourceX - (float) left;

            readClamped (left, top, topLeft), readClamped (left + 1, top, topRight);
            readClamped (left, top + 1, bottomLeft), readClamped (left + 1, top + 1, bottomRight);

            const auto outputIndex = (size_t) outputY * (size_t) width + (size_t) outputX;
            for (size_t channel = 0; channel < 3; ++channel) // 0 = red, 1 = green, 2 = blue
            {
                const auto level = (topLeft[channel] * (1 - towardsRight) + topRight[channel] * towardsRight) * (1 - towardsBottom)
                                 + (bottomLeft[channel] * (1 - towardsRight) + bottomRight[channel] * towardsRight) * towardsBottom;
                const auto bgrPlane = 2 - channel;
                out[bgrPlane * planeSize + outputIndex] = level;
            }
        }
    }
}

void headCropToTensor (const juce::Image& frame, SquareCrop crop, int size, float* out)
{
    const FramePixels pixels (frame);
    resizeSquareToTensor ([&] (int x, int y, float* rgb) { pixels.read (x, y, rgb); }, pixels.width(), pixels.height(), crop, size, out);
}
} // namespace tracker
