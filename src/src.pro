
CONFIG(debug,debug|release) {
  message( Building BeeBEEP in Debug Mode )
  DEFINES += BEEBEEP_DEBUG
} else {
  message( Building BeeBEEP in Release Mode )
}

macx {
  CONFIG(debug,debug|release) {
    CONFIG -= app_bundle
    message( Building BeeBEEP without BUNDLE )
  } else {
    CONFIG += app_bundle
    message( Building BeeBEEP BUNDLE )
  }
}

greaterThan(QT_MAJOR_VERSION, 5) {
  message(Qt version: $$[QT_VERSION])
} else {
  greaterThan(QT_MAJOR_VERSION, 4):!lessThan(QT_MINOR_VERSION, 15) {
    message(Qt version: $$[QT_VERSION])
  } else {
    message(Qt $$[QT_VERSION] is NOT SUPPORTED - required 5.15.0+)
    error(Unablet to build BeeBEEP with this Qt $$[QT_VERSION])
  }
}

include(../beebeep.pri)

message( Target folder: $$DESTDIR )

TEMPLATE = app
#DEFINES += BEEBEEP_DISABLE_FILE_TRANSFER
#DEFINES += BEEBEEP_DISABLE_SEND_MESSAGE
#DEFINES += BEEBEEP_DISABLE_VIDEO_CALL
#DEFINES += BEEBEEP_USE_WEBENGINE

TARGET = yongbeep

QT += network xml widgets printsupport
# YongBEEP S4c: QtSvg for YongTheme svg icon rendering (no svg image plugin
# in the homebrew Qt build, icons render through QSvgRenderer directly).
QT += svg
# YongBEEP: classic bluetooth RFCOMM transport (docs/02-技术设计.md §2.6).
QT += bluetooth

win32|unix {
  QT += multimedia
  # YongBEEP: voice chat disabled for Qt6 (QAudioEncoderSettings/QAudioDeviceInfo removed
  # in Qt6 Multimedia); feature is on the removal list per docs/01-产品设计.md §3.4.
  # DEFINES += BEEBEEP_USE_VOICE_CHAT
}

unix:!macx:!android: {
  LIBS += -lxcb -lxcb-screensaver
  CONFIG += largefile
}

macx {
  QMAKE_LFLAGS += -F/System/Library/Frameworks/ApplicationServices.framework
  LIBS += -framework ApplicationServices
  QMAKE_INFO_PLIST = ../misc/Info.plist
}

win32 {
  DEFINES += _CRT_SECURE_NO_WARNINGS
  LIBS += -luser32
}

unix {
  contains(QMAKE_HOST.arch, armv7l* ): {
    DEFINES += BEEBEEP_FOR_RASPBERRY_PI
  }
}

message( Arch: $$QMAKE_HOST.arch )
message( Qt modules: $$QT )

include(../locale/locale.pri)
# YongBEEP: spell check disabled — QTextCodec-based SpellChecker requires Qt5Compat,
# feature is on the removal list per docs/01-产品设计.md §3.4.
# include(hunspell/hunspell.pri)
include(core/core.pri)
include(bluetooth/bluetooth.pri)
include(ecdh/ecdh.pri)
include(gui/gui.pri)
include(desktop/desktop.pri)
include(override/override.pri)
include(utils/utils.pri)
win32|unix {
  include(qxt/qxt.pri)
  # YongBEEP: desktop sharing + screenshot tool removed per PRD 3.4 cut list
  # (BEEBEEP_USE_SHAREDESKTOP no longer defined; all references are #ifdef-guarded).
  # include(sharedesktop/sharedesktop.pri)
  contains(DEFINES, BEEBEEP_USE_VOICE_CHAT) {
    include(voicechat/voicechat.pri)
  }
}

#!contains( DEFINES, BEEBEEP_DISABLE_VIDEO_CALL ) {
#  greaterThan( QT_MAJOR_VERSION, 4 ): {
#    greaterThan( QT_MINOR_VERSION, 4 ): {
#      message( Custom module added: videocall )
#      include(videocall/videocall.pri)
# } } } else {
#   message( Custom module disabled: videocall )
# }

HEADERS += Interfaces.h

RESOURCES += beebeep.qrc emojis.qrc
!contains(DEFINES, BEEBEEP_FOR_RASPBERRY_PI) {
  RESOURCES += emojis2x_1.qrc emojis2x_2.qrc
}

win32: RC_FILE = beebeep.rc
macx: ICON = beebeep.icns
macx: include(mdns/mdns.pri)

message( Config: $$CONFIG )
message( Libs: $$LIBS )
message( Defines: $$DEFINES )
message( Resources: $$RESOURCES )
macx: message( Info.plist: $$QMAKE_INFO_PLIST )
message( Object dir: $$OBJECTS_DIR )
unix: message( CXX flags: $$QMAKE_CXXFLAGS )
message( QT Plugins: $$QTPLUGIN )

unix:!macx:!android: {
  target.path = $$BINDIR
  INSTALLS += target
}
