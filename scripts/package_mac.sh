#!/usr/bin/env bash
#
# INFINIVERB (code name RVB1) — macOS build / sign / notarize / installer package
#
# Produces, in dist/:
#   - a signed, notarized, stapled .pkg that installs, system-wide,
#       INFINIVERB.vst3       into /Library/Audio/Plug-Ins/VST3
#       INFINIVERB.component  into /Library/Audio/Plug-Ins/Components (AU)
#     both universal (Apple Silicon and Intel), macOS 10.13 and later
#   - the corresponding source as a .tar.gz, because the AGPLv3 requires the
#     source to go with every binary that is conveyed
#
# Built in build-universal/, separate from the native development build.
#
# Usage:
#   scripts/package_mac.sh --label beta1          full run
#   scripts/package_mac.sh --label beta1 --no-notarize   sign and package only
#
# Prerequisites (one-time):
#   1. Developer ID Application + Developer ID Installer certificates installed
#   2. Notary credentials stored in the keychain (xcrun notarytool store-credentials)
#   3. The signing identity, from the environment or from scripts/signing.local.sh
#      (not in version control):
#        RVB1_TEAM_ID=...          the Apple developer team ID
#        RVB1_SIGN_NAME=...        the name on the Developer ID certificates
#        RVB1_NOTARY_PROFILE=...   the keychain profile from step 2
#
set -euo pipefail

#-----------------------------------------------------------------------------
# Configuration
#-----------------------------------------------------------------------------
LOCAL_SIGNING="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/signing.local.sh"
# shellcheck source=/dev/null
[ -f "$LOCAL_SIGNING" ] && . "$LOCAL_SIGNING"
TEAM_ID="${RVB1_TEAM_ID:?Set RVB1_TEAM_ID, or create scripts/signing.local.sh (see the header)}"
SIGN_NAME="${RVB1_SIGN_NAME:?Set RVB1_SIGN_NAME, or create scripts/signing.local.sh (see the header)}"
APP_SIGN_ID="Developer ID Application: $SIGN_NAME ($TEAM_ID)"
PKG_SIGN_ID="Developer ID Installer: $SIGN_NAME ($TEAM_ID)"
NOTARY_PROFILE="${RVB1_NOTARY_PROFILE:?Set RVB1_NOTARY_PROFILE, or create scripts/signing.local.sh (see the header)}"

VST3_PKG_ID="com.fatbirdstudios.infiniverb.vst3"
AU_PKG_ID="com.fatbirdstudios.infiniverb.au"
VST3_LOCATION="/Library/Audio/Plug-Ins/VST3"
AU_LOCATION="/Library/Audio/Plug-Ins/Components"

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$BASE_DIR/build-universal"
ARTEFACTS="$BUILD_DIR/plugin/RVB1_artefacts/Release"
VST3="$ARTEFACTS/VST3/INFINIVERB.vst3"
AU="$ARTEFACTS/AU/INFINIVERB.component"

# Single source of truth: the version lives in CMakeLists.txt.
VERSION="$(sed -n 's/^project(RVB1 VERSION \([0-9.]*\).*/\1/p' "$BASE_DIR/CMakeLists.txt")"

LABEL=""
DO_NOTARIZE=1
while [ $# -gt 0 ]; do
  case "$1" in
    --label)       LABEL="$2"; shift ;;
    --no-notarize) DO_NOTARIZE=0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

FULL_VERSION="$VERSION${LABEL:+-$LABEL}"
DIST_DIR="$BASE_DIR/dist"
WORK_DIR="$DIST_DIR/.work"
RESOURCES_DIR="$WORK_DIR/resources"
VST3_PKG="$WORK_DIR/INFINIVERB-VST3.pkg"
AU_PKG="$WORK_DIR/INFINIVERB-AU.pkg"
DIST_XML="$WORK_DIR/distribution.xml"
PKG_OUT="$DIST_DIR/INFINIVERB-$FULL_VERSION-macOS.pkg"
SOURCE_OUT="$DIST_DIR/INFINIVERB-$FULL_VERSION-source.tar.gz"

