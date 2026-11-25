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

// PINTO's YOLOX Body-Head-Hand (Apache-2.0): heads from any direction, incl. from behind.
// Input 1x3x192x320 BGR 0-255 (the whole frame, stretched); output [N,7] =
// batch, class (0 body, 1 head, 2 hand), score, x1, y1, x2, y2 in input pixels.
class HeadDetector
{
public:
    explicit HeadDetector (const juce::File& dir) : model (dir.getChildFile ("yolox_n_body_head_hand_192x320.onnx")) {}

    std::optional<Box> find (const juce::Image& frame, float& ms)
    {
        const auto start = Clock::now();
        constexpr int w = 320, h = 192;
        input.resize (3 * w * h);
        toTensor (frame, frame.getBounds().toFloat(), w, h, ColourOrder::bgr, 1.0f, zero, one, input.data());

        const auto outputs = model.run (input, { 1, 3, h, w });
        const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 2 || shape[1] != 7)
            throw std::runtime_error ("unexpected head detector output"); // a frame without a head
        const auto count = shape[0];
        const auto* rows = outputs[0].GetTensorData<float>();

        std::vector<Detection> heads;
        const auto kx = (float) frame.getWidth() / w, ky = (float) frame.getHeight() / h;
        for (int64_t i = 0; i < count; ++i)
        {
            const auto* r = rows + i * 7;
            if ((int) r[1] == 1 && r[2] >= 0.35f)
                heads.push_back ({ 1, { r[3] * kx, r[4] * ky, r[5] * kx, r[6] * ky }, r[2] });
        }

        ms = msSince (start);
        const auto head = pickHead (heads, previous);
        previous = head ? std::optional<Box> (head->box) : std::nullopt;
        return previous;
    }

    juce::String provider() const { return model.provider(); }

private:
    Model model;
    std::vector<float> input;
    std::optional<Box> previous;
};

// HFHPE (PINTO0309, 2026; code MIT, weights' licence to be confirmed before shipping; trained
// without non-commercial data): the joint ViT-T 128 graph - yaw, pitch and roll in one pass, each
// as (cos, sin), plus a confidence per angle (kappa). Input: the crop contract (hfhpeCrop) of the
// YOLOX head box, RGB, x / 127.5 - 1 (HFHPE's own DEIMv2 detector gives the same readings to
// within ~0.6 deg but costs ~250 ms per frame on an i5-10210U; research/head-tracking).
// Yaw: 0 = facing the camera, + = the head turned to the person's right, the full circle.
class Hfhpe final : public Engine
{
public:
    static constexpr int size = 128;

    explicit Hfhpe (const juce::File& dir) : heads (dir), model (dir.getChildFile ("hfhpe_vitt_ypr_kappa_1x3x128x128.onnx")) {}

    std::optional<Estimate> estimate (const juce::Image& frame) override
    {
        Estimate e;
        const auto head = heads.find (frame, e.detectMs);
        if (! head || ! (head->x2 - head->x1 >= 1.0f && head->y2 - head->y1 >= 1.0f)) // NaN included
            return std::nullopt;

        const auto start = Clock::now();
        input.resize (3 * size * size);
        squareTensor (frame, hfhpeCrop (*head), size, input.data());
        const auto outputs = model.run (input, { 1, 3, size, size });
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

    juce::String provider() const override { return model.provider(); }

private:
    HeadDetector heads;
    Model model;
    std::vector<float> input;
};

} // namespace

std::unique_ptr<Engine> makeEngine (const juce::String& name, const juce::File& dir)
{
    if (name == "hfhpe") return std::make_unique<Hfhpe> (dir);
    throw std::runtime_error ("unknown engine " + name.toStdString());
}
} // namespace engines
