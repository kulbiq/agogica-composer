#include "Onnx.h"
#if COMPOSER_TRACKER_DIRECTML
 #include <dml_provider_factory.h>
#endif

namespace engines
{
namespace
{
Ort::Env& environment()
{
    static Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "AgogicaTracker" };
    return env;
}

// ONNX Runtime takes wide paths on Windows (ORTCHAR_T), UTF-8 elsewhere.
std::basic_string<ORTCHAR_T> modelPath (const juce::File& file)
{
   #if JUCE_WINDOWS
    return file.getFullPathName().toWideCharPointer();
   #else
    return file.getFullPathName().toStdString();
   #endif
}

Ort::Session openSession (const juce::File& file, juce::String& provider)
{
    // Development: AGOGICA_TRACKER_EP=cpu compares against ONNX Runtime's own CPU kernels.
   #if JUCE_MAC
    const auto wanted = juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_EP", "coreml");
   #elif COMPOSER_TRACKER_DIRECTML
    const auto wanted = juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_EP", "dml");
   #else
    const auto wanted = juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_EP", "cpu");
   #endif

    if (wanted == "coreml")
    {
        try
        {
            Ort::SessionOptions options;
            // Static shapes only: the head detector's post-processing returns N detections, and
            // CoreML fails on N = 0 (no head in view) - those nodes run on the CPU instead.
            // Compiled models are cached, so only the first launch compiles.
            const auto cache = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                   .getChildFile ("Library/Caches/Agogica/coreml");
            cache.createDirectory();
            options.AppendExecutionProvider ("CoreML", { { "ModelFormat", "MLProgram" },
                                                         { "MLComputeUnits", "ALL" },
                                                         { "RequireStaticInputShapes", "1" },
                                                         { "ModelCacheDirectory", cache.getFullPathName().toStdString() } });
            provider = "CoreML";
            return Ort::Session (environment(), modelPath (file).c_str(), options);
        }
        catch (const Ort::Exception&) {}
    }

   #if COMPOSER_TRACKER_DIRECTML
    if (wanted == "dml")
    {
        try
        {
            // DirectML needs these two (ONNX Runtime's DirectML notes).
            Ort::SessionOptions options;
            options.DisableMemPattern();
            options.SetExecutionMode (ExecutionMode::ORT_SEQUENTIAL);
            options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
            Ort::ThrowOnError (OrtSessionOptionsAppendExecutionProvider_DML (options, 0));
            provider = "DirectML";
            return Ort::Session (environment(), modelPath (file).c_str(), options);
        }
        catch (const Ort::Exception&) {}
    }
   #endif

    Ort::SessionOptions options;
    // Measured on an i5-10210U with the former 6DRepNet360 (a ResNet-50): denormals in the activations made
    // pose 4x slower (404 -> 97 ms), and the detector's and the pose model's thread pools
    // spinning against each other cost another 40 % (-> 57 ms). Two threads (79 ms) leave
    // cores to the DAW's audio and spend less CPU time per frame than four.
    options.SetIntraOpNumThreads (2);
    options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.AddConfigEntry ("session.set_denormal_as_zero", "1");
    options.AddConfigEntry ("session.intra_op.allow_spinning", "0");
    provider = "CPU";
    return Ort::Session (environment(), modelPath (file).c_str(), options);
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

void squareTensor (const juce::Image& image, SquareCrop crop, int size, float* out)
{
    const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
    const bool argb = image.getFormat() == juce::Image::ARGB;
    squareToTensor ([&] (int x, int y, float* c)
    {
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
    }, pixels.width, pixels.height, crop, size, out);
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