step() { printf '\n\033[1;36m==> %s\033[0m\n' "$1"; }
ok()   { printf '\033[1;32m    ok: %s\033[0m\n' "$1"; }
die()  { printf '\n\033[1;31mFAILED: %s\033[0m\n' "$1" >&2; exit 1; }

#-----------------------------------------------------------------------------
# 0. Preflight
#-----------------------------------------------------------------------------
step "Preflight checks"

[ -n "$VERSION" ] || die "Could not read the version from CMakeLists.txt"
ok "version $FULL_VERSION"

# The source archive must be exactly what the binary was built from.
[ -z "$(git -C "$BASE_DIR" status --porcelain)" ] \
  || die "Working tree has uncommitted changes — commit first, so the source archive matches the binary."
ok "working tree clean at $(git -C "$BASE_DIR" rev-parse --short HEAD)"

for f in LICENSE THIRD-PARTY-NOTICES.txt installer/README.txt; do
  [ -f "$BASE_DIR/$f" ] || die "Missing $f — it must ship with the installer."
done
ok "licence, notices and read-me present"

# The manual is embedded in the plug-in and prints a version; it must be the
# one being released.
MANUAL_VERSION="$(cat "$BASE_DIR/docs/manual/rendered-version.txt" 2>/dev/null || true)"
[ "$MANUAL_VERSION" = "$VERSION" ] \
  || die "The manual was rendered for version '${MANUAL_VERSION:-none}', not $VERSION — run scripts/build_manual.sh and commit."
ok "manual rendered for $VERSION"

# Captured into a variable rather than piped into grep -q: under pipefail, grep
# exiting on its first match kills the upstream command with SIGPIPE.
IDENTITIES="$(security find-identity -v 2>&1)"
case "$IDENTITIES" in *"$APP_SIGN_ID"*) ;; *) die "Missing certificate: $APP_SIGN_ID" ;; esac
case "$IDENTITIES" in *"$PKG_SIGN_ID"*) ;; *) die "Missing certificate: $PKG_SIGN_ID" ;; esac
ok "both Developer ID certificates present"

if [ "$DO_NOTARIZE" -eq 1 ]; then
  xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1 \
    || die "Notary profile '$NOTARY_PROFILE' not found or invalid."
  ok "notary credentials valid"
fi

#-----------------------------------------------------------------------------
# 1. Build
#-----------------------------------------------------------------------------
step "Building Release (universal: arm64 + x86_64)"
cmake -S "$BASE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DRVB1_UNIVERSAL_BINARY=ON >/dev/null
# A failed build must stop here: a plugin left over from an earlier build would
# otherwise pass every check below and ship.
mkdir -p "$BUILD_DIR"
cmake --build "$BUILD_DIR" --target RVB1_VST3 RVB1_AU -j8 > "$BUILD_DIR/build-for-release.log" 2>&1 \
  || die "Build failed — see $BUILD_DIR/build-for-release.log"
[ -d "$VST3" ] || die "Build produced no VST3 at: $VST3"
[ -d "$AU" ]   || die "Build produced no AU at: $AU"
ok "build complete"

# Both slices, both formats, and the oldest macOS each slice loads on.
for BUNDLE in "$VST3" "$AU"; do
  BIN="$BUNDLE/Contents/MacOS/INFINIVERB"
  ARCHS="$(lipo -archs "$BIN")"
  [[ "$ARCHS" == *"arm64"* && "$ARCHS" == *"x86_64"* ]] || die "$(basename "$BUNDLE") is not universal (got: $ARCHS)"
  # Targets this old carry LC_VERSION_MIN_MACOSX ("version"), newer ones LC_BUILD_VERSION ("minos").
  MINOS_X86="$(otool -arch x86_64 -l "$BIN" | awk '/LC_VERSION_MIN_MACOSX|LC_BUILD_VERSION/{f=1} f&&($1=="version"||$1=="minos"){print $2; exit}')"
  [ "$MINOS_X86" = "10.13" ] || die "$(basename "$BUNDLE") x86_64 slice targets macOS $MINOS_X86, expected 10.13"
