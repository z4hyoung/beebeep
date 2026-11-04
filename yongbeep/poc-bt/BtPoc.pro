######################################################################
# YongBEEP M1 蓝牙 RFCOMM PoC
# 目的：验证 Qt Bluetooth 在 macOS(25.6) 与 Windows(WinRT 后端) 之间
#       经典 RFCOMM 连接/发现/吞吐是否满足 go/no-go 标准 (≥100KB/s)
# 构建：qmake BtPoc.pro && make（macOS 用 /opt/homebrew/opt/qt/bin/qmake）
######################################################################
QT += core gui widgets bluetooth
CONFIG += c++17 console
TARGET = BtPoc
TEMPLATE = app

SOURCES += main.cpp
