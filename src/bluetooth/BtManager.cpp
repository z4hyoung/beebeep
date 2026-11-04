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

#include "BtManager.h"
#include "BtDiscoverer.h"
#include "BtListener.h"
#include "Config.h"
#include "Settings.h"
#include <QCoreApplication>
#if QT_CONFIG(permissions)
# include <QPermissions>
#endif
#include <QtBluetooth/QBluetoothLocalDevice>
#include <QtBluetooth/QBluetoothSocket>

#if defined( Q_OS_WIN )
  #include "BtWinWorker.h"
  #include <QtCore/QMetaObject>
  #include <QtCore/QThread>
#endif


BtManager::BtManager( QObject* parent )
  : QObject( parent ), mp_listener( new BtListener( this ) ), mp_discoverer( new BtDiscoverer( this ) ),
    m_localAddress(), m_active( false ), m_stopRequested( false )
#if defined( Q_OS_WIN )
  , mp_winThread( Q_NULLPTR ), mp_winWorker( Q_NULLPTR )
#endif
{
  connect( mp_discoverer, &BtDiscoverer::btPeerFound, this, &BtManager::btPeerFound );
  connect( mp_listener, &BtListener::newBtConnection, this, &BtManager::newBtConnection );

#if defined( Q_OS_WIN )
  // MinGW builds of Qt ship QtBluetooth with a dummy backend only, so the
  // whole transport runs on the native win32 stack in a worker thread.
  mp_winThread = new QThread( this );
  mp_winWorker = new BtWinWorker();
  mp_winWorker->moveToThread( mp_winThread );
  BtWinWorker::setInstance( mp_winWorker );

  connect( mp_winThread, &QThread::finished, mp_winWorker, &QObject::deleteLater );
  connect( mp_winWorker, &BtWinWorker::localAddressReady, this, &BtManager::onWinLocalAddress );

  mp_listener->setWinWorker( mp_winWorker );
  mp_discoverer->setWinWorker( mp_winWorker );

  mp_winThread->start();
  QMetaObject::invokeMethod( mp_winWorker, "initialize", Qt::QueuedConnection );
#endif
}

BtManager::~BtManager()
{
#if defined( Q_OS_WIN )
  if( mp_winThread )
  {
    if( mp_winWorker )
      QMetaObject::invokeMethod( mp_winWorker, "shutdown", Qt::BlockingQueuedConnection );
    mp_winThread->quit();
    mp_winThread->wait();
    BtWinWorker::setInstance( Q_NULLPTR );
  }
#endif
}

#if defined( Q_OS_WIN )
void BtManager::onWinLocalAddress( quint64 bt_address )
{
  m_localAddress = QBluetoothAddress( bt_address );
  mp_discoverer->setLocalAddress( m_localAddress );
}
#endif

bool BtManager::start()
{
  if( m_active )
    return true;

  m_stopRequested = false;

  if( !Settings::instance().useBluetooth() )
  {
    qDebug() << "BtManager is disabled by settings (UseBluetooth = false)";
    return false;
  }

#if QT_CONFIG(permissions)
  // Qt 6.5+ permission gate (docs/02-技术设计.md §2.6). On macOS the request
  // shows the system dialog on first run; on platforms without a permission
  // concept checkPermission() is granted and the flow is synchronous.
  QBluetoothPermission bt_permission;
  bt_permission.setCommunicationModes( QBluetoothPermission::Access );
  const Qt::PermissionStatus permission_status = qApp->checkPermission( bt_permission );

  if( permission_status == Qt::PermissionStatus::Granted )
  {
    startComponents();
    return true;
  }

  qDebug() << "BtManager requests the bluetooth permission";
  qApp->requestPermission( bt_permission, this, [this]( const QPermission& permission )
  {
    // The request is asynchronous: Core may have been stopped meanwhile.
    if( m_stopRequested )
    {
      qDebug() << "BtManager discards the bluetooth permission callback because a stop was requested";
      return;
    }

    if( permission.status() == Qt::PermissionStatus::Granted )
    {
      qDebug() << "BtManager has obtained the bluetooth permission";
      startComponents();
    }
    else
    {
#ifdef Q_OS_MACOS
      // YongBEEP: Qt's QBluetoothPermission maps to the CoreBluetooth (BLE)
      // authorization on macOS, while our classic RFCOMM transport runs on
      // IOBluetooth, which is NOT gated by that permission. Verified live:
      // classic inquiry scan and RFCOMM server work regardless of this status.
      // Treat the denial as non-fatal and run the classic transport anyway.
      qWarning() << "BtManager bluetooth permission denied for BLE: continuing with the classic (IOBluetooth) transport";
      startComponents();
#else
      qWarning() << "BtManager bluetooth permission denied: the bluetooth transport stays disabled (degraded mode, TCP only)";
      emit bluetoothPermissionDenied();
#endif
    }
  } );

  // Started asynchronously: the components will run as soon as the user grants the permission.
  return true;
#else
  // Windows/Linux Qt builds ship without the permissions feature: there is no
  // OS-level bluetooth permission to request, so the transport starts directly.
  startComponents();
  return true;
#endif
}

void BtManager::startComponents()
{
  if( m_active )
    return;

#if !defined( Q_OS_WIN )
  // Reading the local adapter may itself initialize the bluetooth stack, so it
  // happens only after the permission has been granted. Some platforms do not
  // expose the local address at all: it stays invalid and the self connection
  // filter of BtDiscoverer/Core simply degrades to a no-op.
  QBluetoothLocalDevice local_device;
  m_localAddress = local_device.address();
#endif
  // On Windows the address arrives asynchronously from BtWinWorker
  // (onWinLocalAddress): if the radio is not ready yet it stays invalid and
  // the self connection filter degrades to a no-op, exactly like above.
  qDebug() << "BtManager starts on local adapter" << (!m_localAddress.isNull() ? m_localAddress.toString() : QString( "(unknown address)" ));

  QString service_name = Settings::instance().bluetoothDeviceName();
  if( service_name.isEmpty() )
    service_name = QString::fromLatin1( BLUETOOTH_SERVICE_NAME );

  if( !mp_listener->listen( service_name ) )
    qWarning() << "BtManager cannot listen: incoming bluetooth connections are disabled (outgoing ones still active)";

  mp_discoverer->setLocalAddress( m_localAddress );
  mp_discoverer->start();

  m_active = true;
  qDebug() << "BtManager is active";
}

void BtManager::stop()
{
  m_stopRequested = true;

  if( !m_active )
    return;

  mp_discoverer->stop();
  mp_listener->close();
  m_active = false;
  qDebug() << "BtManager stopped";
}

quint16 BtManager::listenerPort() const
{
  return mp_listener->isListening() ? mp_listener->serverPort() : 0;
}
