#!/bin/bash
# YongBEEP A0: Qt for Android 安装（aqtinstall，无需 Qt 账号）
set -x
QTA="$HOME/Android/Qt"

# 1) 安装 aqtinstall
pip3 install --quiet --user aqtinstall 2>/dev/null || pip3 install --quiet --break-system-packages --user aqtinstall
export PATH="$PATH:$HOME/Library/Python/3.9/bin:$HOME/.local/bin"
which aqt || python3 -m aqt --version

# 2) 查可用 Android 版本（取最新 6.x LTS 线）
aqt list-qt mac android --arch 6.8.3 2>/dev/null || aqt list-qt mac android 2>/dev/null | tail -5 > /tmp/a0_qt_versions.txt

# 3) 安装 Qt 6.8.3 for Android（arm64-v8a 真机优先；x86_64 供模拟器，后续按需）
mkdir -p "$QTA"
aqt install-qt mac android 6.8.3 android_arm64_v8a -O "$QTA" > /tmp/a0_qt_install.log 2>&1
echo "QT_ARM64_EXIT=$?"
aqt install-qt mac android 6.8.3 android_x86_64 -O "$QTA" >> /tmp/a0_qt_install.log 2>&1
echo "QT_X86_EXIT=$?"

# 4) 验证
ls "$QTA/6.8.3/" 2>/dev/null
"$QTA/6.8.3/android_arm64_v8a/bin/qmake" -query QT_VERSION 2>/dev/null
echo "A0_QT_DONE"
