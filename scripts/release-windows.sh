#!/bin/sh
# Publishes the Windows release zip that Agogica Hub installs, built by GitHub Actions
# (.github/workflows/windows-release.yml, on the vX.Y.Z tag) - no Windows machine needed.
#
#   scripts/release-windows.sh               # version from the vX.Y.Z tag on HEAD
#
# Downloads that run's zip, checks it (Composer.vst3 alone at the root, no development licence
# key), signs it with minisign - the Agogica Hub updater key, which the Windows hub checks before
# unpacking (there is no Developer ID on Windows) - then uploads and registers it like
# release-macos.sh (R2_BUCKET, POCKETBASE_URL, POCKETBASE_TOKEN; same refusal of a version that
# is already registered, RELEASE_FORCE=1 overrides).
# The key comes from agogica-platform's desktop/.env.release (TAURI_SIGNING_PRIVATE_KEY,
# TAURI_SIGNING_PRIVATE_KEY_PASSWORD; PLATFORM_DIR, default ../agogica-platform), and the
# signing tool from its desktop/node_modules (cd desktop && bun install).
set -eu
cd "$(dirname "$0")/.."

PLATFORM=${PLATFORM_DIR:-../agogica-platform}
VERSION=$(git describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)
[ -n "$VERSION" ] || { echo "HEAD has no vX.Y.Z tag: Windows releases are built from a tag" >&2; exit 1; }
[ -z "$(git status --porcelain)" ] || [ "${RELEASE_FORCE:-}" = 1 ] || {
  echo "The working tree has uncommitted changes; a release is published from a clean, tagged commit." >&2
  exit 1
}
[ -f "$PLATFORM/desktop/.env.release" ] && . "$PLATFORM/desktop/.env.release"
: "${TAURI_SIGNING_PRIVATE_KEY:?Set TAURI_SIGNING_PRIVATE_KEY (Agogica Hub updater key), e.g. in $PLATFORM/desktop/.env.release}"
TAURI="$PLATFORM/desktop/node_modules/.bin/tauri"
[ -x "$TAURI" ] || { echo "Missing $TAURI (cd $PLATFORM/desktop && bun install)" >&2; exit 1; }

# The superuser token never goes on a command line (visible in ps): curl reads it from a file.
HEADERS=""
if [ -n "${POCKETBASE_URL:-}" ] && [ -n "${POCKETBASE_TOKEN:-}" ]; then
  HEADERS=$(mktemp)
  chmod 600 "$HEADERS"
  trap 'rm -f "$HEADERS"' EXIT
  printf 'Authorization: %s\n' "$POCKETBASE_TOKEN" > "$HEADERS"
  FILTER=$(printf "product='composer' && version='%s' && platform='windows'" "$VERSION" | sed 's/ /%20/g; s/&/%26/g; s/=/%3D/g')
  EXISTING=$(curl -fsS -H @"$HEADERS" "$POCKETBASE_URL/api/collections/releases/records?perPage=1&fields=id&filter=$FILTER" | sed -n 's/.*"totalItems":\([0-9]*\).*/\1/p')
  if [ "${EXISTING:-0}" != 0 ] && [ "${RELEASE_FORCE:-}" != 1 ]; then
    echo "Composer $VERSION for Windows is already registered at $POCKETBASE_URL; publish a new version instead (RELEASE_FORCE=1 to overwrite anyway)." >&2
    exit 1
  fi
fi

# The build of exactly this commit.
RUN=$(gh run list --workflow "Windows build" --commit "$(git rev-parse HEAD)" --status success --limit 1 --json databaseId --jq '.[0].databaseId')
[ -n "$RUN" ] || { echo "No successful 'Windows build' run for $(git rev-parse --short HEAD) yet (git push origin v$VERSION, then wait for it)" >&2; exit 1; }
OUT=build/dist
NAME="Composer-$VERSION-windows-x64"
ZIP="$OUT/$NAME.zip"
rm -rf "$OUT/windows" "$ZIP" "$ZIP.sig"
mkdir -p "$OUT/windows"
gh run download "$RUN" --name composer-windows-x64 --dir "$OUT/windows"
mv "$OUT/windows/Composer-windows-x64.zip" "$ZIP"
rm -rf "$OUT/windows"

