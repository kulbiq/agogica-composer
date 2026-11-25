#include "HeadPoseEstimator.h"
#include "ImageTensor.h"

namespace tracker
{
namespace
{
using Clock = std::chrono::steady_clock;
float msSince (Clock::time_point start) { return std::chrono::duration<float, std::milli> (Clock::now() - start).count(); }

constexpr size_t anglesOutput = 0, kappaOutput = 1; // the pose model's two outputs
constexpr size_t angleValues = 6;                   // cos, sin of yaw, pitch, roll
constexpr float minimumHeadSidePixels = 1.0f;

juce::Rectangle<float> normalised (const Box& box, const juce::Image& frame)
{
    const auto width = (float) frame.getWidth(), height = (float) frame.getHeight();
    return { box.x1 / width, box.y1 / height, (box.x2 - box.x1) / width, (box.y2 - box.y1) / height };
}

// The output's values, after checking it holds at least `count` of them.
const float* valuesOf (const std::vector<Ort::Value>& outputs, size_t index, size_t count)
{
    if (index >= outputs.size() || outputs[index].GetTensorTypeAndShapeInfo().GetElementCount() < count)
        throw std::runtime_error ("unexpected head pose output");
    return outputs[index].GetTensorData<float>();
}

bool allFinite (const float* values, size_t count)
{
    return std::all_of (values, values + count, [] (float v) { return std::isfinite (v); });
}
} // namespace

HeadPoseEstimator::HeadPoseEstimator (const juce::File& modelsFolder)
    : headDetector (modelsFolder), poseModel (modelsFolder.getChildFile ("hfhpe_vitt_ypr_kappa_1x3x128x128.onnx"))
{
}

std::optional<HeadObservation> HeadPoseEstimator::estimate (const juce::Image& frame)
{
    HeadObservation observation;

    const auto detectStart = Clock::now();
    const auto head = headDetector.find (frame);
    observation.detectMs = msSince (detectStart);

    // Written so that a box with NaN in it also counts as no head.
    const bool isHeadUsable = head && head->x2 - head->x1 >= minimumHeadSidePixels && head->y2 - head->y1 >= minimumHeadSidePixels;
    if (! isHeadUsable)
        return std::nullopt;

    const auto poseStart = Clock::now();
    input.resize ((size_t) 3 * inputSize * inputSize);
    headCropToTensor (frame, squareCropAroundHead (*head), inputSize, input.data());
    const auto outputs = poseModel.run (input, { 1, 3, inputSize, inputSize });
    observation.poseMs = msSince (poseStart);

    const auto* cosSin = valuesOf (outputs, anglesOutput, angleValues);
    const auto* kappa = valuesOf (outputs, kappaOutput, 1);
    if (! allFinite (cosSin, angleValues) || ! std::isfinite (kappa[0]))
        return std::nullopt;

    observation.orientation = { angleFromCosSinDegrees (cosSin[0], cosSin[1]),
                                angleFromCosSinDegrees (cosSin[2], cosSin[3]),
                                angleFromCosSinDegrees (cosSin[4], cosSin[5]) };
    observation.confidence = confidenceFromKappa (kappa[0]); // the yaw's
    observation.headBoxNormalised = normalised (*head, frame);
    return observation;
}
} // namespace tracker
