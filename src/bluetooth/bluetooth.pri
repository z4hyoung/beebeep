
INCLUDEPATH += $$PWD

HEADERS += bluetooth/BtManager.h \
  bluetooth/BtDiscoverer.h \
  bluetooth/BtListener.h \
  bluetooth/StreamDeviceBt.h

SOURCES += bluetooth/BtManager.cpp \
  bluetooth/BtDiscoverer.cpp \
  bluetooth/BtListener.cpp \
  bluetooth/StreamDeviceBt.cpp

# YongBEEP: MinGW builds of Qt ship QtBluetooth with a dummy backend, so the
# Windows transport talks to the native stack (AF_BTH + BluetoothAPIs).
win32 {
  HEADERS += bluetooth/BtWinWorker.h
  SOURCES += bluetooth/BtWinWorker.cpp
  LIBS += -lbthprops -lws2_32
}

macx {
  HEADERS += bluetooth/BtMacDiscovery.h
  OBJECTIVE_SOURCES += bluetooth/BtMacDiscovery.mm
  LIBS += -framework IOBluetooth
}
