#------------------------------------------------------------------
# ybcore.pro - BeeBEEP/YongBEEP C++ core (protocol/crypto/transfer/
# discovery/files, NO widgets GUI) as an Android shared library.
#
# Build (see build.sh):
#   ~/Android/Qt/6.8.3/android_arm64_v8a/bin/qmake ybcore.pro \
#     -spec android-clang ANDROID_ABIS=arm64-v8a && make -j8
#
# Rules of engagement (A1.2):
#   * source/src is NEVER modified - desktop/GUI-only dependencies are
#     satisfied by headers in ./stubs, which is FIRST in INCLUDEPATH.
#   * QtWidgets / QtMultimedia / QtPrintSupport are NOT linked. The
#     umbrella includes in src/core/Config.h resolve to empty stub files.
#   * gui/ is compiled only for the classes the core actually references.
#------------------------------------------------------------------

TEMPLATE = lib
TARGET   = ybcore
CONFIG  += c++17 shared
QT      += core gui network bluetooth
# widgets must NEVER be added here.

# --- stubs first: AudioManager.h / BeeApplication.h / GuiChatMessage.h /
#     Updater.h / GAnalytics.h / QApplication / QtWidgets / QtMultimedia /
#     QtPrintSupport all resolve from here before the real source tree.
INCLUDEPATH += $$PWD/stubs

SRC = $$clean_path($$PWD/../../../src)
INCLUDEPATH += $$SRC \
               $$SRC/core \
               $$SRC/utils \
               $$SRC/bluetooth \
               $$SRC/ecdh \
               $$SRC/gui
# NOTE: $$SRC/desktop and $$SRC/mdns are intentionally NOT in the path.

#------------------------------------------------------------------
# core/ (full list mirrors src/core/core.pri; win32-only and the
# disabled CoreShareDesktop/CoreVideoCall/CoreVoiceChat are excluded)
#------------------------------------------------------------------
HEADERS += $$SRC/core/Broadcaster.h \
  $$SRC/core/BuildFileList.h \
  $$SRC/core/BuildFileShareList.h \
  $$SRC/core/BuildSavedChatList.h \
  $$SRC/core/Chat.h \
  $$SRC/core/ChatManager.h \
  $$SRC/core/ChatMessage.h \
  $$SRC/core/ChatMessageData.h \
  $$SRC/core/ChatRecord.h \
  $$SRC/core/Config.h \
  $$SRC/core/Connection.h \
  $$SRC/core/ConnectionSocket.h \
  $$SRC/core/Core.h \
  $$SRC/core/FileInfo.h \
  $$SRC/core/FileShare.h \
  $$SRC/core/FileTransfer.h \
  $$SRC/core/FileTransferPeer.h \
  $$SRC/core/InlineFileTransferPeer.h \
  $$SRC/core/FirewallManager.h \
  $$SRC/core/Group.h \
  $$SRC/core/HistoryManager.h \
  $$SRC/core/HistoryMessage.h \
  $$SRC/core/Hive.h \
  $$SRC/core/Job.h \
  $$SRC/core/Log.h \
  $$SRC/core/Listener.h \
  $$SRC/core/Message.h \
  $$SRC/core/MessageManager.h \
  $$SRC/core/MessageRecord.h \
  $$SRC/core/NetworkAddress.h \
  $$SRC/core/NetworkEntry.h \
  $$SRC/core/NetworkManager.h \
  $$SRC/core/NetworkMonitor.h \
  $$SRC/core/PluginManager.h \
  $$SRC/core/Protocol.h \
  $$SRC/core/Random.h \
  $$SRC/core/Rijndael.h \
  $$SRC/core/SaveChatList.h \
  $$SRC/core/Settings.h \
  $$SRC/core/StreamDevice.h \
  $$SRC/core/StreamDeviceTcp.h \
  $$SRC/core/TickManager.h \
  $$SRC/core/User.h \
  $$SRC/core/UserList.h \
  $$SRC/core/UserManager.h \
  $$SRC/core/UserRecord.h \
  $$SRC/core/UserStatusRecord.h \
  $$SRC/core/VCard.h \
  $$SRC/core/Version.h

