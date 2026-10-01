#!/bin/sh
# Builds the macOS release zip that Agogica Hub installs, and registers it.
#
#   scripts/release-macos.sh                 # version from the vX.Y.Z tag on HEAD
#   VERSION=0.1.1 scripts/release-macos.sh   # untagged test release
#   scripts/release-macos.sh --skip-build    # package what build/release already has
#
# The zip holds the bundles at its root; the hub maps each to its per-user folder:
#   Composer.vst3 -> ~/Library/Audio/Plug-Ins/VST3, Composer.component -> .../Components,
#   Agogica Tracker.app -> ~/Applications
#
# Upload: R2_BUCKET=agogica-releases puts the zip at composer/<version>/<name>.zip (private;
# the hub gets a signed link). Uses Wrangler's login (`wrangler login`).
# Registering needs SUPABASE_URL and SUPABASE_SECRET_KEY, plus DOWNLOAD_BASE when not uploading
# (a public URL prefix, e.g. a local test server).
#
# Signing: Developer ID + hardened runtime + secure timestamp (SIGN_IDENTITY overrides the identity).
# Notarization: APPLE_API_KEY + APPLE_API_ISSUER + APPLE_API_KEY_PATH (CI), else NOTARY_PROFILE
# (default agogica-notary, a `notarytool store-credentials` profile);
# NOTARIZE=0 skips it for a local test build. Agogica Hub refuses bundles not signed by our team.
set -eu
cd "$(dirname "$0")/.."

SKIP_BUILD=0
[ "${1:-}" = "--skip-build" ] && SKIP_BUILD=1

VERSION=${VERSION:-$(git describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)}
[ -n "$VERSION" ] || { echo "HEAD has no vX.Y.Z tag; set VERSION=x.y.z for a test release" >&2; exit 1; }

if [ $SKIP_BUILD = 0 ]; then
  cmake --preset release
  cmake --build --preset release --target Composer_VST3 Composer_AU ComposerTracker
fi

OUT=build/dist
NAME="Composer-$VERSION-macos-arm64"
STAGE="$OUT/$NAME"
ZIP="$OUT/$NAME.zip"
rm -rf "$STAGE" "$ZIP"
mkdir -p "$STAGE"
ditto build/release/Composer_artefacts/Release/VST3/Composer.vst3 "$STAGE/Composer.vst3"
ditto build/release/Composer_artefacts/Release/AU/Composer.component "$STAGE/Composer.component"
ditto "build/release/ComposerTracker_artefacts/Release/Agogica Tracker.app" "$STAGE/Agogica Tracker.app"

IDENTITY=${SIGN_IDENTITY:-"Developer ID Application: ..."}
sign() { codesign --force --timestamp --options runtime --sign "$IDENTITY" "$@"; }
# Inside out: nested code first, then the bundles that contain it.
sign "$STAGE/Agogica Tracker.app/Contents/Frameworks/libonnxruntime.1.dylib"
sign --entitlements scripts/tracker.entitlements "$STAGE/Agogica Tracker.app"
sign "$STAGE/Composer.vst3"
sign "$STAGE/Composer.component"
for b in "$STAGE"/*; do codesign --verify --deep --strict "$b"; done
echo "  signed as $IDENTITY"

if [ "${NOTARIZE:-1}" != 0 ]; then
  # CI passes the App Store Connect key directly; locally the agogica-notary Keychain profile is used.
  if [ -n "${APPLE_API_KEY:-}" ]; then
    AUTH="--key ${APPLE_API_KEY_PATH:?} --key-id $APPLE_API_KEY --issuer ${APPLE_API_ISSUER:?}"
  else
    AUTH="--keychain-profile ${NOTARY_PROFILE:-agogica-notary}"
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

SHA=$(shasum -a 256 "$ZIP" | cut -d' ' -f1)
SIZE=$(stat -f %z "$ZIP")
echo "$ZIP"
echo "  version $VERSION  size $SIZE  sha256 $SHA"

if [ -n "${R2_BUCKET:-}" ]; then
  KEY="composer/$VERSION/$NAME.zip"
  ${WRANGLER:-npx --yes wrangler@4} r2 object put "$R2_BUCKET/$KEY" --file "$ZIP" --content-type application/zip --remote
  LOCATION="\"artifact_key\":\"$KEY\""
  echo "  uploaded to r2://$R2_BUCKET/$KEY"
elif [ -n "${DOWNLOAD_BASE:-}" ]; then
  LOCATION="\"download_url\":\"$DOWNLOAD_BASE/$NAME.zip\""
else
  echo "  not uploaded or registered (set R2_BUCKET or DOWNLOAD_BASE)"
  exit 0
fi

if [ -n "${SUPABASE_URL:-}" ] && [ -n "${SUPABASE_SECRET_KEY:-}" ]; then
  curl -fsS -X POST "$SUPABASE_URL/rest/v1/releases" \
    -H "apikey: $SUPABASE_SECRET_KEY" -H "Authorization: Bearer $SUPABASE_SECRET_KEY" \
    -H "Content-Type: application/json" -H "Prefer: return=minimal" \
    -d "{\"product\":\"composer\",\"version\":\"$VERSION\",\"platform\":\"macos\",$LOCATION,\"sha256\":\"$SHA\",\"size_bytes\":$SIZE}"
  echo "  registered at $SUPABASE_URL"
else
  echo "  not registered (set SUPABASE_URL, SUPABASE_SECRET_KEY)"
fi
