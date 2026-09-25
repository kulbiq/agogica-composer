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

// WHENet (BSD-3; trained on non-commercial data): full-range yaw from a head crop expanded
// x ±w/5, y ±h/10. PINTO's prepost export: 1x3x224x224 RGB 0-255 in (normalises inside),
// output "yaw_roll_pitch" in degrees.
class Whenet final : public Engine
{
public:
    explicit Whenet (const juce::File& dir) : heads (dir), model (dir.getChildFile ("whenet_1x3x224x224_prepost.onnx")) {}

    std::optional<Estimate> estimate (const juce::Image& frame) override
    {
        Estimate e;
        const auto head = heads.find (frame, e.detectMs);
        if (! head)
            return std::nullopt;

        const auto start = Clock::now();
        const auto w = head->x2 - head->x1, h = head->y2 - head->y1;
        const juce::Rectangle<float> crop { head->x1 - w / 5, head->y1 - h / 10, w * 1.4f, h * 1.2f };
        input.resize (3 * 224 * 224);
        toTensor (frame, crop, 224, 224, ColourOrder::rgb, 1.0f, zero, one, input.data());

        const auto* out = model.run (input, { 1, 3, 224, 224 })[0].GetTensorData<float>();
        e.yaw = out[0];
        e.roll = out[1];
        e.pitch = out[2];
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

// DEIMv2-Wholebody34 atto (Apache-2.0): 8 head-direction classes; the whole frame stretched
// to 320x320 BGR 0-255 ("input_bgr", normalises inside); output [1,340,6] =
// label, x1, y1, x2, y2 (0..1), score.
class Deim final : public Engine
{
public:
    explicit Deim (const juce::File& dir) : model (dir.getChildFile ("deimv2_atto_wholebody34_320x320.onnx")) {}

    std::optional<Estimate> estimate (const juce::Image& frame) override
    {
        const auto start = Clock::now();
        input.resize (3 * 320 * 320);
        toTensor (frame, frame.getBounds().toFloat(), 320, 320, ColourOrder::bgr, 1.0f, zero, one, input.data());

        const auto outputs = model.run (input, { 1, 3, 320, 320 });
        const auto count = outputs[0].GetTensorTypeAndShapeInfo().GetShape()[1];
        const auto* rows = outputs[0].GetTensorData<float>();

        std::vector<Detection> detections;
        for (int64_t i = 0; i < count; ++i)
        {
            const auto* r = rows + i * 6;
            detections.push_back ({ (int) r[0], { r[1], r[2], r[3], r[4] }, r[5] });
        }

        const auto direction = deimDirection (detections);
        if (! direction)
            return std::nullopt;

        Estimate e;
        e.yaw = direction->yaw;
        e.confidence = direction->confidence;
        e.box = { direction->box.x1, direction->box.y1, direction->box.x2 - direction->box.x1, direction->box.y2 - direction->box.y1 };
        e.poseMs = msSince (start);
        return e;
    }

    juce::String provider() const override { return model.provider(); }

private:
    Model model;
    std::vector<float> input;
};

// YOLOv8n-Pose (AGPL-3.0): 640x640 letterbox, RGB /255; output [1,56,8400]; coarse yaw from
// the nose / eyes / ears / shoulders (keypointYaw).
class YoloPose final : public Engine
{
public:
    explicit YoloPose (const juce::File& dir) : model (dir.getChildFile ("yolov8n-pose.onnx")) {}

    std::optional<Estimate> estimate (const juce::Image& frame) override
    {
        const auto start = Clock::now();
        constexpr int size = 640;
        const auto scale = (float) size / (float) std::max (frame.getWidth(), frame.getHeight());
        const auto side = (float) size / scale; // letterbox: a square around the frame, padded grey
        const juce::Rectangle<float> source { ((float) frame.getWidth() - side) / 2, ((float) frame.getHeight() - side) / 2, side, side };
        input.resize (3 * size * size);
        toTensor (frame, source, size, size, ColourOrder::rgb, 1.0f / 255.0f, zero, one, input.data());

        const auto outputs = model.run (input, { 1, 3, size, size });
        const auto anchors = (int) outputs[0].GetTensorTypeAndShapeInfo().GetShape()[2];
        auto person = yoloBestPerson (outputs[0].GetTensorData<float>(), anchors);
        if (! person)
            return std::nullopt;

        const auto yaw = keypointYaw (*person);
        if (! yaw)
            return std::nullopt;

        const auto toFrame = [&] (float v, float offset) { return v / scale + offset; };
        Estimate e;
        e.yaw = *yaw;
        e.confidence = person->score;
        const Box b { toFrame (person->box.x1, source.getX()), toFrame (person->box.y1, source.getY()),
                      toFrame (person->box.x2, source.getX()), toFrame (person->box.y2, source.getY()) };
        e.box = normalised (b, frame);
        e.poseMs = msSince (start);
        return e;
    }

    juce::String provider() const override { return model.provider(); }

private:
    Model model;
    std::vector<float> input;
};
} // namespace

std::unique_ptr<Engine> makeEngine (const juce::String& name, const juce::File& dir)
{
    if (name == "whenet")      return std::make_unique<Whenet> (dir);
    if (name == "6drepnet360") return std::make_unique<SixDRepNet> (dir);
    if (name == "deimv2")      return std::make_unique<Deim> (dir);
    if (name == "yolov8pose")  return std::make_unique<YoloPose> (dir);
    throw std::runtime_error ("unknown engine " + name.toStdString());
}
} // namespace engines
