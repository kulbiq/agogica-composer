#pragma once

#include <juce_graphics/juce_graphics.h>
#include <onnxruntime_cxx_api.h>
#include "Decode.h"

namespace engines
{
enum class ColourOrder { rgb, bgr };

// Channel-first float tensor [3, height, width] from `source` (a region of `image`, in its
// pixels), resized bilinearly. value = (pixel * scale - mean[c]) / std[c], channels in
// `order`. Regions outside the image read as `padding` (0-255).
void toTensor (const juce::Image& image, juce::Rectangle<float> source, int width, int height, ColourOrder order,
               float scale, const std::array<float, 3>& mean, const std::array<float, 3>& std, float* out,
               float padding = 114.0f);

// HFHPE's input from `crop` of a camera frame (squareToTensor in Decode.h): [3, size, size].
void squareTensor (const juce::Image& image, SquareCrop crop, int size, float* out);

// One ONNX model: CoreML (GPU / Neural Engine) when it takes the model, CPU otherwise.
class Model
{
public:
    explicit Model (const juce::File&);

    // Runs with one input of `shape`; returns every output.
    std::vector<Ort::Value> run (std::vector<float>& input, const std::vector<int64_t>& shape);

    juce::String provider() const { return usedProvider; }

private:
    juce::String usedProvider; // before `session`: openSession() writes it while initialising
    Ort::Session session { nullptr };
    std::string inputName;
    std::vector<std::string> outputNames;
};
} // namespace engines
