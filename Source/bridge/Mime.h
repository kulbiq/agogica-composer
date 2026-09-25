#pragma once

#include <juce_core/juce_core.h>
#include <unordered_map>

namespace web
{
// Content type for a file served to a WebBrowserComponent page (by extension).
inline const char* mimeType (const juce::String& path)
{
    static const std::unordered_map<juce::String, const char*> types {
        { "html",  "text/html" },
        { "js",    "text/javascript" },
        { "mjs",   "text/javascript" },
        { "css",   "text/css" },
        { "json",  "application/json" },
        { "svg",   "image/svg+xml" },
        { "png",   "image/png" },
        { "woff2", "font/woff2" },
        { "woff",  "font/woff" },
        { "wasm",  "application/wasm" }, // required for streaming compilation
    };

    const auto it = types.find (path.fromLastOccurrenceOf (".", false, false).toLowerCase());
    return it != types.end() ? it->second : "application/octet-stream";
}
} // namespace web