SOURCES += $$SRC/core/Broadcaster.cpp \
  $$SRC/core/BuildFileList.cpp \
  $$SRC/core/BuildFileShareList.cpp \
  $$SRC/core/BuildSavedChatList.cpp \
  $$SRC/core/Chat.cpp \
  $$SRC/core/ChatManager.cpp \
  $$SRC/core/ChatMessage.cpp \
  $$SRC/core/ChatMessageData.cpp \
  $$SRC/core/ChatRecord.cpp \
  $$SRC/core/Connection.cpp \
  $$SRC/core/ConnectionSocket.cpp \
  $$SRC/core/Core.cpp \
  $$SRC/core/CoreChat.cpp \
  $$SRC/core/CoreConnection.cpp \
  $$SRC/core/CoreDispatcher.cpp \
  $$SRC/core/CoreFileTransfer.cpp \
  $$SRC/core/CoreParser.cpp \
  $$SRC/core/CoreUser.cpp \
  $$SRC/core/FileInfo.cpp \
  $$SRC/core/FileShare.cpp \
  $$SRC/core/FileTransfer.cpp \
  $$SRC/core/FileTransferDownload.cpp \
  $$SRC/core/FileTransferPeer.cpp \
  $$SRC/core/FileTransferUpload.cpp \
  $$SRC/core/InlineFileTransferPeer.cpp \
  $$SRC/core/FirewallManager.cpp \
  $$SRC/core/Group.cpp \
  $$SRC/core/HistoryManager.cpp \
  $$SRC/core/HistoryMessage.cpp \
  $$SRC/core/Hive.cpp \
  $$SRC/core/Listener.cpp \
  $$SRC/core/Log.cpp \
  $$SRC/core/Message.cpp \
  $$SRC/core/MessageManager.cpp \
  $$SRC/core/MessageRecord.cpp \
  $$SRC/core/NetworkAddress.cpp \
  $$SRC/core/NetworkEntry.cpp \
  $$SRC/core/NetworkManager.cpp \
  $$SRC/core/NetworkMonitor.cpp \
  $$SRC/core/PluginManager.cpp \
  $$SRC/core/Protocol.cpp \
  $$SRC/core/Rijndael.cpp \
  $$SRC/core/SaveChatList.cpp \
  $$SRC/core/Settings.cpp \
  $$SRC/core/StreamDeviceTcp.cpp \
  $$SRC/core/TickManager.cpp \
  $$SRC/core/User.cpp \
  $$SRC/core/UserList.cpp \
  $$SRC/core/UserManager.cpp \
  $$SRC/core/UserRecord.cpp \
  $$SRC/core/UserStatusRecord.cpp \
  $$SRC/core/VCard.cpp

#------------------------------------------------------------------
# ecdh/ + utils/ (BeeUtils only; GAnalytics/HttpDownloader/Updater are
# stubbed - see stubs/Updater.h and stubs/GAnalytics.h)
#------------------------------------------------------------------
HEADERS += $$SRC/ecdh/ECDH.h \
  $$SRC/utils/BeeUtils.h
SOURCES += $$SRC/ecdh/ECDH.cpp \
  $$SRC/utils/BeeUtils.cpp

#------------------------------------------------------------------
# bluetooth/ (BtWinWorker.cpp is fully #if defined(Q_OS_WIN)-guarded,
# it compiles to an empty unit on Android and keeps MinGW builds intact)
#------------------------------------------------------------------
HEADERS += $$SRC/bluetooth/BtManager.h \
  $$SRC/bluetooth/BtDiscoverer.h \
  $$SRC/bluetooth/BtListener.h \
  $$SRC/bluetooth/StreamDeviceBt.h \
  $$SRC/bluetooth/BtWinWorker.h
SOURCES += $$SRC/bluetooth/BtManager.cpp \
  $$SRC/bluetooth/BtDiscoverer.cpp \
  $$SRC/bluetooth/BtListener.cpp \
  $$SRC/bluetooth/StreamDeviceBt.cpp \
  $$SRC/bluetooth/BtWinWorker.cpp

#------------------------------------------------------------------
# gui/ minimal set referenced by the core (icons/emoticons/colors/avatar)
#------------------------------------------------------------------
HEADERS += $$SRC/gui/IconManager.h \
  $$SRC/gui/EmoticonManager.h \
  $$SRC/gui/Emoticon.h \
  $$SRC/gui/ColorManager.h \
  $$SRC/gui/Avatar.h
SOURCES += $$SRC/gui/IconManager.cpp \
  $$SRC/gui/EmoticonManager.cpp \
  $$SRC/gui/Emoticon.cpp \
  $$SRC/gui/Emojis.cpp \
  $$SRC/gui/ColorManager.cpp \
  $$SRC/gui/Avatar.cpp

#------------------------------------------------------------------
# stubs with Q_OBJECT (moc must run on them)
#------------------------------------------------------------------
HEADERS += stubs/AudioManager.h \
  stubs/BeeApplication.h \
  stubs/GuiChatMessage.h \
  stubs/Updater.h \
  stubs/GAnalytics.h
SOURCES += stubs/BeeApplication_stub.cpp \
  ybcore_jni_entry.cpp \
  ybcore_jni.cpp

#------------------------------------------------------------------
# resources: BeeBEEP icons + emoticons, emoji set, YongBEEP svg icons
#------------------------------------------------------------------
RESOURCES += $$SRC/beebeep.qrc \
  $$SRC/emojis.qrc \
  $$SRC/gui/icons/yongbeep-icons.qrc
