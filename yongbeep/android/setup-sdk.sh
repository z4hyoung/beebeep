#!/bin/bash
# YongBEEP A0: Android 工具链安装（SDK/NDK 部分）
# 无头安装：commandline-tools + sdkmanager，无需 Android Studio
set -x
SDK="$HOME/Library/Android/sdk"
mkdir -p "$SDK"
cd "$SDK"

# 1) 下载 commandline-tools（官方 zip）
if [ ! -d cmdline-tools/latest ]; then
  curl -L -o cmdtools.zip "https://dl.google.com/android/repository/commandlinetools-mac-11076708_latest.zip"
  unzip -q cmdtools.zip -d cmdline-tools-tmp
  mkdir -p cmdline-tools
  mv cmdline-tools-tmp/cmdline-tools cmdline-tools/latest
  rm -rf cmdline-tools-tmp cmdtools.zip
fi
SDKMANAGER="$SDK/cmdline-tools/latest/bin/sdkmanager"

# 2) 接受许可
yes | "$SDKMANAGER" --licenses > /tmp/a0_licenses.log 2>&1

# 3) 安装核心包（platform-tools/平台/build-tools/NDK）
yes | "$SDKMANAGER" "platform-tools" "platforms;android-34" "build-tools;34.0.0" "ndk;26.3.11579264" > /tmp/a0_sdk_install.log 2>&1
echo "SDK_INSTALL_EXIT=$?"

# 4) 验证
ls "$SDK/ndk/" 2>/dev/null
"$SDK/platform-tools/adb" version 2>/dev/null | head -1
echo "A0_SDK_DONE"
