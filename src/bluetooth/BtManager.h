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

#ifndef BEEBEEP_BTMANAGER_H
#define BEEBEEP_BTMANAGER_H

#include <QtBluetooth/QBluetoothAddress>
#include <QObject>

class BtDiscoverer;
class BtListener;
class QBluetoothSocket;
class BtWinWorker;
class QThread;


/*! \brief Bluetooth transport controller: permission, listener and discoverer
 *
 * Composes BtListener and BtDiscoverer (docs/02-技术设计.md §2.2) and owns the
 * QBluetoothPermission gate: on macOS the first start() shows the system
 * authorization dialog and the components are started in the asynchronous
 * callback; on platforms without a permission concept checkPermission() is
 * always granted and the start is synchronous. If the permission is denied
 * (or the adapter is off) the manager degrades to inactive and only logs:
 * the TCP transport is never affected.
 */
class BtManager : public QObject
{
  Q_OBJECT

public:
  explicit BtManager( QObject* parent = Q_NULLPTR );
  ~BtManager() override;

  bool start();
  void stop();
  inline bool isActive() const;

  /*! Address of the local adapter (invalid if not exposed by the platform). */
  inline const QBluetoothAddress& localAddress() const;

  /*! RFCOMM channel of our listening service (0 if not listening). */
  quint16 listenerPort() const;

  /*! YongBEEP: one-shot search; native SDP queries on macOS, native discovery worker on Windows. */
  void triggerManualScan();
  void setFileTransferActive( bool active );

signals:
  void manualSearchStatusChanged( const QString& );
  void manualSearchFinished();
  void btPeerFound( const QBluetoothAddress& bt_address, const QString& device_name );
  void newBtConnection( QObject* bt_socket );
  void bluetoothPermissionDenied();

private:
  void startComponents();
#if defined( Q_OS_WIN )
  void onWinLocalAddress( quint64 bt_address );
#endif

  BtListener* mp_listener;
  BtDiscoverer* mp_discoverer;
  QBluetoothAddress m_localAddress;
  bool m_active;
  bool m_stopRequested;
#if defined( Q_OS_WIN )
  /*! Native win32 stack worker thread (MinGW Qt builds have no bluetooth backend). */
  QThread* mp_winThread;
  BtWinWorker* mp_winWorker;
#endif

};


// Inline Functions
inline bool BtManager::isActive() const { return m_active; }
inline const QBluetoothAddress& BtManager::localAddress() const { return m_localAddress; }

#endif // BEEBEEP_BTMANAGER_H
