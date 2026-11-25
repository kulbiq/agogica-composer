#include "OnnxModel.h"

namespace tracker
{
namespace
{
constexpr int cpuThreads = 2;

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

// Development: AGOGICA_TRACKER_PROFILE=<folder> makes ONNX Runtime time every node of every run;
// each model writes <folder>/<model>_<date>.json (Chrome trace format) when its session closes,
// i.e. on quitting the app.
Ort::SessionOptions sessionOptions (const juce::File& file)
{
    Ort::SessionOptions options;
    const auto folder = juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_PROFILE", {});
    if (folder.isNotEmpty())
    {
        const juce::File dir (folder);
        dir.createDirectory();
        options.EnableProfiling (modelPath (dir.getChildFile (file.getFileNameWithoutExtension())).c_str());
    }
    return options;
}

// Development: AGOGICA_TRACKER_EP=cpu compares against ONNX Runtime's own CPU kernels.
bool coreMlWanted()
{
   #if JUCE_MAC
    return juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_EP", "coreml") == "coreml";
   #else
    return false;
   #endif
}

Ort::Session openCoreMlSession (const juce::File& file)
{
    auto options = sessionOptions (file);
    // Static shapes only: the head detector's post-processing returns N detections, and CoreML
    // fails on N = 0 (no head in view) - those nodes run on the CPU instead.
    // Compiled models are cached, so only the first launch compiles.
    const auto cache = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                           .getChildFile ("Library/Caches/Agogica/coreml");
    cache.createDirectory();
    options.AppendExecutionProvider ("CoreML", { { "ModelFormat", "MLProgram" },
                                                 { "MLComputeUnits", "ALL" },
                                                 { "RequireStaticInputShapes", "1" },
                                                 { "ModelCacheDirectory", cache.getFullPathName().toStdString() } });
    return Ort::Session (environment(), modelPath (file).c_str(), options);
}

Ort::Session openCpuSession (const juce::File& file)
{
    auto options = sessionOptions (file);
    // Measured on an i5-10210U with the former 6DRepNet360 (a ResNet-50): denormals in the
    // activations made pose 4x slower (404 -> 97 ms), and the detector's and the pose model's
    // thread pools spinning against each other cost another 40 % (-> 57 ms). Two threads (79 ms)
    // leave cores to the DAW's audio and spend less CPU time per frame than four.
    options.SetIntraOpNumThreads (cpuThreads);
    options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.AddConfigEntry ("session.set_denormal_as_zero", "1");
    options.AddConfigEntry ("session.intra_op.allow_spinning", "0");
    return Ort::Session (environment(), modelPath (file).c_str(), options);
}

Ort::Session openSession (const juce::File& file, juce::String& provider)
{
    if (coreMlWanted())
    {
        try
        {
            provider = "CoreML";
            return openCoreMlSession (file);
        }
        catch (const Ort::Exception& e)
        {
            juce::Logger::writeToLog ("Agogica Tracker: CoreML didn't take " + file.getFileName() + " (" + e.what() + "); using the CPU");
        }
    }

    provider = "CPU";
    return openCpuSession (file);
}
} // namespace

OnnxModel::OnnxModel (const juce::File& file) : session (openSession (file, usedProvider))
{
    Ort::AllocatorWithDefaultOptions allocator;
    inputName = session.GetInputNameAllocated (0, allocator).get();

    for (size_t i = 0; i < session.GetOutputCount(); ++i)
        outputNames.push_back (session.GetOutputNameAllocated (i, allocator).get());
}

std::vector<Ort::Value> OnnxModel::run (std::vector<float>& input, const std::vector<int64_t>& shape)
{
    const auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    auto tensor = Ort::Value::CreateTensor<float> (memory, input.data(), input.size(), shape.data(), shape.size());

    const char* inputNames[] { inputName.c_str() };
    std::vector<const char*> names;
    for (const auto& name : outputNames)
        names.push_back (name.c_str());

    return session.Run (Ort::RunOptions { nullptr }, inputNames, &tensor, 1, names.data(), names.size());
}
} // namespace tracker
