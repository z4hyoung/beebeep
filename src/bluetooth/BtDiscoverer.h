//////////////////////////////////////////////////////////////////////
//
// YongBEEP is based on BeeBEEP Copyright (C) 2010-2026 Marco Mastroddi
//
// YongBEEP is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// YongBEEP is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_BTDISCOVERER_H
#define BEEBEEP_BTDISCOVERER_H

#include <QtBluetooth/QBluetoothAddress>
#include <QtBluetooth/QBluetoothServiceDiscoveryAgent>
#include <QTimer>


/*! \brief Periodic SDP discovery of the other YongBEEP instances
 *
 * Mirror of core/Broadcaster for the Bluetooth transport: a
 * QBluetoothServiceDiscoveryAgent (classic SDP, filtered on our service UUID)
 * is started every BLUETOOTH_SCAN_INTERVAL_MS milliseconds by an internal
 * timer (a scan already in progress is never restarted) and every service
 * found is reported with btPeerFound(bluetooth address, device name).
 * The local device address is filtered out to prevent self connections.
 */
class BtDiscoverer : public QObject
{
  Q_OBJECT

public:
  explicit BtDiscoverer( QObject* parent = Q_NULLPTR );

  void start();
  void stop();
  inline bool isActive() const;

  void setLocalAddress( const QBluetoothAddress& local_address );

signals:
  void btPeerFound( const QBluetoothAddress& bt_address, const QString& device_name );
  void discoveryError( QBluetoothServiceDiscoveryAgent::Error error );

private slots:
  void onServiceDiscovered( const QBluetoothServiceInfo& service_info );
  void onDiscoveryFinished();
  void onDiscoveryError( QBluetoothServiceDiscoveryAgent::Error error );
  void startScan();

private:
  QBluetoothServiceDiscoveryAgent* mp_agent;
  QTimer* mp_scanTimer;
  QBluetoothAddress m_localAddress;

};


// Inline Functions
inline bool BtDiscoverer::isActive() const { return mp_scanTimer->isActive(); }

#endif // BEEBEEP_BTDISCOVERER_H
