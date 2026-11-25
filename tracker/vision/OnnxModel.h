#pragma once

#include <juce_core/juce_core.h>
#include <onnxruntime_cxx_api.h>

namespace tracker
{
// One ONNX model, run by ONNX Runtime: on macOS through CoreML (GPU / Neural Engine) when it
// takes the model, otherwise on the CPU.
class OnnxModel
{
public:
    explicit OnnxModel (const juce::File&); // throws when the model can't be loaded

    // Runs with one input of `shape`; returns every output.
    std::vector<Ort::Value> run (std::vector<float>& input, const std::vector<int64_t>& shape);

    juce::String provider() const { return usedProvider; } // "CoreML" / "CPU"

private:
    juce::String usedProvider; // before `session`: opening the session writes it
    Ort::Session session { nullptr };
    std::string inputName;
    std::vector<std::string> outputNames;
};
} // namespace tracker