# Composer.vst3 alone at the root, and a Release build: the dev key's bytes must be absent.
ROOT=$(unzip -Z1 "$ZIP" | cut -d/ -f1 | sort -u)
[ "$ROOT" = "Composer.vst3" ] || { echo "Unexpected zip root: $ROOT" >&2; exit 1; }
DEV_KEY=$(sed -n '/developmentKey/,/}/p' Source/licensing/PublicKeys.h | grep -o '0x[0-9a-f][0-9a-f]' | sed 's/0x//' | tr -d '\n')
[ ${#DEV_KEY} -eq 64 ] || { echo "Couldn't read the development key from PublicKeys.h" >&2; exit 1; }
if unzip -p "$ZIP" 'Composer.vst3/Contents/x86_64-win/Composer.vst3' | xxd -p | tr -d '\n' | grep -q "$DEV_KEY"; then
  echo "The plug-in contains the development licence key: this is not a Release build" >&2
  exit 1
fi

# Signed with the key whose public half the hub carries (tauri.conf.json): compare the key ids,
# or every Windows hub would refuse this download.
case "$TAURI_SIGNING_PRIVATE_KEY" in
  /*|~*) "$TAURI" signer sign -f "$TAURI_SIGNING_PRIVATE_KEY" -p "${TAURI_SIGNING_PRIVATE_KEY_PASSWORD:-}" "$ZIP" >/dev/null ;;
  *) "$TAURI" signer sign -k "$TAURI_SIGNING_PRIVATE_KEY" -p "${TAURI_SIGNING_PRIVATE_KEY_PASSWORD:-}" "$ZIP" >/dev/null ;;
esac
SIGNATURE=$(cat "$ZIP.sig")
key_id() { base64 -d | sed -n 2p | base64 -d | xxd -p -s 2 -l 8; }
PUBKEY=$(sed -n 's/.*"pubkey": "\(.*\)".*/\1/p' "$PLATFORM/desktop/src-tauri/tauri.conf.json")
[ "$(printf %s "$SIGNATURE" | key_id)" = "$(printf %s "$PUBKEY" | key_id)" ] || {
  echo "Signed with a key the hub doesn't know (not the one in tauri.conf.json)" >&2
  exit 1
}

SHA=$(shasum -a 256 "$ZIP" | cut -d' ' -f1)
SIZE=$(stat -f %z "$ZIP")
echo "$ZIP"
echo "  version $VERSION  size $SIZE  sha256 $SHA  (run $RUN, signed)"

[ -n "${R2_BUCKET:-}" ] || { echo "  not uploaded or registered (set R2_BUCKET, POCKETBASE_URL, POCKETBASE_TOKEN)"; exit 0; }
KEY="composer/$VERSION/$NAME.zip"
${WRANGLER:-npx --yes wrangler@4.141.0} r2 object put "$R2_BUCKET/$KEY" --file "$ZIP" --content-type application/zip --remote
echo "  uploaded to r2://$R2_BUCKET/$KEY"

if [ -n "$HEADERS" ]; then
  curl -fsS -o /dev/null -X POST "$POCKETBASE_URL/api/collections/releases/records" \
    -H @"$HEADERS" -H "Content-Type: application/json" \
    -d "{\"product\":\"composer\",\"version\":\"$VERSION\",\"platform\":\"windows\",\"artifact_key\":\"$KEY\",\"sha256\":\"$SHA\",\"size_bytes\":$SIZE,\"signature\":\"$SIGNATURE\"}"
  echo "  registered at $POCKETBASE_URL"
else
  echo "  not registered (set POCKETBASE_URL, POCKETBASE_TOKEN)"
fi
