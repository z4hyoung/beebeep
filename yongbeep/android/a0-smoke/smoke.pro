# A0 冒烟工程：验证 Qt-for-Android 工具链能编出含 QtBluetooth 的核心库
# 构建（脚本会注入 ANDROID_SDK_ROOT/NDK 路径）：
#   ~/Android/Qt/6.8.3/android_arm64_v8a/bin/qmake smoke.pro -spec android-clang \
#     ANDROID_ABIS=arm64-v8a && make -j8 && make install
QT += core network bluetooth
CONFIG += c++17 console
TEMPLATE = lib
TARGET = ybcore-smoke
SOURCES += smoke.cpp
