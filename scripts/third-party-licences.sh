#!/usr/bin/env bash
# Writes the licence texts of everything the plugin contains into one file, for the release
# zips (THIRD_PARTY_NOTICES.md lists the works; this carries the texts their licences ask to
# be passed on). Run after a build: the page's packages must be installed (ui/node_modules).
# Fails if a text is missing, so a release never ships without one.
#   scripts/third-party-licences.sh out.txt            # macOS
#   scripts/third-party-licences.sh out.txt windows    # adds the WebView2 SDK (NuGet package)
set -euo pipefail
cd "$(dirname "$0")/.."
out=$1
platform=${2:-macos}

files=(
  "JUCE (AGPL-3.0: see LICENSE)|JUCE/LICENSE.md"
  "VST3 SDK|JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt"
  "zlib|JUCE/modules/juce_core/zip/zlib/LICENSE"
  "HarfBuzz|JUCE/modules/juce_graphics/fonts/harfbuzz/COPYING"
  "SheenBidi|JUCE/modules/juce_graphics/unicode/sheenbidi/LICENSE"
  "LunaSVG|JUCE/modules/juce_graphics/drawables/lunasvg/LICENSE"
  "PlutoVG|JUCE/modules/juce_graphics/drawables/lunasvg/plutovg/LICENSE"
  "libpng|JUCE/modules/juce_graphics/image_formats/pnglib/LICENSE"
  "jpeglib (Independent JPEG Group)|JUCE/modules/juce_graphics/image_formats/jpglib/README"
  "FLAC|JUCE/modules/juce_audio_formats/codecs/flac/Flac Licence.txt"
  "Ogg Vorbis|JUCE/modules/juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt"
  "SADIE II HRTF database|Resources/hrir/SADIE-II-LICENSE.txt"
  "SADIE II HRTF database: notice of changes|Resources/hrir/NOTICE"
  "React|ui/node_modules/react/LICENSE"
  "React DOM|ui/node_modules/react-dom/LICENSE"
  "scheduler|ui/node_modules/scheduler/LICENSE"
  "Space Grotesk|ui/node_modules/@fontsource/space-grotesk/LICENSE"
  "Hanken Grotesk|ui/node_modules/@fontsource/hanken-grotesk/LICENSE"
  "JetBrains Mono|ui/node_modules/@fontsource/jetbrains-mono/LICENSE"
)

if [[ $platform == windows ]]; then
  webview2=$(ls -d "$USERPROFILE"/AppData/Local/PackageManagement/NuGet/Packages/Microsoft.Web.WebView2.*/LICENSE.txt 2>/dev/null | tail -1 || true)
  [[ -n $webview2 ]] || { echo "the WebView2 SDK's LICENSE.txt wasn't found in the NuGet package" >&2; exit 1; }
  files+=("Microsoft Edge WebView2 SDK|$webview2")
else
  files+=("AudioUnitSDK|JUCE/modules/juce_audio_plugin_client/AU/AudioUnitSDK/LICENSE.txt")
fi

{
  echo "Composer contains the works below. Composer itself: LICENSE (AGPL-3.0); the list: THIRD_PARTY_NOTICES.md."
  for entry in "${files[@]}"; do
    name=${entry%%|*}
    path=${entry#*|}
    [[ -f $path ]] || { echo "licence text missing: $path ($name)" >&2; exit 1; }
    printf '\n\n================================================================================\n%s\n================================================================================\n\n' "$name"
    cat "$path"
  done
} > "$out"
