#include "Engines.h"
#include "Onnx.h"

namespace engines
{
namespace
{
using Clock = std::chrono::steady_clock;
float msSince (Clock::time_point t) { return std::chrono::duration<float, std::milli> (Clock::now() - t).count(); }

constexpr std::array<float, 3> zero { 0, 0, 0 }, one { 1, 1, 1 };

juce::Rectangle<float> normalised (const Box& b, const juce::Image& frame)
{
    return { b.x1 / (float) frame.getWidth(), b.y1 / (float) frame.getHeight(),
             (b.x2 - b.x1) / (float) frame.getWidth(), (b.y2 - b.y1) / (float) frame.getHeight() };
}

} // namespace

HeadDetector::HeadDetector (const juce::File& dir) : model (dir.getChildFile ("yolox_n_body_head_hand_192x320.onnx")) {}

std::optional<Box> HeadDetector::find (const juce::Image& frame, float& ms)
{
    const auto start = Clock::now();
    constexpr int w = 320, h = 192;
    input.resize (3 * w * h);
    toTensor (frame, frame.getBounds().toFloat(), w, h, ColourOrder::bgr, 1.0f, zero, one, input.data());

    const auto outputs = model.run (input, { 1, 3, h, w });
    const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 2 || shape[1] != 7)
        throw std::runtime_error ("unexpected head detector output");
    const auto count = shape[0];
    const auto* rows = outputs[0].GetTensorData<float>();

    std::vector<Detection> heads;
    const auto kx = (float) frame.getWidth() / w, ky = (float) frame.getHeight() / h;
    for (int64_t i = 0; i < count; ++i)
    {
        const auto* r = rows + i * 7;
        if ((int) r[1] == 1 && r[2] >= 0.35f)
            heads.push_back ({ { r[3] * kx, r[4] * ky, r[5] * kx, r[6] * ky }, r[2] });
    }

    ms = msSince (start);
    const auto head = pickHead (heads, previous);
    previous = head ? std::optional<Box> (head->box) : std::nullopt;
    return previous;
}

HeadPoseEstimator::HeadPoseEstimator (const juce::File& dir)
    : heads (dir), model (dir.getChildFile ("hfhpe_vitt_ypr_kappa_1x3x128x128.onnx"))
{
}

std::optional<Estimate> HeadPoseEstimator::estimate (const juce::Image& frame)
{
    Estimate e;
    const auto head = heads.find (frame, e.detectMs);
    if (! head || ! (head->x2 - head->x1 >= 1.0f && head->y2 - head->y1 >= 1.0f)) // NaN included
        return std::nullopt;

    const auto start = Clock::now();
    input.resize (3 * inputSize * inputSize);
    squareTensor (frame, hfhpeCrop (*head), inputSize, input.data());
    const auto outputs = model.run (input, { 1, 3, inputSize, inputSize });
    const auto* cosSin = outputs[0].GetTensorData<float>(); // cos/sin of yaw, pitch, roll
    const auto* kappa = outputs[1].GetTensorData<float>();
    for (int i = 0; i < 6; ++i)
        if (! std::isfinite (cosSin[i]))
            return std::nullopt;

    e.yaw = angleOf (cosSin[0], cosSin[1]);
    e.pitch = angleOf (cosSin[2], cosSin[3]);
    e.roll = angleOf (cosSin[4], cosSin[5]);
    e.confidence = kappaConfidence (kappa[0]);
    e.box = normalised (*head, frame);
    e.poseMs = msSince (start);
    return e;
}
} // namespace engines
