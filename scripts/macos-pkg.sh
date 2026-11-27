#!/bin/sh
# Makes the macOS installer: Composer.vst3 and Composer.component into the user's own plug-in
# folders (no admin password), the licence texts into ~/Library/Application Support/Agogica.
#
#   scripts/macos-pkg.sh <folder> <out.pkg> <version> <identity>
#
# <folder>: the signed (and stapled) bundles and the licence texts, as in the release zip.
# <identity>: a "Developer ID Installer: ..." identity from the Keychain.
# Called by scripts/release-macos.sh, which notarizes and staples the result.
set -eu
FROM=$1 PKG=$2 VERSION=$3 IDENTITY=$4

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
PLUGINS="$WORK/root/Library/Audio/Plug-Ins"
LICENCES="$WORK/root/Library/Application Support/Agogica/Composer licences"
mkdir -p "$PLUGINS/VST3" "$PLUGINS/Components" "$LICENCES"
ditto "$FROM/Composer.vst3" "$PLUGINS/VST3/Composer.vst3"
ditto "$FROM/Composer.component" "$PLUGINS/Components/Composer.component"
cp "$FROM/LICENSE" "$FROM/THIRD_PARTY_NOTICES.md" "$FROM/THIRD_PARTY_LICENSES.txt" "$LICENCES/"
# A copy that was downloaded carries marks the installed files must not get.
xattr -cr "$WORK/root"

# Always written where it is told to: never moved to another copy of the bundle found on the
# disk (a build folder, say), and never skipped because the installed one claims a newer version.
pkgbuild --analyze --root "$WORK/root" "$WORK/components.plist" > /dev/null
i=0
while /usr/libexec/PlistBuddy -c "Set :$i:BundleIsVersionChecked false" "$WORK/components.plist" 2> /dev/null; do
  /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$WORK/components.plist"
  i=$((i + 1))
done
[ $i = 2 ] || { echo "expected two bundles in the installer, found $i" >&2; exit 1; }

# The package version is numbers only (0.1.0-beta.1 -> 0.1.0).
pkgbuild --root "$WORK/root" --component-plist "$WORK/components.plist" \
  --identifier com.agogica.composer.pkg --version "${VERSION%%-*}" --install-location / \
  "$WORK/Composer.pkg" > /dev/null

# For this user only (the paths above are then relative to the home folder), Apple Silicon,
# macOS 11 or newer: what the plugin itself needs.
cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Composer $VERSION</title>
    <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64"/>
    <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    <license file="LICENSE.txt"/>
    <choices-outline><line choice="composer"/></choices-outline>
    <choice id="composer" title="Composer"><pkg-ref id="com.agogica.composer.pkg"/></choice>
    <pkg-ref id="com.agogica.composer.pkg">Composer.pkg</pkg-ref>
</installer-gui-script>
XML
mkdir "$WORK/resources"
cp "$FROM/LICENSE" "$WORK/resources/LICENSE.txt"

rm -f "$PKG"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" --resources "$WORK/resources" \
  --sign "$IDENTITY" --timestamp "$PKG" > /dev/null
pkgutil --check-signature "$PKG" > /dev/null
