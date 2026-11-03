#!/bin/bash
# make_flatpak.sh - Automated Incremental Flatpak Build Script for BeeBEEP

set -e

# ==============================================================================
# CONFIGURATION
# ==============================================================================
APP_ID="org.beebeep.BeeBEEP"
VERSION="5.9.1"
ARCH="x86_64"
RUNTIME_VERSION="5.15-24.08"

BUILD_DIR="../build/flatpak"
EXPORT_REPO="$BUILD_DIR/repo"
SANDBOX_DIR="$BUILD_DIR/sandbox"
OUTPUT_BUNDLE="$BUILD_DIR/BeeBEEP-${VERSION}-${ARCH}.flatpak"
TEMP_MANIFEST="org.beebeep.BeeBEEP.temp.yaml"

CLEAN_BUILD=${CLEAN_BUILD:-false}

echo "============================================="
echo "📦 BEEBEEP FLATPAK AUTOMATED BUILDER"
echo "============================================="

cleanup() {
    echo "🧹 Cleaning up temporary manifest file..."
    rm -f "$TEMP_MANIFEST"
}
trap cleanup EXIT

# 1. VERIFY REQUIRED TOOLS
FLATPAK_BUILDER_BIN=$(command -v flatpak-builder || echo "/usr/bin/flatpak-builder")

if [ ! -x "$FLATPAK_BUILDER_BIN" ]; then
    echo "❌ ERROR: 'flatpak-builder' is not installed or not executable."
    exit 1
fi

echo "✅ Found flatpak-builder at: $FLATPAK_BUILDER_BIN"

# 2. PRE-BUILD CLEANUP
if [ "$CLEAN_BUILD" = "true" ]; then
    echo "🧹 Clean build requested: Removing previous build caches..."
    rm -rf "$SANDBOX_DIR" "$EXPORT_REPO" "$BUILD_DIR/.flatpak-builder"
else
    echo "⚡ Incremental build mode: Reusing existing build cache if available."
fi
mkdir -p "$BUILD_DIR"

# 3. GENERATE TEMPORARY MANIFEST DYNAMICALLY
SOURCE_ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)

cat << EOF > "$TEMP_MANIFEST"
app-id: $APP_ID
runtime: org.kde.Platform
runtime-version: '$RUNTIME_VERSION'
sdk: org.kde.Sdk
command: beebeep
finish-args:
  - --share=network
  - --socket=x11
  - --socket=fallback-x11
  - --socket=wayland
  - --socket=pulseaudio
  - --talk-name=org.freedesktop.Notifications

modules:
  - name: beebeep
    buildsystem: qmake
    config-opts:
      - beebeep-desktop.pro
    make-args:
      - QMAKE=/usr/bin/qmake
    make-install-args:
      - INSTALL_ROOT=/app
    post-install:
      # Se il binario è finito in /app/usr/bin/beebeep, spostalo in /app/bin/beebeep per Flatpak
      - mkdir -p /app/bin
      - test -f /app/usr/bin/beebeep && mv /app/usr/bin/beebeep /app/bin/beebeep || true
      - test -f /app/beebeep && mv /app/beebeep /app/bin/beebeep || true
    build-options:
      arch:
        $ARCH: {}
    sources:
      - type: dir
        path: $SOURCE_ROOT_DIR
EOF

echo "✅ Temporary manifest generated for source: $SOURCE_ROOT_DIR"

# 4. PREPARE RUNTIME AND SDK DEPENDENCIES
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo || true
flatpak install --user -y flathub org.kde.Platform//$RUNTIME_VERSION org.kde.Sdk//$RUNTIME_VERSION || true

# 5. BUILD FLATPAK SANDBOX
echo "⚙️ Building Flatpak in isolated sandbox..."
rm -rf "$SANDBOX_DIR" # Previene l'errore 'sandbox is not empty' mantenendo la cache

"$FLATPAK_BUILDER_BIN" --ccache \
                       --state-dir="$BUILD_DIR/.flatpak-builder" \
                       --repo="$EXPORT_REPO" \
                       "$SANDBOX_DIR" \
                       "$TEMP_MANIFEST"

# 6. CREATE STANDALONE BUNDLE (.flatpak)
echo "📦 Packaging standalone offline bundle: $OUTPUT_BUNDLE"
flatpak build-bundle "$EXPORT_REPO" "$OUTPUT_BUNDLE" "$APP_ID"

# 7. FINAL SUCCESS SUMMARY
echo "================================================================="
echo "🎉 SUCCESS: BeeBEEP Flatpak bundle created successfully!"
echo "================================================================="
echo "📁 Output file:  $OUTPUT_BUNDLE"
echo "📏 Size:         $(du -h "$OUTPUT_BUNDLE" | cut -f1)"
echo "🚀 To test/install locally, run:"
echo "   flatpak install --user $OUTPUT_BUNDLE"
echo "   flatpak run $APP_ID"
echo "================================================================="

