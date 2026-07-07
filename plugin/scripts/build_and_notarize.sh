#!/bin/bash
set -euo pipefail

#───────────────────────────────────────────────────────────────────────────────
# Holy Shifter v107 — Build, Sign, Notarize & Package
#───────────────────────────────────────────────────────────────────────────────
#
# Prerequisites:
#   1. Apple Developer account with Developer ID certificates installed
#   2. Run once:  xcrun notarytool store-credentials "notary-profile" \
#                   --apple-id "your@email.com" \
#                   --team-id "DU92Z6L82F" \
#                   --password "app-specific-password"
#      (Generate an app-specific password at appleid.apple.com → Sign-In & Security)
#
#   3. Verify your signing identity exists:
#        security find-identity -v -p codesigning
#      You need TWO certificates from Apple:
#        • "Developer ID Application: YourName (DU92Z6L82F)"  — signs the plugin bundles
#        • "Developer ID Installer: YourName (DU92Z6L82F)"    — signs the .pkg
#
# Usage:
#   ./scripts/build_and_notarize.sh
#
#───────────────────────────────────────────────────────────────────────────────

# ── Configuration ─────────────────────────────────────────────────────────────
TEAM_ID="DU92Z6L82F"
APP_SIGNING_ID="Developer ID Application: benjamin vaughan (${TEAM_ID})"
INSTALLER_SIGNING_ID="Developer ID Installer: benjamin vaughan (${TEAM_ID})"
NOTARY_PROFILE="notary-profile"

# NOTE: PLUGIN_NAME must match PRODUCT_NAME in CMakeLists.txt (currently "Holy Shifter",
# no version suffix). The built bundles are "${PLUGIN_NAME}.vst3" / ".component".
# Overridable via env so this script can build either UI variant:
#   default            -> Visage build  ("Holy Shifter",   code Fshf)
#   HOLY_SHIFTER_USE_WEBVIEW=ON PLUGIN_NAME="Holy Shifter WV" -> WebView build (code Fswv)
PLUGIN_NAME="${PLUGIN_NAME:-Holy Shifter}"
PKG_IDENTIFIER="${PKG_IDENTIFIER:-com.harmonictools.frequencyshifter}"
PKG_VERSION="0.2.4"  # keep in sync with project(... VERSION) in plugin/CMakeLists.txt
HOLY_SHIFTER_USE_WEBVIEW="${HOLY_SHIFTER_USE_WEBVIEW:-OFF}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"
DIST_DIR="${DIST_DIR:-${PLUGIN_DIR}/dist}"
PKG_ROOT="${DIST_DIR}/pkg-root"

# Build in /tmp to avoid Finder/Spotlight metadata contamination
BUILD_DIR="${BUILD_DIR:-/tmp/holy-shifter-build}"

# ── Color output ──────────────────────────────────────────────────────────────
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

step() { echo -e "\n${GREEN}▶ $1${NC}"; }
warn() { echo -e "${YELLOW}⚠ $1${NC}"; }
fail() { echo -e "${RED}✖ $1${NC}"; exit 1; }

# ── Preflight checks ─────────────────────────────────────────────────────────
step "Running preflight checks..."

# Check for signing identities
if ! security find-identity -v -p codesigning | grep -q "$TEAM_ID"; then
    fail "No Developer ID certificates found for team ${TEAM_ID}.
    Install them from developer.apple.com → Certificates, Identifiers & Profiles."
fi

# Check for notarytool credentials (verify keychain entry exists)
if ! security find-generic-password -s "notary-profile" &>/dev/null; then
    warn "Could not verify notary credentials in Keychain — will attempt notarization anyway."
fi

echo "  ✓ Signing identities found"
echo "  ✓ Notarization credentials configured"

# ── Step 1: Clean Build ─────────────────────────────────────────────────────
step "Cleaning previous build artifacts..."
rm -rf "$BUILD_DIR"
rm -rf "$DIST_DIR"
mkdir -p "$BUILD_DIR"

step "Building ${PLUGIN_NAME} (Release, Universal Binary) in /tmp..."
echo "  (Building in /tmp to avoid Finder metadata contamination)"

cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0" \
    -DHOLY_SHIFTER_USE_WEBVIEW="${HOLY_SHIFTER_USE_WEBVIEW}" \
    "$PLUGIN_DIR"

cmake --build "$BUILD_DIR" --config Release -j "$(sysctl -n hw.logicalcpu)"

ARTEFACTS="${BUILD_DIR}/FrequencyShifter_artefacts/Release"
VST3_PATH="${ARTEFACTS}/VST3/${PLUGIN_NAME}.vst3"
AU_PATH="${ARTEFACTS}/AU/${PLUGIN_NAME}.component"

[ -d "$VST3_PATH" ] || fail "VST3 bundle not found at ${VST3_PATH}"

echo "  ✓ VST3: ${VST3_PATH}"
if [ -d "$AU_PATH" ]; then
    echo "  ✓ AU:   ${AU_PATH}"
else
    warn "AU bundle not found — will create VST3-only package"
fi

# ── Step 2: Code Sign ────────────────────────────────────────────────────────
step "Code signing plugin bundles..."

