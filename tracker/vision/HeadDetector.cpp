#include "HeadDetector.h"
#include "ImageTensor.h"

namespace tracker
{
HeadDetector::HeadDetector (const juce::File& modelsFolder)
    : model (modelsFolder.getChildFile ("yolox_n_body_head_hand_192x320.onnx"))
{
}

std::optional<Box> HeadDetector::find (const juce::Image& frame)
{
    // A camera that changed its picture size: the last head's pixels mean nothing any more.
    if (std::exchange (previousFrameSize, frame.getBounds()) != frame.getBounds())
        previousHead.reset();

    input.resize ((size_t) 3 * inputWidth * inputHeight);
    frameToBgrTensor (frame, inputWidth, inputHeight, input.data());
    const auto outputs = model.run (input, { 1, 3, inputHeight, inputWidth });

    const auto shape = outputs.empty() ? std::vector<int64_t>() : outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 2 || shape[1] != detectorRowLength)
        throw std::runtime_error ("unexpected head detector output");

    const auto heads = headsFromDetectorRows (outputs[0].GetTensorData<float>(), shape[0],
                                              (float) frame.getWidth() / inputWidth, (float) frame.getHeight() / inputHeight);
    const auto head = pickHead (heads, previousHead);
    previousHead = head ? std::optional (head->box) : std::nullopt;
    return previousHead;
}
} // namespace tracker
