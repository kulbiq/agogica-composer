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
# and the licence texts next to them.
# A tag with a suffix (v0.1.0-beta.1) makes a GitHub pre-release.
# TRACKER=1 adds Agogica Tracker.app (-> ~/Applications). Off by default: the head-pose model's
# weights have no stated licence yet (tracker/models/MODELS.md), so a build with them isn't
# published and its zip says so in its name.
# COMPOSER_TELEMETRY_KEY: the key of the usage-data project (PRIVACY.md); without it the build
# never asks and never sends.
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
# that commit, so the zip name, the GitHub release and the plugin's own version agree (the
# version CMake really built is checked again after the build).
TAGGED=$(git describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)
VERSION=${VERSION:-$TAGGED}
[ -n "$VERSION" ] || { echo "HEAD has no vX.Y.Z tag; set VERSION=x.y.z for a test build" >&2; exit 1; }
if [ $PUBLISH = 1 ]; then
  [ "$VERSION" = "$TAGGED" ] || {
    echo "Only a tagged commit is published: HEAD is not tagged v$VERSION." >&2
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
  cmake --preset release "-DCOMPOSER_TELEMETRY_KEY=${COMPOSER_TELEMETRY_KEY:-}"
  if [ "$TRACKER" = 1 ]; then
    cmake --build --preset release --target Composer_VST3 Composer_AU ComposerTracker
  else
    cmake --build --preset release --target Composer_VST3 Composer_AU
  fi
fi

# What the binaries say about themselves must be what the zip says.
BUILT=$(cat build/release/version.txt 2>/dev/null || true)
if [ $PUBLISH = 1 ] && [ "$BUILT" != "$VERSION" ]; then
  echo "The build in build/release is version '$BUILT', not $VERSION: build again without --skip-build." >&2
  exit 1
fi
VST3=build/release/Composer_artefacts/Release/VST3/Composer.vst3
MINOS=$(otool -l "$VST3/Contents/MacOS/Composer" | awk '/LC_BUILD_VERSION/ { found = 1 } found && /minos/ { print $2; exit }')
[ "$MINOS" = 11.0 ] || { echo "The plugin needs macOS $MINOS or newer, not 11.0 (CMAKE_OSX_DEPLOYMENT_TARGET)." >&2; exit 1; }

OUT=build/dist
NAME="Composer-$VERSION-macos-$(uname -m)"
[ "$TRACKER" = 0 ] || NAME="$NAME-with-tracker-DO-NOT-PUBLISH"
STAGE="$OUT/$NAME"
ZIP="$OUT/$NAME.zip"
rm -rf "$STAGE" "$ZIP"
mkdir -p "$STAGE"
ditto "$VST3" "$STAGE/Composer.vst3"
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
  # The credentials as separate arguments (a key path may have spaces in it).
  if [ -n "${APPLE_API_KEY:-}" ]; then
    set -- --key "${APPLE_API_KEY_PATH:?}" --key-id "$APPLE_API_KEY" --issuer "${APPLE_API_ISSUER:?}"
  else
    set -- --keychain-profile "${NOTARY_PROFILE:?set NOTARY_PROFILE (or APPLE_API_KEY...), or NOTARIZE=0 for a local test}"
  fi
  ditto -c -k "$STAGE" "$OUT/notarize.zip"
  # Its own exit status is looked at below, with what it said: a refusal must show its reason.
  RESULT=$(xcrun notarytool submit "$OUT/notarize.zip" "$@" --wait --output-format plist) || true
  rm -f "$OUT/notarize.zip"
  STATUS=$(echo "$RESULT" | plutil -extract status raw -o - - 2>/dev/null || true)
  [ "$STATUS" = Accepted ] || {
    echo "Notarization failed (status '$STATUS'): $RESULT" >&2
    echo "Log: xcrun notarytool log <id> (same credentials)" >&2
    exit 1
  }
  # Staple the ticket into each bundle so Gatekeeper can check it offline, then ask Gatekeeper
  # itself: what a user's Mac will say about each bundle.
  for b in "$STAGE"/*; do
    xcrun stapler staple -q "$b"
    xcrun stapler validate -q "$b"
    case "$b" in *.app) kind=execute ;; *) kind=install ;; esac
    spctl --assess --type "$kind" "$b"
  done
  echo "  notarized, stapled and accepted by Gatekeeper"
else
  echo "  NOT notarized (NOTARIZE=0) - fine for local tests, not for users"
fi

# The licence texts go with the binaries (after signing and stapling: they aren't code).
cp LICENSE THIRD_PARTY_NOTICES.md Resources/hrir/SADIE-II-LICENSE.txt "$STAGE/"
cp Resources/hrir/NOTICE "$STAGE/SADIE-II-NOTICE.txt"

# No --keepParent: the bundles sit at the zip root.
ditto -c -k --sequesterRsrc "$STAGE" "$ZIP"
rm -rf "$STAGE"

(cd "$OUT" && shasum -a 256 "$NAME.zip" > "$NAME.zip.sha256")
echo "$ZIP"
echo "  version $VERSION  size $(stat -f %z "$ZIP")  sha256 $(cut -d' ' -f1 "$ZIP.sha256")"

if [ $PUBLISH = 1 ]; then
  # The Windows workflow may have made the release for this tag already.
  if ! gh release view "v$VERSION" > /dev/null 2>&1; then
    # A version with a suffix (0.1.0-beta.1) is a pre-release: it never becomes "Latest".
    case "$VERSION" in *-*) PRE=--prerelease ;; *) PRE= ;; esac
    # shellcheck disable=SC2086 # PRE is one flag or nothing
    gh release create "v$VERSION" --title "Composer $VERSION" --generate-notes $PRE || {
      status=$?
      gh release view "v$VERSION" > /dev/null 2>&1 || exit "$status"
    }
  fi
  gh release upload --clobber "v$VERSION" "$ZIP" "$ZIP.sha256"
  echo "  attached to the GitHub release v$VERSION"
else
  echo "  not published (--publish attaches it to the GitHub release)"
fi