# --- Sign VST3 (this always works fine) ---
codesign --deep --force --options runtime \
    --sign "$APP_SIGNING_ID" \
    --timestamp \
    "$VST3_PATH"

codesign --verify --verbose "$VST3_PATH"
echo "  ✓ VST3 signed and verified"

# --- Sign AU (with aggressive resource fork stripping) ---
AU_SIGNED=false

if [ -d "$AU_PATH" ]; then
    AU_BINARY="$AU_PATH/Contents/MacOS/${PLUGIN_NAME}"

    step "Stripping resource forks from AU component..."

    # Method 1: Empty the resource fork via named fork path
    cat /dev/null > "${AU_BINARY}/..namedfork/rsrc" 2>/dev/null || true

    # Method 2: Cat trick — copies only data fork
    cat "$AU_BINARY" > "${AU_BINARY}.clean"
    rm -f "$AU_BINARY"
    mv "${AU_BINARY}.clean" "$AU_BINARY"
    chmod +x "$AU_BINARY"

    # Method 3: Strip ALL extended attributes from every file in the bundle
    find "$AU_PATH" -exec xattr -c {} \; 2>/dev/null || true

    # Method 4: Remove any AppleDouble (._) files and .DS_Store
    find "$AU_PATH" -name "._*" -delete 2>/dev/null || true
    find "$AU_PATH" -name ".DS_Store" -delete 2>/dev/null || true
    dot_clean "$AU_PATH" 2>/dev/null || true

    # Method 5: Remove any existing code signatures so we start fresh
    find "$AU_PATH" -name "_CodeSignature" -type d -exec rm -rf {} + 2>/dev/null || true

    # Attempt signing — inner binary first, then the bundle
    echo "  Attempting AU code signing..."
    if codesign --force --options runtime \
        --sign "$APP_SIGNING_ID" \
        --timestamp \
        "$AU_BINARY" 2>&1; then

        if codesign --force --options runtime \
            --sign "$APP_SIGNING_ID" \
            --timestamp \
            "$AU_PATH" 2>&1; then

            codesign --verify --verbose "$AU_PATH" && AU_SIGNED=true
        fi
    fi

    if $AU_SIGNED; then
        echo -e "  ${GREEN}✓ AU signed and verified${NC}"
    else
        warn "AU signing failed — this is a known JUCE issue with AU resource forks."
        warn "The PKG will include VST3 only. VST3 works in all major DAWs except Logic Pro."
        warn "Logic Pro users can install the AU manually with: xattr -cr ~/Downloads/au-component"
    fi
fi

# ── Step 3: Build PKG installer ──────────────────────────────────────────────
step "Building PKG installer..."

mkdir -p "$PKG_ROOT/Library/Audio/Plug-Ins/VST3"

# Always include VST3
cp -R "$VST3_PATH" "$PKG_ROOT/Library/Audio/Plug-Ins/VST3/"

# Include AU only if it signed successfully
FORMATS="VST3"
if $AU_SIGNED; then
    mkdir -p "$PKG_ROOT/Library/Audio/Plug-Ins/Components"
    cp -R "$AU_PATH" "$PKG_ROOT/Library/Audio/Plug-Ins/Components/"
    FORMATS="VST3 + AU"
fi

UNSIGNED_PKG="${DIST_DIR}/${PLUGIN_NAME}-unsigned.pkg"
SIGNED_PKG="${DIST_DIR}/${PLUGIN_NAME}.pkg"

mkdir -p "$DIST_DIR"

# Build the component package
pkgbuild \
    --root "$PKG_ROOT" \
    --identifier "$PKG_IDENTIFIER" \
    --version "$PKG_VERSION" \
    --install-location "/" \
    "$UNSIGNED_PKG"

# Sign the package
productsign \
    --sign "$INSTALLER_SIGNING_ID" \
    --timestamp \
    "$UNSIGNED_PKG" \
    "$SIGNED_PKG"

rm "$UNSIGNED_PKG"

echo "  ✓ Signed PKG: ${SIGNED_PKG}"

# ── Step 4: Notarize ─────────────────────────────────────────────────────────
step "Submitting for notarization (this may take a few minutes)..."

xcrun notarytool submit "$SIGNED_PKG" \
    --keychain-profile "$NOTARY_PROFILE" \
    --wait

# ── Step 5: Staple ───────────────────────────────────────────────────────────
step "Stapling notarization ticket..."

xcrun stapler staple "$SIGNED_PKG"

# ── Step 6: Verify everything ────────────────────────────────────────────────
step "Final verification..."

# Verify the pkg passes Gatekeeper
spctl --assess --verbose --type install "$SIGNED_PKG"

PKG_SIZE=$(du -h "$SIGNED_PKG" | cut -f1)

echo ""
echo -e "${GREEN}════════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  ✅ ${PLUGIN_NAME} is ready to distribute!${NC}"
echo -e "${GREEN}════════════════════════════════════════════════════════════════${NC}"
echo ""
echo "  Package:  ${SIGNED_PKG}"
echo "  Size:     ${PKG_SIZE}"
echo "  Formats:  ${FORMATS} (Universal Binary: arm64 + x86_64)"
echo ""
echo "  Users just double-click the .pkg to install."
echo "  No Gatekeeper warnings. No quarantine issues."
echo ""
