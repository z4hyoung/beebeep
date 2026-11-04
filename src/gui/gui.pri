
INCLUDEPATH += $$PWD

# YongBEEP S4c: WeChat macOS theme module (palette/QSS/svg icon helpers)
HEADERS += gui/YongTheme.h
SOURCES += gui/YongTheme.cpp
# Hand-drawn SVG icon set (search/smile/paperclip/plus/bluetooth/file types)
RESOURCES += gui/icons/yongbeep-icons.qrc

HEADERS += gui/AudioManager.h \
  gui/Avatar.h \
  gui/ChatListItemDelegate.h \
  gui/ChatListModel.h \
  gui/ColorManager.h \
  gui/Emoticon.h \
  gui/EmoticonManager.h \
  gui/GuiMainWindow.h \
  gui/GuiScanDevices.h \
  gui/IconManager.h \
  gui/ImageOptimizer.h \
  gui/GuiSelectItems.h \
  gui/Screenshot.h \
  gui/ShortcutManager.h

SOURCES += gui/AudioManager.cpp \
  gui/Avatar.cpp \
  gui/ChatListItemDelegate.cpp \
  gui/ChatListModel.cpp \
  gui/ColorManager.cpp \
  gui/Emojis.cpp \
  gui/Emoticon.cpp \
  gui/EmoticonManager.cpp \
  gui/GuiMainWindow.cpp \
  gui/GuiScanDevices.cpp \
  gui/IconManager.cpp \
  gui/ImageOptimizer.cpp \
  gui/GuiSelectItems.cpp \
  gui/Screenshot.cpp \
  gui/ShortcutManager.cpp

FORMS += gui/GuiSelectItems.ui
