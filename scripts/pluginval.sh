#!/usr/bin/env bash
# Validate the installed Composer plugins: auval + pluginval (strictness 10).
# Usage: scripts/pluginval.sh   (build first; COPY_PLUGIN_AFTER_BUILD installs them)
set -euo pipefail

# shellcheck source=pluginval.version
source "$(dirname "$0")/pluginval.version" # version, sha256_macOS
tools="$(cd "$(dirname "$0")/.." && pwd)/build/tools"
pluginval="$tools/pluginval.app/Contents/MacOS/pluginval"
plugins="$HOME/Library/Audio/Plug-Ins"

if [[ ! -x "$pluginval" ]]; then
    mkdir -p "$tools"
    curl -fsSL -o "$tools/pluginval.zip" \
        "https://github.com/Tracktion/pluginval/releases/download/$version/pluginval_macOS.zip"
    echo "$sha256_macOS  $tools/pluginval.zip" | shasum -a 256 -c - > /dev/null || {
        echo "pluginval.zip doesn't match the SHA-256 in scripts/pluginval.version" >&2
        rm -f "$tools/pluginval.zip"
        exit 1
    }
    unzip -qo "$tools/pluginval.zip" -d "$tools"
    xattr -dr com.apple.quarantine "$tools/pluginval.app" || true
fi

# Validation never reports usage, whatever this user answered in the editor.
export DO_NOT_TRACK=1

# Make the AU registrar pick up the freshly copied component.
killall -9 AudioComponentRegistrar 2>/dev/null || true

auval -v aufx Cmps Agog

for plugin in "$plugins/VST3/Composer.vst3" "$plugins/Components/Composer.component"; do
    "$pluginval" --strictness-level 10 \
        --timeout-ms 120000 --validate "$plugin"
done
