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
#include <QTimer>

class QBluetoothServiceDiscoveryAgent;
class BtWinWorker;

#if !defined( Q_OS_WIN )
  #include <QtBluetooth/QBluetoothServiceDiscoveryAgent>
#endif


/*! \brief Periodic SDP discovery of the other YongBEEP instances
 *
 * Mirror of core/Broadcaster for the Bluetooth transport: every
 * BLUETOOTH_SCAN_INTERVAL_MS milliseconds an internal timer starts a classic
 * SDP scan filtered on our service UUID (a scan already in progress is never
 * restarted) and every service found is reported with btPeerFound(bluetooth
 * address, device name). The local device address is filtered out to prevent
 * self connections.
 *
 * On macOS the scan runs in a QBluetoothServiceDiscoveryAgent; on Windows it
 * is delegated to the native BtWinWorker (BluetoothFindFirstDevice inquiry
 * plus a per device WSALookupService SDP query in the worker thread,
 * docs/02-技术设计.md §2.6).
 */
class BtDiscoverer : public QObject
{
  Q_OBJECT

public:
  explicit BtDiscoverer( QObject* parent = Q_NULLPTR );

  void start();
  /*! YongBEEP: one-shot manual scan (macOS SDP crash guard: the periodic
   * timer is disabled there; the devices dialog triggers this instead). */
  void startSingleScan();
  void stop();
  inline bool isActive() const;

  void setLocalAddress( const QBluetoothAddress& local_address );

#if defined( Q_OS_WIN )
  /*! The native worker doing the actual scanning (owned by BtManager). */
  void setWinWorker( BtWinWorker* win_worker );
#endif

signals:
  void btPeerFound( const QBluetoothAddress& bt_address, const QString& device_name );
#if !defined( Q_OS_WIN )
  void discoveryError( QBluetoothServiceDiscoveryAgent::Error error );
#endif

private slots:
#if !defined( Q_OS_WIN )
  void onServiceDiscovered( const QBluetoothServiceInfo& service_info );
  void onDiscoveryFinished();
  void onDiscoveryError( QBluetoothServiceDiscoveryAgent::Error error );
#else
  void onWinPeerFound( quint64 bt_address, const QString& device_name );
  void onWinScanFinished();
#endif
  void startScan();

private:
#if !defined( Q_OS_WIN )
  QBluetoothServiceDiscoveryAgent* mp_agent;
#else
  BtWinWorker* mp_winWorker;
  bool m_scanRequested;
#endif
  QTimer* mp_scanTimer;
  QBluetoothAddress m_localAddress;

};


// Inline Functions
inline bool BtDiscoverer::isActive() const { return mp_scanTimer->isActive(); }

#endif // BEEBEEP_BTDISCOVERER_H
