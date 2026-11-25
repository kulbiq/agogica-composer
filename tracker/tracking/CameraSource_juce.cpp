#include <juce_video/juce_video.h>
#include "CameraSource.h"

#include <dshow.h>
#include <wrl/client.h>

namespace tracker
{
namespace
{
using Microsoft::WRL::ComPtr;

struct DirectShowCamera
{
    CameraDeviceInfo device;
    int juceIndex;
};

std::vector<DirectShowCamera> enumerateCameras()
{
    std::vector<DirectShowCamera> cameras;
    ComPtr<ICreateDevEnum> deviceEnum;
    ComPtr<IEnumMoniker> monikers;
    if (FAILED (CoCreateInstance (CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (deviceEnum.GetAddressOf())))
        || deviceEnum->CreateClassEnumerator (CLSID_VideoInputDeviceCategory, monikers.GetAddressOf(), 0) != S_OK)
        return cameras;

    int index = 0;
    ComPtr<IMoniker> moniker;
    while (monikers->Next (1, moniker.ReleaseAndGetAddressOf(), nullptr) == S_OK)
    {
        ComPtr<IBindCtx> context;
        CreateBindCtx (0, context.GetAddressOf());

        // Match the pinned JUCE DirectShow backend: only entries with a working
        // capture filter and a readable FriendlyName count towards its index.
        ComPtr<IBaseFilter> filter;
        ComPtr<IPropertyBag> properties;
        if (FAILED (moniker->BindToObject (context.Get(), nullptr, IID_PPV_ARGS (filter.GetAddressOf())))
            || FAILED (moniker->BindToStorage (context.Get(), nullptr, IID_PPV_ARGS (properties.GetAddressOf()))))
            continue;

        struct ScopedVariant
        {
            VARIANT value {};
            ~ScopedVariant() { VariantClear (&value); }
        } name;
        name.value.vt = VT_BSTR;
        if (FAILED (properties->Read (L"FriendlyName", &name.value, nullptr)))
            continue;

        const auto juceIndex = index++;
        LPOLESTR displayName = nullptr;
        if (SUCCEEDED (moniker->GetDisplayName (context.Get(), nullptr, &displayName)))
        {
            const std::unique_ptr<wchar_t, decltype (&CoTaskMemFree)> id (displayName, CoTaskMemFree);
            if (id != nullptr && *id.get() != L'\0' && name.value.vt == VT_BSTR)
                cameras.push_back ({ { juce::String (id.get()), juce::String (name.value.bstrVal) }, juceIndex });
        }
    }
    return cameras;
}

struct SizeRange { int minWidth, minHeight, maxWidth, maxHeight; };

// VGA first: the models need no more (the detector sees 320x192, the pose model a 128 px head),
// and webcams send uncompressed frames, so larger sizes are what USB 2 caps (the integrated
// camera of an i5-10210U laptop: 1280x720 at 10 fps, 640x480 at 30 in good light, 10 in a dim
// room) and cost Windows more to convert (0.2 cores at 1280x720).
// On Windows JUCE takes the largest size in the range.
constexpr SizeRange preferredSizes[] { { 640, 480, 640, 480 }, { 640, 360, 640, 360 }, { 320, 240, 1280, 720 } };

class JuceCameraSource final : public CameraSource,
                               private juce::CameraDevice::Listener
{
public:
    JuceCameraSource (std::unique_ptr<juce::CameraDevice> device, FrameCallback callback)
        : camera (std::move (device)), onFrame (std::move (callback))
    {
        camera->addListener (this);
    }

    ~JuceCameraSource() override { camera->removeListener (this); }

private:
    void imageReceived (const juce::Image& frame) override { onFrame (frame); }

    const std::unique_ptr<juce::CameraDevice> camera;
    const FrameCallback onFrame;
};
} // namespace

std::vector<CameraDeviceInfo> CameraSource::availableCameras()
{
    std::vector<CameraDeviceInfo> cameras;
    for (auto& entry : enumerateCameras())
        cameras.push_back (std::move (entry.device));
    return cameras;
}

std::unique_ptr<CameraSource> CameraSource::open (const juce::String& deviceId, FrameCallback onFrame)
{
    for (const auto& size : preferredSizes)
    {
        // JUCE only opens by index and enumerates again internally. Resolve
        // the ID immediately before each call to keep that window short.
        const auto cameras = enumerateCameras();
        const auto found = std::find_if (cameras.begin(), cameras.end(), [&] (const auto& entry)
        {
            return entry.device.id == deviceId;
        });
        if (found == cameras.end())
            return nullptr;

        const bool highQuality = false;
        std::unique_ptr<juce::CameraDevice> device (juce::CameraDevice::openDevice (found->juceIndex, size.minWidth, size.minHeight, size.maxWidth, size.maxHeight, highQuality));
        if (device != nullptr)
            return std::make_unique<JuceCameraSource> (std::move (device), std::move (onFrame));
    }
    return nullptr;
}
} // namespace tracker
