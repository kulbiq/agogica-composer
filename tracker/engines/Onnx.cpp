#include "Onnx.h"

namespace engines
{
namespace
{
Ort::Env& environment()
{
    static Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "AgogicaTracker" };
    return env;
}

Ort::Session openSession (const juce::File& file, juce::String& provider)
{
    // Development: AGOGICA_TRACKER_EP=cpu compares against ONNX Runtime's own CPU kernels.
    const auto wanted = juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_EP", "coreml");

    if (wanted == "coreml")
    {
        try
        {
            Ort::SessionOptions options;
            options.AppendExecutionProvider ("CoreML", { { "ModelFormat", "MLProgram" }, { "MLComputeUnits", "ALL" } });
            provider = "CoreML";
            return Ort::Session (environment(), file.getFullPathName().toRawUTF8(), options);
        }
        catch (const Ort::Exception&) {}
    }

    Ort::SessionOptions options;
    options.SetIntraOpNumThreads (4);
    options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
    provider = "CPU";
    return Ort::Session (environment(), file.getFullPathName().toRawUTF8(), options);
}
} // namespace

void toTensor (const juce::Image& image, juce::Rectangle<float> source, int width, int height, ColourOrder order,
               float scale, const std::array<float, 3>& mean, const std::array<float, 3>& std, float* out, float padding)
{
    const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
    const auto plane = (size_t) width * (size_t) height;
    const auto sx = source.getWidth() / (float) width;
    const auto sy = source.getHeight() / (float) height;

    // Straight from the pixel memory (getPixelColour per sample is far too slow per frame).
    // Camera frames are 24-bit RGB or 32-bit ARGB; JUCE's PixelRGB / PixelARGB byte order
    // is platform-specific, so read through the pixel structs.
    const bool argb = image.getFormat() == juce::Image::ARGB;
    auto rgb = [&] (int x, int y, float* c)
    {
        if (x < 0 || y < 0 || x >= pixels.width || y >= pixels.height)
        {
            c[0] = c[1] = c[2] = padding;
            return;
        }
        const auto* p = pixels.getPixelPointer (x, y);
        if (argb)
        {
            const auto* px = reinterpret_cast<const juce::PixelARGB*> (p);
            c[0] = px->getRed(), c[1] = px->getGreen(), c[2] = px->getBlue();
        }
        else
        {
            const auto* px = reinterpret_cast<const juce::PixelRGB*> (p);
            c[0] = px->getRed(), c[1] = px->getGreen(), c[2] = px->getBlue();
        }
    };

    std::array<float, 3> k, offset; // value = v * k - offset, per output slot
    for (size_t slot = 0; slot < 3; ++slot)
    {
        k[slot] = scale / std[slot];
        offset[slot] = mean[slot] / std[slot];
    }

    for (int y = 0; y < height; ++y)
    {
        const auto fy = source.getY() + ((float) y + 0.5f) * sy - 0.5f;
        const auto y0 = (int) std::floor (fy);
        const auto ty = fy - (float) y0;

        for (int x = 0; x < width; ++x)
        {
            const auto fx = source.getX() + ((float) x + 0.5f) * sx - 0.5f;
            const auto x0 = (int) std::floor (fx);
            const auto tx = fx - (float) x0;

            float a[3], b[3], c[3], d[3];
            rgb (x0, y0, a), rgb (x0 + 1, y0, b), rgb (x0, y0 + 1, c), rgb (x0 + 1, y0 + 1, d);
            const auto index = (size_t) y * (size_t) width + (size_t) x;

            for (size_t ch = 0; ch < 3; ++ch) // ch 0 = r, 1 = g, 2 = b
            {
                const auto v = (a[ch] * (1 - tx) + b[ch] * tx) * (1 - ty) + (c[ch] * (1 - tx) + d[ch] * tx) * ty;
                const auto slot = order == ColourOrder::rgb ? ch : 2 - ch;
                out[slot * plane + index] = v * k[slot] - offset[slot];
            }
        }
    }
}

Model::Model (const juce::File& file) : session (openSession (file, usedProvider))
{
    Ort::AllocatorWithDefaultOptions allocator;
    inputName = session.GetInputNameAllocated (0, allocator).get();

    for (size_t i = 0; i < session.GetOutputCount(); ++i)
        outputNames.push_back (session.GetOutputNameAllocated (i, allocator).get());
}

std::vector<Ort::Value> Model::run (std::vector<float>& input, const std::vector<int64_t>& shape)
{
    const auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    auto tensor = Ort::Value::CreateTensor<float> (memory, input.data(), input.size(), shape.data(), shape.size());

    const char* in[] { inputName.c_str() };
    std::vector<const char*> out;
    for (auto& n : outputNames)
        out.push_back (n.c_str());

    return session.Run (Ort::RunOptions { nullptr }, in, &tensor, 1, out.data(), out.size());
}
} // namespace engines
