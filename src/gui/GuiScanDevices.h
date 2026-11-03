//////////////////////////////////////////////////////////////////////
//
// YongBEEP — scan nearby devices dialog (S3-4b)
//
// Lists the YongBEEP peers recently found by the bluetooth discovery
// (BtManager::btPeerFound, read-only signal reuse) with name, address and
// a connect button. The connect request goes through
// Core::connectToBluetoothPeer() which forwards to the very same
// Core::btPeerFound slot used by the automatic discovery path, so all
// duplicate-connection guards apply. Unpaired devices need the OS pairing
// first: the dialog shows per-platform guidance and a button that opens
// the system bluetooth settings (PRD F1 / 4.2).
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

private slots:
  void onBtPeerFound( const QBluetoothAddress& bt_address, const QString& device_name );
  void connectToDevice();
  void openSystemBluetoothSettings();

private:
  void updateStatus();
  void rebuildDeviceList();

private:
  QLabel* mp_statusLabel;
  QListWidget* mp_deviceList;
  QPushButton* mp_pbConnect;
  QPushButton* mp_pbOpenSettings;
  QMap<QString, QString> m_devices; // bt address -> device name (dedup by address)

};

#endif // BEEBEEP_GUISCANDEVICES_H