done
ok "universal ($ARCHS), Intel slice from macOS 10.13"


# JUCE gives the AU's resource usage network access along with file access.
# INFINIVERB never touches the network, so that entry goes; file access stays
# (presets and the manual are written to the user's Library).
AU_PLIST="$AU/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Delete :AudioComponents:0:resourceUsage:network.client" "$AU_PLIST" 2>/dev/null || true
if /usr/libexec/PlistBuddy -c "Print :AudioComponents:0:resourceUsage:network.client" "$AU_PLIST" >/dev/null 2>&1; then
  die "The AU still asks for network access"
fi
ok "the AU asks for no network access"

#-----------------------------------------------------------------------------
# 2. Sign the plug-in bundle
#-----------------------------------------------------------------------------
step "Code signing the plug-ins (Developer ID + hardened runtime)"
for BUNDLE in "$VST3" "$AU"; do
  codesign --force --sign "$APP_SIGN_ID" --options runtime --timestamp "$BUNDLE"
  codesign --verify --deep --strict "$BUNDLE" || die "Signature verification failed: $(basename "$BUNDLE")"

  SIG_INFO="$(codesign -dvv "$BUNDLE" 2>&1)"
  case "$SIG_INFO" in *"(runtime)"*) ;; *) die "Hardened runtime flag missing: $(basename "$BUNDLE")" ;; esac
  case "$SIG_INFO" in *"TeamIdentifier=$TEAM_ID"*) ;; *) die "Unexpected team identifier: $(basename "$BUNDLE")" ;; esac
done
ok "both signed, hardened runtime + Developer ID confirmed"

#-----------------------------------------------------------------------------
# 3. Build the installer
#-----------------------------------------------------------------------------
rm -rf "$WORK_DIR" "$PKG_OUT" "$SOURCE_OUT"
mkdir -p "$WORK_DIR/vst3" "$WORK_DIR/au" "$RESOURCES_DIR"

# The licence and the notices are shown by the installer and stay with the user.
cp "$BASE_DIR/LICENSE" "$RESOURCES_DIR/LICENSE.txt"
{ cat "$BASE_DIR/installer/README.txt"; printf '\n\n'; cat "$BASE_DIR/THIRD-PARTY-NOTICES.txt"; } \
  > "$RESOURCES_DIR/README.txt"

step "Staging payloads"
ditto "$VST3" "$WORK_DIR/vst3/INFINIVERB.vst3"        # bundle-aware; keeps the signature intact
ditto "$AU"   "$WORK_DIR/au/INFINIVERB.component"
ok "INFINIVERB.vst3 -> $VST3_LOCATION"
ok "INFINIVERB.component -> $AU_LOCATION"

step "Building component packages"
pkgbuild --root "$WORK_DIR/vst3" --identifier "$VST3_PKG_ID" --version "$VERSION" \
         --install-location "$VST3_LOCATION" --ownership recommended "$VST3_PKG" >/dev/null
pkgbuild --root "$WORK_DIR/au" --identifier "$AU_PKG_ID" --version "$VERSION" \
         --install-location "$AU_LOCATION" --ownership recommended "$AU_PKG" >/dev/null
ok "$(basename "$VST3_PKG"), $(basename "$AU_PKG")"

