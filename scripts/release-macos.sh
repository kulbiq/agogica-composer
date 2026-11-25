#!/bin/sh
# Builds the signed, notarized macOS zip and, when asked, attaches it to the GitHub release.
#
#   scripts/release-macos.sh                 # version from the vX.Y.Z tag on HEAD; builds the zip
#   scripts/release-macos.sh --publish       # also uploads it to the GitHub release of that tag
#   VERSION=0.1.1 scripts/release-macos.sh   # untagged test build (never published)
#   scripts/release-macos.sh --skip-build    # package what build/release already has
#
# The zip holds the bundles at its root, to be copied to the per-user folders:
#   Composer.vst3 -> ~/Library/Audio/Plug-Ins/VST3, Composer.component -> ~/Library/Audio/Plug-Ins/Components
# TRACKER=1 adds Agogica Tracker.app (-> ~/Applications). Off by default: the head-pose model's
# weights have no stated licence yet (tracker/models/MODELS.md), so a build with them isn't published.
#
# Signing: SIGN_IDENTITY, a "Developer ID Application: ..." identity from the Keychain
# (hardened runtime, secure timestamp).
# Notarization: APPLE_API_KEY + APPLE_API_ISSUER + APPLE_API_KEY_PATH, else NOTARY_PROFILE
# (a `notarytool store-credentials` profile); NOTARIZE=0 skips it for a local test build.
set -eu
cd "$(dirname "$0")/.."

SKIP_BUILD=0
PUBLISH=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    --publish) PUBLISH=1 ;;
    *) echo "unknown option: $arg" >&2; exit 1 ;;
  esac
done
TRACKER=${TRACKER:-0}

# CMake takes the version from the nearest vX.Y.Z tag; a release must be built from exactly
# that commit, so the zip name, the GitHub release and the plugin's own version agree.
BUILT=$(git describe --tags --match 'v[0-9]*' --abbrev=0 2>/dev/null | sed 's/^v//' || true)
TAGGED=$(git describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)
VERSION=${VERSION:-$TAGGED}
[ -n "$VERSION" ] || { echo "HEAD has no vX.Y.Z tag; set VERSION=x.y.z for a test build" >&2; exit 1; }
if [ $PUBLISH = 1 ]; then
  [ "$VERSION" = "$TAGGED" ] && [ "$VERSION" = "$BUILT" ] || {
    echo "Only a tagged commit is published: HEAD is not tagged v$VERSION (CMake builds $BUILT)." >&2
    exit 1
  }
  [ -z "$(git status --porcelain)" ] || {
    echo "The working tree has uncommitted changes; a release is built from a clean, tagged commit." >&2
    exit 1
  }
  [ "${NOTARIZE:-1}" != 0 ] || { echo "NOTARIZE=0 builds are for local tests only, never published" >&2; exit 1; }
  [ "$TRACKER" = 0 ] || { echo "TRACKER=1 builds aren't published: the head-pose weights have no licence yet" >&2; exit 1; }
fi
IDENTITY=${SIGN_IDENTITY:?set SIGN_IDENTITY to your \"Developer ID Application: ...\" identity}

if [ $SKIP_BUILD = 0 ]; then
  cmake --preset release
  if [ "$TRACKER" = 1 ]; then
    cmake --build --preset release --target Composer_VST3 Composer_AU ComposerTracker
  else
    cmake --build --preset release --target Composer_VST3 Composer_AU
  fi
fi

OUT=build/dist
NAME="Composer-$VERSION-macos-arm64"
STAGE="$OUT/$NAME"
ZIP="$OUT/$NAME.zip"
rm -rf "$STAGE" "$ZIP"
mkdir -p "$STAGE"
ditto build/release/Composer_artefacts/Release/VST3/Composer.vst3 "$STAGE/Composer.vst3"
ditto build/release/Composer_artefacts/Release/AU/Composer.component "$STAGE/Composer.component"

sign() { codesign --force --timestamp --options runtime --sign "$IDENTITY" "$@"; }
if [ "$TRACKER" = 1 ]; then
  ditto "build/release/tracker/ComposerTracker_artefacts/Release/Agogica Tracker.app" "$STAGE/Agogica Tracker.app"
  # Inside out: nested code first, then the bundle that contains it.
  sign "$STAGE/Agogica Tracker.app/Contents/Frameworks/libonnxruntime.1.dylib"
  sign --entitlements scripts/tracker.entitlements "$STAGE/Agogica Tracker.app"
fi
sign "$STAGE/Composer.vst3"
sign "$STAGE/Composer.component"
for b in "$STAGE"/*; do codesign --verify --deep --strict "$b"; done
echo "  signed as $IDENTITY"

if [ "${NOTARIZE:-1}" != 0 ]; then
  if [ -n "${APPLE_API_KEY:-}" ]; then
    AUTH="--key ${APPLE_API_KEY_PATH:?} --key-id $APPLE_API_KEY --issuer ${APPLE_API_ISSUER:?}"
  else
    AUTH="--keychain-profile ${NOTARY_PROFILE:?set NOTARY_PROFILE (or APPLE_API_KEY...), or NOTARIZE=0 for a local test}"
  fi
  ditto -c -k "$STAGE" "$OUT/notarize.zip"
  # shellcheck disable=SC2086 # AUTH is a list of flags
  RESULT=$(xcrun notarytool submit "$OUT/notarize.zip" $AUTH --wait --output-format json)
  rm -f "$OUT/notarize.zip"
  echo "$RESULT" | grep -q '"status":"Accepted"' || {
    echo "Notarization failed: $RESULT" >&2
    echo "Log: xcrun notarytool log <id> (same credentials)" >&2
    exit 1
  }
  # Staple the ticket into each bundle so Gatekeeper can check it offline.
  for b in "$STAGE"/*; do xcrun stapler staple -q "$b"; done
  echo "  notarized and stapled"
else
  echo "  NOT notarized (NOTARIZE=0) - fine for local tests, not for users"
fi

# No --keepParent: the bundles sit at the zip root.
ditto -c -k --sequesterRsrc "$STAGE" "$ZIP"
rm -rf "$STAGE"

(cd "$OUT" && shasum -a 256 "$NAME.zip" > "$NAME.zip.sha256")
echo "$ZIP"
echo "  version $VERSION  size $(stat -f %z "$ZIP")  sha256 $(cut -d' ' -f1 "$ZIP.sha256")"

if [ $PUBLISH = 1 ]; then
  # The Windows workflow may have made the release for this tag already.
  gh release view "v$VERSION" > /dev/null 2>&1 || gh release create "v$VERSION" --title "Composer $VERSION" --generate-notes
  gh release upload "v$VERSION" "$ZIP" "$ZIP.sha256"
  echo "  attached to the GitHub release v$VERSION"
else
  echo "  not published (--publish attaches it to the GitHub release)"
fi
