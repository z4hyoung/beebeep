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

#include "BtDiscoverer.h"
#include "Config.h"
#include "StreamDeviceBt.h"
#include "Core.h"
#include "User.h"
#include "UserManager.h"
#include "UserList.h"
#include <QList>
#include <QtBluetooth/QBluetoothServiceInfo>
#include <QTimer>

#if defined( Q_OS_WIN )
  #include "BtWinWorker.h"
  #include <QtCore/QMetaObject>
#endif


BtDiscoverer::BtDiscoverer( QObject* parent )
  : QObject( parent )
#if !defined( Q_OS_WIN )
  , mp_agent( new QBluetoothServiceDiscoveryAgent( this ) )
#else
  , mp_winWorker( Q_NULLPTR ), m_scanRequested( false )
#endif
  , mp_scanTimer( new QTimer( this ) ), m_localAddress()
{
#if !defined( Q_OS_WIN )
  // Classic SDP discovery: only the devices running our RFCOMM service are reported.
  mp_agent->setUuidFilter( StreamDeviceBt::serviceUuid() );

  connect( mp_agent, &QBluetoothServiceDiscoveryAgent::serviceDiscovered, this, &BtDiscoverer::onServiceDiscovered );
  connect( mp_agent, &QBluetoothServiceDiscoveryAgent::finished, this, &BtDiscoverer::onDiscoveryFinished );
  connect( mp_agent, &QBluetoothServiceDiscoveryAgent::errorOccurred, this, &BtDiscoverer::onDiscoveryError );
#endif

  mp_scanTimer->setInterval( BLUETOOTH_SCAN_INTERVAL_MS );
  connect( mp_scanTimer, &QTimer::timeout, this, &BtDiscoverer::startScan );
}

#if defined( Q_OS_WIN )
void BtDiscoverer::setWinWorker( BtWinWorker* win_worker )
{
  if( mp_winWorker )
    disconnect( mp_winWorker, Q_NULLPTR, this, Q_NULLPTR );

  mp_winWorker = win_worker;

  if( mp_winWorker )
  {
    connect( mp_winWorker, &BtWinWorker::peerFound, this, &BtDiscoverer::onWinPeerFound );
    connect( mp_winWorker, &BtWinWorker::scanFinished, this, &BtDiscoverer::onWinScanFinished );
  }
}
#endif

void BtDiscoverer::start()
{
  if( mp_scanTimer->isActive() )
    return;

#if defined( Q_OS_WIN )
  if( mp_winWorker == Q_NULLPTR )
    return;
#endif

  qDebug() << "BtDiscoverer starts scanning for service" << BLUETOOTH_SERVICE_UUID << "every" << BLUETOOTH_SCAN_INTERVAL_MS << "ms";
  mp_scanTimer->start();
  startScan();
}

void BtDiscoverer::stop()
{
  if( !mp_scanTimer->isActive() )
    return;

  mp_scanTimer->stop();
#if !defined( Q_OS_WIN )
  if( mp_agent->isActive() )
    mp_agent->stop();
#endif
  qDebug() << "BtDiscoverer stopped";
}

void BtDiscoverer::setLocalAddress( const QBluetoothAddress& local_address )
{
  m_localAddress = local_address;
}

void BtDiscoverer::startScan()
{
  // YongBEEP: pause SDP discovery while a peer is connected (this is a 1:1
  // messenger, there is nothing more to find) - SDP re-inquiries on macOS
  // risk the DarwinBTSDPInquiry teardown race that crashed the app (all
  // three crash reports of 2026-11-04/05 die in sdpQueryComplete)
  if( beeCore && beeCore->isConnected() )
  {
    const QList<User>& users = UserManager::instance().userList().toList();
    for( int i = 0; i < users.size(); ++i )
    {
      if( users.at( i ).isStatusConnected() )
        return;
    }
  }

#if !defined( Q_OS_WIN )
  if( mp_agent->isActive() )
    return;

  // MinimalDiscovery: the uuid filter already restricts the SDP browse to our service.
  mp_agent->start( QBluetoothServiceDiscoveryAgent::MinimalDiscovery );
#ifdef BEEBEEP_DEBUG
  qDebug() << "BtDiscoverer scan started";
#endif
#else
  // A cycle still running in the worker thread is skipped: the timer keeps
  // the cadence, the inquiry plus the SDP probes define the real duration.
  if( m_scanRequested || mp_winWorker == Q_NULLPTR )
    return;

  m_scanRequested = true;
  QMetaObject::invokeMethod( mp_winWorker, "scanForPeers", Qt::QueuedConnection,
                             Q_ARG( quint64, m_localAddress.toUInt64() ) );
#ifdef BEEBEEP_DEBUG
  qDebug() << "BtDiscoverer scan requested";
#endif
#endif
}

#if !defined( Q_OS_WIN )

void BtDiscoverer::onServiceDiscovered( const QBluetoothServiceInfo& service_info )
{
  QBluetoothAddress bt_address = service_info.device().address();

  // Self connection filter: skip the service record of the local adapter.
  if( !m_localAddress.isNull() && bt_address == m_localAddress )
  {
#ifdef BEEBEEP_DEBUG
    qDebug() << "BtDiscoverer skips its own service record";
#endif
    return;
  }

  QString device_name = service_info.device().name();
  qDebug() << "BtDiscoverer has found service" << service_info.serviceName() << "of device" << device_name
           << "(" << bt_address.toString() << ") on channel" << service_info.serverChannel();

  emit btPeerFound( bt_address, device_name );
}

void BtDiscoverer::onDiscoveryFinished()
{
#ifdef BEEBEEP_DEBUG
  qDebug() << "BtDiscoverer scan finished:" << mp_agent->discoveredServices().size() << "services found";
#endif
}

void BtDiscoverer::onDiscoveryError( QBluetoothServiceDiscoveryAgent::Error error )
{
  if( error == QBluetoothServiceDiscoveryAgent::PoweredOffError )
    qDebug() << "BtDiscoverer cannot scan because the bluetooth adapter is powered off";
  else
    qWarning() << "BtDiscoverer error" << static_cast<int>( error ) << ":" << mp_agent->errorString();

  emit discoveryError( error );
}

#else

void BtDiscoverer::onWinPeerFound( quint64 bt_address, const QString& device_name )
{
  // Self connection filter: skip the service record of the local adapter.
  if( !m_localAddress.isNull() && bt_address == m_localAddress.toUInt64() )
    return;

  qDebug() << "BtDiscoverer has found the service of device" << device_name << "(" << Qt::hex << bt_address << ")";

  emit btPeerFound( QBluetoothAddress( bt_address ), device_name );
}

void BtDiscoverer::onWinScanFinished()
{
  m_scanRequested = false;
}

#endif
