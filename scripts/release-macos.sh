#!/bin/sh
# Builds the macOS release zip that Agogica Hub installs, and registers it.
#
#   scripts/release-macos.sh                 # version from the vX.Y.Z tag on HEAD
#   VERSION=0.1.1 scripts/release-macos.sh   # untagged test build (never uploaded or registered)
#   scripts/release-macos.sh --skip-build    # package what build/release already has
#
# The zip holds the bundles at its root; the hub maps each to its per-user folder:
#   Composer.vst3 -> ~/Library/Audio/Plug-Ins/VST3, Composer.component -> .../Components,
#   Agogica Tracker.app -> ~/Applications
#
# Upload: R2_BUCKET=agogica-releases puts the zip at composer/<version>/<name>.zip (private;
# the hub gets a signed link). Uses Wrangler's login (`wrangler login`).
# Registering needs POCKETBASE_URL and POCKETBASE_TOKEN (a superuser token, see agogica-platform
# README), plus DOWNLOAD_BASE when not uploading (a public URL prefix, e.g. a local test server).
# A version that is already registered is refused before anything is uploaded: the Hub checks
# downloads against the registered sha256, so an overwritten zip would fail every install.
# RELEASE_FORCE=1 overrides that (and the dirty-tree check) when you know what you're doing.
#
# Signing: Developer ID + hardened runtime + secure timestamp (SIGN_IDENTITY overrides the identity).
# Notarization: APPLE_API_KEY + APPLE_API_ISSUER + APPLE_API_KEY_PATH (CI), else NOTARY_PROFILE
# (default agogica-notary, a `notarytool store-credentials` profile);
# NOTARIZE=0 skips it for a local test build. Agogica Hub refuses bundles not signed by our team.
set -eu
cd "$(dirname "$0")/.."

SKIP_BUILD=0
[ "${1:-}" = "--skip-build" ] && SKIP_BUILD=1

# CMake takes the version from the nearest vX.Y.Z tag; a release must be built from exactly
# that commit, so the zip name, the registered row and the plugin's own version agree.
BUILT=$(git describe --tags --match 'v[0-9]*' --abbrev=0 2>/dev/null | sed 's/^v//' || true)
TAGGED=$(git describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)
VERSION=${VERSION:-$TAGGED}
[ -n "$VERSION" ] || { echo "HEAD has no vX.Y.Z tag; set VERSION=x.y.z for a test build" >&2; exit 1; }
PUBLISH=0
if [ -n "${R2_BUCKET:-}" ] || [ -n "${POCKETBASE_URL:-}" ]; then
  PUBLISH=1
  [ "$VERSION" = "$TAGGED" ] && [ "$VERSION" = "$BUILT" ] || {
    echo "Only a tagged commit is released: HEAD is not tagged v$VERSION (CMake builds $BUILT). Drop R2_BUCKET/POCKETBASE_URL for a test build." >&2
    exit 1
  }
  [ -z "$(git status --porcelain)" ] || [ "${RELEASE_FORCE:-}" = 1 ] || {
    echo "The working tree has uncommitted changes; a release is built from a clean, tagged commit." >&2
    exit 1
  }
fi

# The superuser token never goes on a command line (visible in ps): curl reads it from a file.
HEADERS=""
if [ -n "${POCKETBASE_URL:-}" ] && [ -n "${POCKETBASE_TOKEN:-}" ]; then
  HEADERS=$(mktemp)
  chmod 600 "$HEADERS"
  trap 'rm -f "$HEADERS"' EXIT
  printf 'Authorization: %s\n' "$POCKETBASE_TOKEN" > "$HEADERS"
  FILTER=$(printf "product='composer' && version='%s' && platform='macos'" "$VERSION" | sed 's/ /%20/g; s/&/%26/g; s/=/%3D/g')
  EXISTING=$(curl -fsS -H @"$HEADERS" "$POCKETBASE_URL/api/collections/releases/records?perPage=1&fields=id&filter=$FILTER" | sed -n 's/.*"totalItems":\([0-9]*\).*/\1/p')
  if [ "${EXISTING:-0}" != 0 ] && [ "${RELEASE_FORCE:-}" != 1 ]; then
    echo "Composer $VERSION for macOS is already registered at $POCKETBASE_URL; publish a new version instead (RELEASE_FORCE=1 to overwrite anyway)." >&2
    exit 1
  fi
fi

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

