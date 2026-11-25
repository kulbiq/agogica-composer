#include "UiResources.h"
#include "Mime.h"
#include <BinaryData.h>

namespace web
{
namespace
{
juce::ZipFile& getUiZip()
{
    static juce::MemoryInputStream stream { BinaryData::ui_zip, BinaryData::ui_zipSize, false };
    static juce::ZipFile zip { stream };
    return zip;
}
} // namespace

std::optional<juce::WebBrowserComponent::Resource> uiResource (const juce::String& url)
{
    const auto path = url == "/" ? juce::String { "index.html" }
                                 : url.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);

    auto& zip = getUiZip();
    const auto* entry = zip.getEntry (path);

    if (entry == nullptr)
        return std::nullopt;

    std::unique_ptr<juce::InputStream> in { zip.createStreamForEntry (*entry) };

    if (in == nullptr)
        return std::nullopt;

    const auto length = in->getTotalLength();

    if (length < 0)
        return std::nullopt;

    std::vector<std::byte> data ((size_t) length);

    if (in->read (data.data(), data.size()) != (juce::int64) data.size())
        return std::nullopt;

    return juce::WebBrowserComponent::Resource { std::move (data), mimeType (path) };
}
} // namespace web
