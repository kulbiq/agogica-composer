#include "Engines.h"
#include "Onnx.h"

namespace engines
{
namespace
{
using Clock = std::chrono::steady_clock;
float msSince (Clock::time_point t) { return std::chrono::duration<float, std::milli> (Clock::now() - t).count(); }

constexpr std::array<float, 3> zero { 0, 0, 0 }, one { 1, 1, 1 };
constexpr std::array<float, 3> imagenetMean { 0.485f, 0.456f, 0.406f }, imagenetStd { 0.229f, 0.224f, 0.225f };

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
        const auto count = outputs[0].GetTensorTypeAndShapeInfo().GetShape()[0];
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

// 6DRepNet360 (MIT; trained on non-commercial data): head box x1.2, resized to 256 and
// centre-cropped to 224 (= the central 7/8 of the expanded box), RGB, ImageNet mean/std.
// Output "yaw_pitch_roll"; the full heading is rebuilt by sixDRepNetHeading.
class SixDRepNet final : public Engine
{
public:
    explicit SixDRepNet (const juce::File& dir) : heads (dir), model (dir.getChildFile ("sixdrepnet360_1x3x224x224_full.onnx")) {}

    std::optional<Estimate> estimate (const juce::Image& frame) override
    {
        Estimate e;
        const auto head = heads.find (frame, e.detectMs);
        if (! head)
            return std::nullopt;

        const auto start = Clock::now();
        const auto cx = centreX (*head), cy = centreY (*head);
        const auto side = std::max (head->x2 - head->x1, head->y2 - head->y1) * 1.2f * 224.0f / 256.0f;
        input.resize (3 * 224 * 224);
        toTensor (frame, { cx - side / 2, cy - side / 2, side, side }, 224, 224, ColourOrder::rgb, 1.0f / 255.0f,
                  imagenetMean, imagenetStd, input.data());

        const auto* out = model.run (input, { 1, 3, 224, 224 })[0].GetTensorData<float>();
        e.yaw = sixDRepNetHeading (out[0], out[1], out[2]);
        e.rawYaw = out[0]; // the model's own Euler yaw (±90°), before the hemisphere reconstruction
        e.pitch = out[1];
        e.roll = out[2];
        e.confidence = 1.0f;
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
    if (name == "6drepnet360") return std::make_unique<SixDRepNet> (dir);
    throw std::runtime_error ("unknown engine " + name.toStdString());
}
} // namespace engines