# A Release plugin trusts only the production licence key: the dev key's bytes must be absent.
DEV_KEY=$(sed -n '/developmentKey/,/}/p' Source/licensing/PublicKeys.h | grep -o '0x[0-9a-f][0-9a-f]' | sed 's/0x//' | tr -d '\n')
[ ${#DEV_KEY} -eq 64 ] || { echo "Couldn't read the development key from PublicKeys.h" >&2; exit 1; }
for bin in "$STAGE/Composer.vst3/Contents/MacOS/Composer" "$STAGE/Composer.component/Contents/MacOS/Composer"; do
  if xxd -p "$bin" | tr -d '\n' | grep -q "$DEV_KEY"; then
    echo "$bin contains the development licence key: this is not a Release build" >&2
    exit 1
  fi
done

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
  [ $PUBLISH = 0 ] || { echo "NOTARIZE=0 builds are for local tests only, never published" >&2; exit 1; }
  echo "  NOT notarized (NOTARIZE=0) - fine for local tests, not for users"
fi

# No --keepParent: the bundles sit at the zip root.
ditto -c -k --sequesterRsrc "$STAGE" "$ZIP"
rm -rf "$STAGE"

SHA=$(shasum -a 256 "$ZIP" | cut -d' ' -f1)
SIZE=$(stat -f %z "$ZIP")
echo "$ZIP"
echo "  version $VERSION  size $SIZE  sha256 $SHA"

# Upload to R2: CI uses an S3 key scoped to the bucket (R2_UPLOAD_ACCESS_KEY_ID/R2_UPLOAD_SECRET_ACCESS_KEY,
# Object Read & Write); locally Wrangler's login is used (a pinned version: this shell holds signing secrets).
r2_put() { # file key content-type
  if [ -n "${R2_UPLOAD_ACCESS_KEY_ID:-}" ]; then
    AWS_ACCESS_KEY_ID=$R2_UPLOAD_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY=${R2_UPLOAD_SECRET_ACCESS_KEY:?} \
      AWS_REQUEST_CHECKSUM_CALCULATION=when_required AWS_RESPONSE_CHECKSUM_VALIDATION=when_required \
      aws s3 cp "$1" "s3://$R2_BUCKET/$2" --content-type "$3" --region auto --only-show-errors \
      --endpoint-url "https://${CLOUDFLARE_ACCOUNT_ID:?}.r2.cloudflarestorage.com"
  else
    ${WRANGLER:-npx --yes wrangler@4.141.0} r2 object put "$R2_BUCKET/$2" --file "$1" --content-type "$3" --remote
  fi
}

if [ -n "${R2_BUCKET:-}" ]; then
  KEY="composer/$VERSION/$NAME.zip"
  r2_put "$ZIP" "$KEY" application/zip
  LOCATION="\"artifact_key\":\"$KEY\""
  echo "  uploaded to r2://$R2_BUCKET/$KEY"
elif [ -n "${DOWNLOAD_BASE:-}" ]; then
  LOCATION="\"download_url\":\"$DOWNLOAD_BASE/$NAME.zip\""
else
  echo "  not uploaded or registered (set R2_BUCKET or DOWNLOAD_BASE)"
  exit 0
fi

if [ -n "$HEADERS" ]; then
  curl -fsS -o /dev/null -X POST "$POCKETBASE_URL/api/collections/releases/records" \
    -H @"$HEADERS" -H "Content-Type: application/json" \
    -d "{\"product\":\"composer\",\"version\":\"$VERSION\",\"platform\":\"macos\",$LOCATION,\"sha256\":\"$SHA\",\"size_bytes\":$SIZE}"
  echo "  registered at $POCKETBASE_URL"
else
  echo "  not registered (set POCKETBASE_URL, POCKETBASE_TOKEN)"
fi

# The product image on agogica.app shows this version's editor; regenerate it there and commit it
# with the platform (its own repo). A failure here never fails the release.
PLATFORM=${PLATFORM_DIR:-../agogica-platform}
if [ -f "$PLATFORM/scripts/composer-shot.ts" ]; then
  (cd "$PLATFORM" && COMPOSER_DIR="$OLDPWD" VERSION="$VERSION" bun run shot:composer) \
    && echo "  product image updated: commit public/products/composer/hero.png in $PLATFORM" \
    || echo "  product image NOT updated (run VERSION=$VERSION bun run shot:composer in $PLATFORM)" >&2
fi
