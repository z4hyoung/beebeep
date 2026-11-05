#!/bin/bash
#------------------------------------------------------------------
# build.sh - build libybcore.so (BeeBEEP/YongBEEP core, no GUI) for
# Android arm64-v8a and deploy it into the YongBEEP-Android app.
#
# Usage: ./build.sh [jobs]
#------------------------------------------------------------------
set -euo pipefail

export ANDROID_SDK_ROOT="$HOME/Library/Android/sdk"
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/26.3.11579264"
QT_ANDROID_ARM64="$HOME/Android/Qt/6.8.3/android_arm64_v8a"
QMAKE="$QT_ANDROID_ARM64/bin/qmake"

JOBS="${1:-8}"
HERE="$(cd "$(dirname "$0")" && pwd)"
DEPLOY_DIR="$HERE/../YongBEEP-Android/app/libs/arm64-v8a"

cd "$HERE"
mkdir -p build
cd build

"$QMAKE" "$HERE/ybcore.pro" -spec android-clang ANDROID_ABIS=arm64-v8a
make -j"$JOBS"

SO="libybcore_arm64-v8a.so"
if [ ! -f "$SO" ]; then
  echo "ERROR: $SO not produced" >&2
  exit 1
fi

mkdir -p "$DEPLOY_DIR"
cp "$SO" "$DEPLOY_DIR/libybcore.so"
echo "deployed: $DEPLOY_DIR/libybcore.so"
