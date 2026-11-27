# Third-party notices

Composer is licensed under the GNU Affero General Public License, version 3 (see `LICENSE`).
The plugin binaries contain the works below.
Their licence texts ship with every release in `THIRD_PARTY_LICENSES.txt` (gathered by `scripts/third-party-licences.sh`).
The list follows what the build links and embeds: the JUCE modules in `CMakeLists.txt` (with `JUCE/JUCE.spdx.json` for what each bundles) and the npm packages that end up in `ui/dist`.
Agogica Tracker is not part of the released plugin and is not covered here (see `tracker/models/MODELS.md`).

## Framework

| Work | Licence | Text |
|---|---|---|
| JUCE 9.0.2 (Raw Material Software) | AGPL-3.0-only | `LICENSE`, `JUCE/LICENSE.md` |
| `@juce-framework/webview` (the page's side of the bridge) | AGPL-3.0-only | `JUCE/modules/juce_gui_extra/native/typescript/webview-interop/LICENSE.md` |

## Bundled in JUCE and linked into the plugin

| Work | Licence | Text |
|---|---|---|
| VST3 SDK 3.8.0 (Steinberg Media Technologies) | MIT | `JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt` |
| AudioUnitSDK 1.1.0 (Apple), macOS only | Apache-2.0 | `JUCE/modules/juce_audio_plugin_client/AU/AudioUnitSDK/LICENSE.txt` |
| zlib 1.3.2 | Zlib | `JUCE/modules/juce_core/zip/zlib/LICENSE` |
| HarfBuzz 14.2.1 | MIT (modern variant) | `JUCE/modules/juce_graphics/fonts/harfbuzz/COPYING` |
| SheenBidi 2.9.0 | Apache-2.0 | `JUCE/modules/juce_graphics/unicode/sheenbidi/LICENSE` |
| LunaSVG 3.5.0 and PlutoVG 1.3.2 | MIT | `JUCE/modules/juce_graphics/drawables/lunasvg/LICENSE` |
| libpng 1.6.58 | libpng-2.0 | `JUCE/modules/juce_graphics/image_formats/pnglib/LICENSE` |
| jpeglib 10.0 (Independent JPEG Group) | IJG | `JUCE/modules/juce_graphics/image_formats/jpglib/README` |
| FLAC 1.5.0, libogg 1.3.4, libvorbis 1.3.7 (Xiph.Org) | BSD-3-Clause | in the sources under `JUCE/modules/juce_audio_formats/codecs/` |

## Windows only

| Work | Licence | Text |
|---|---|---|
| Microsoft Edge WebView2 SDK (the loader, linked statically) | BSD-3-Clause | `LICENSE.txt` in the `Microsoft.Web.WebView2` NuGet package |

## Sound data

| Work | Licence | Text |
|---|---|---|
| SADIE II database, KU100 head-related impulse responses (University of York), resampled and re-packed | Apache-2.0 | `Resources/hrir/SADIE-II-LICENSE.txt`, changes in `Resources/hrir/NOTICE` |

## Web page (embedded in the plugin)

| Work | Licence |
|---|---|
| React and React DOM 19, scheduler (Meta Platforms) | MIT |
| Space Grotesk (Florian Karsten), Hanken Grotesk (Hanken Design Co.), JetBrains Mono (JetBrains), packaged by Fontsource | SIL Open Font License 1.1 |

Each package's licence text is in its folder under `ui/node_modules/` after `npm ci`.

## Used to build and test only (not in the binaries)

Catch2 (BSL-1.0), Vite, Vitest and TypeScript (MIT / Apache-2.0), pluginval (GPL-3.0).

All of the above may be combined with and distributed under AGPL-3.0.
