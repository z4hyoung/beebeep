#!/bin/bash
set -e

# --- CONFIGURATION ---
PACKAGE_NAME="beebeep"
VERSION="5.9.1"
ARCH=$(dpkg --print-architecture)

# --- PATHS ---
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SOURCE_DIR/build/appimage"
APPDIR="$BUILD_DIR/${PACKAGE_NAME}.AppDir"

echo "=== Preparing AppImage Build Environment ==="
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib"

# --- 1. DOWNLOAD LINUXDEPLOY AND QT PLUGIN ---
echo "Downloading packaging tools..."
cd "$BUILD_DIR"
wget -q -N https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget -q -N https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage

chmod +x linuxdeploy-x86_64.AppImage
chmod +x linuxdeploy-plugin-qt-x86_64.AppImage

# --- 2. COMPILE BEEBEEP SOURCE ---
echo "Compiling BeeBEEP upstream source..."
cd "$SOURCE_DIR"
# Clean previous build artifacts inside source if any
[ -f Makefile ] && make distclean || true

# Configure and compile using release flags
qmake PREFIX=/usr beebeep-desktop.pro
make -j$(nproc)

# --- 3. STAGE FILES INTO APPDIR ---
echo "Staging files into AppDir structure..."
cp test/beebeep "$APPDIR/usr/bin/"
cp test/lib*.so* "$APPDIR/usr/lib/"

# Create the required desktop file inside AppDir root
cat <<EOF > "$APPDIR/$PACKAGE_NAME.desktop"
[Desktop Entry]
Name=BeeBEEP
Version=1.0
Exec=beebeep
Comment=BeeBEEP free office messenger
Icon=beebeep
Type=Application
Terminal=false
StartupNotify=false
Categories=Office;InstantMessaging;
EOF

# Copy icon into AppDir root (linuxdeploy will handle moving it to usr/share/icons)
cp src/images/beebeep.png "$APPDIR/"

# --- 4. RUN LINUXDEPLOY WITH QT PLUGIN ---
echo "Running linuxdeploy to bundle dependencies and generate AppImage..."
cd "$BUILD_DIR"

# Export variables required by linuxdeploy
export VERSION="$VERSION"

# Run linuxdeploy pointing explicitly to the executable (-e), 
# desktop file (-d), and icon (-i) to guarantee AppRun creation.
./linuxdeploy-x86_64.AppImage \
    --appdir "$APPDIR" \
    -e "$APPDIR/usr/bin/beebeep" \
    -d "$APPDIR/$PACKAGE_NAME.desktop" \
    -i "$APPDIR/$PACKAGE_NAME.png" \
    --plugin qt \
    --output appimage

echo "=== AppImage generation completed successfully! ==="
ls -lh "$BUILD_DIR"/*.AppImage

