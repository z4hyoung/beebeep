//////////////////////////////////////////////////////////////////////
//
// YongBEEP — scan nearby devices dialog (S3-4b)
//
// Connection diagnostics and pairing help. LAN discovers peers automatically;
// Windows supports active Bluetooth discovery, while macOS listens passively.
// The device list/connect fallback is exposed only on Windows.
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_GUISCANDEVICES_H
#define BEEBEEP_GUISCANDEVICES_H

#include "Config.h"
#include <QtBluetooth/QBluetoothAddress>
#include <QDialog>
#include <QMap>

class QLabel;
class QListWidget;
class QPushButton;


class GuiScanDevices : public QDialog
{
  Q_OBJECT

public:
  explicit GuiScanDevices( QWidget* parent = Q_NULLPTR );
  void updateStatus();

private slots:
  void onBtPeerFound( const QBluetoothAddress& bt_address, const QString& device_name );
  void connectToDevice();
  void openSystemBluetoothSettings();

private:
  void rebuildDeviceList();

private:
  QLabel* mp_statusLabel;
  QListWidget* mp_deviceList;
  QPushButton* mp_pbConnect;
  QPushButton* mp_pbOpenSettings;
  QMap<QString, QString> m_devices; // bt address -> device name (dedup by address)

};

#endif // BEEBEEP_GUISCANDEVICES_H
