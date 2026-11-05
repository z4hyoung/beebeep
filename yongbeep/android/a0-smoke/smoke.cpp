// A0 冒烟：核心子集在 Android 工具链下可编译、QtBluetooth 可链接
#include <QCoreApplication>
#include <QDebug>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QBluetoothUuid>
#include <QBluetoothAddress>
#include <QBluetoothSocket>

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((visibility("default"))) const char* ybcore_smoke_info()
{
  static QByteArray info;
  if( info.isEmpty() )
  {
    info = QByteArrayLiteral("qt=") + qVersion() +
           QByteArrayLiteral(" proto=96 btuuid=") +
           QBluetoothUuid( QStringLiteral( "84be0454-3170-438d-8803-2a851b241fad" ) ).toString().toUtf8();
  }
  return info.constData();
}

#ifdef __cplusplus
}
#endif
