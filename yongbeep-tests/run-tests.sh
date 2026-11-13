#!/bin/bash
# YongBEEP 核心单元测试：链接 m0-qt6 构建产物（除 Main.o 全量——依赖网太深，不实例化 GUI 无副作用）
set -e
cd "$(dirname "$0")"
BUILD=../../build/m0-qt6/src
TESTBIN=/tmp/yb_core_tests

# 1) 编译测试 main
clang++ -c -std=gnu++17 -pipe -O0 \
  -DBEEBEEP_USE_QXT -DBEEBEEP_USE_MULTICAST_DNS \
  -I../src -I../src/core -I../src/bluetooth -I../src/ecdh -I../src/gui -I../src/desktop -I../src/utils -I../locale \
  -I/opt/homebrew/include -I/opt/homebrew/include/QtCore -I/opt/homebrew/include/QtNetwork \
  -I/opt/homebrew/include/QtGui -I/opt/homebrew/include/QtSvg \
  -I/opt/homebrew/include/QtWidgets -I/opt/homebrew/include/QtXml \
  -I/opt/homebrew/include/QtMultimedia -I/opt/homebrew/include/QtPrintSupport \
  -F/opt/homebrew/lib \
  -o /tmp/yb_test_main.o main.cpp

# 2) 收集对象（除 Main.o 和已禁用功能的陈旧产物——桌面共享家族）
ALL_OBJS=$(ls "$BUILD"/*.o | grep -v -E '/Main\.o$|ShareDesktop' | tr '\n' ' ')
echo "linking $(echo $ALL_OBJS | wc -w | tr -d ' ') objects"

# 3) 链接
QT_LIBS="-F/opt/homebrew/lib -framework QtCore -framework QtNetwork -framework QtGui -framework QtSvg -framework QtWidgets -framework QtXml -framework QtMultimedia -framework QtPrintSupport \
-framework Carbon -framework Foundation -framework AppKit -framework ApplicationServices -framework CoreFoundation \
-framework IOKit -framework IOBluetooth -framework DiskArbitration -framework UniformTypeIdentifiers -framework OpenGL \
-framework QtBluetooth -framework AVFoundation -framework CoreBluetooth \
/opt/homebrew/share/qt/plugins/permissions/libqdarwinmicrophonepermission.a \
/opt/homebrew/share/qt/plugins/permissions/libqdarwinbluetoothpermission.a"
clang++ -o "$TESTBIN" /tmp/yb_test_main.o $ALL_OBJS $QT_LIBS -Wl,-rpath,/opt/homebrew/lib 2>/tmp/yb_test_link.log || { echo LINK_FAIL; grep "referenced from" /tmp/yb_test_link.log | head -8; exit 1; }

# 4) 运行
"$TESTBIN" "$@"