step "Writing installer definition"
cat > "$DIST_XML" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
    <title>INFINIVERB $FULL_VERSION</title>
    <organization>com.fatbirdstudios</organization>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <os-version min="10.13"/>
    <!-- System-wide only: plug-in hosts are not guaranteed to scan ~/Library -->
    <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>
    <readme file="README.txt" mime-type="text/plain"/>
    <!-- AGPLv3 must be conveyed with the binary -->
    <license file="LICENSE.txt" mime-type="text/plain"/>
    <!-- Both formats by default; Customize lets the user leave one out. -->
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
    </choices-outline>
    <choice id="vst3" title="VST3" description="For Ableton Live, Cubase, Studio One, Reaper, Bitwig, FL Studio and most other hosts.">
        <pkg-ref id="$VST3_PKG_ID"/>
    </choice>
    <choice id="au" title="Audio Unit" description="For Logic Pro, GarageBand and MainStage.">
        <pkg-ref id="$AU_PKG_ID"/>
    </choice>
    <pkg-ref id="$VST3_PKG_ID" version="$VERSION" onConclusion="none">$(basename "$VST3_PKG")</pkg-ref>
    <pkg-ref id="$AU_PKG_ID" version="$VERSION" onConclusion="none">$(basename "$AU_PKG")</pkg-ref>
</installer-gui-script>
XML
ok "distribution.xml"

step "Building and signing the installer"
productbuild --distribution "$DIST_XML" \
             --package-path "$WORK_DIR" \
             --resources "$RESOURCES_DIR" \
             --sign "$PKG_SIGN_ID" \
             --timestamp \
             "$PKG_OUT" >/dev/null
pkgutil --check-signature "$PKG_OUT" >/dev/null || die "Installer signature check failed"
ok "$(basename "$PKG_OUT")"

#-----------------------------------------------------------------------------
# 4. Notarize
#-----------------------------------------------------------------------------
if [ "$DO_NOTARIZE" -eq 1 ]; then
  step "Submitting to Apple notary service (this can take a few minutes)"
  SUBMIT_OUT="$(xcrun notarytool submit "$PKG_OUT" --keychain-profile "$NOTARY_PROFILE" --wait 2>&1)" || true
  echo "$SUBMIT_OUT"
  SUBMISSION_ID="$(echo "$SUBMIT_OUT" | awk '/id: /{print $2; exit}')"

  case "$SUBMIT_OUT" in
    *"status: Accepted"*) ;;
    *)
      if [ -n "${SUBMISSION_ID:-}" ]; then
        printf '\n--- notary log ---\n'
        xcrun notarytool log "$SUBMISSION_ID" --keychain-profile "$NOTARY_PROFILE" || true
      fi
      die "Notarization was not accepted."
      ;;
  esac
  ok "notarization accepted"

  step "Stapling ticket"
  xcrun stapler staple "$PKG_OUT" || die "Stapling failed"
  xcrun stapler validate "$PKG_OUT" || die "Staple validation failed"
  ok "ticket stapled and validated"
fi

#-----------------------------------------------------------------------------
# 5. Corresponding source
#-----------------------------------------------------------------------------
step "Archiving the corresponding source"
git -C "$BASE_DIR" archive --format=tar.gz --prefix="INFINIVERB-$FULL_VERSION-source/" -o "$SOURCE_OUT" HEAD
ok "$(basename "$SOURCE_OUT")"

rm -rf "$WORK_DIR"

#-----------------------------------------------------------------------------
# 6. Summary
#-----------------------------------------------------------------------------
step "Done"
echo "    Installer : $PKG_OUT"
echo "    Size      : $(du -h "$PKG_OUT" | cut -f1)"
echo "    Source    : $SOURCE_OUT"
echo "    Archs     : $ARCHS (macOS 10.13+ on Intel, 11+ on Apple Silicon)"
echo "    Installs  : $VST3_LOCATION/INFINIVERB.vst3"
echo "                $AU_LOCATION/INFINIVERB.component"
if [ "$DO_NOTARIZE" -eq 1 ]; then
  echo "    Status    : signed, notarized, stapled"
else
  echo "    Status    : signed only (NOT notarized — local testing build)"
fi
echo
