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
#include <QtBluetooth/QBluetoothServiceInfo>
#include <QTimer>


BtDiscoverer::BtDiscoverer( QObject* parent )
  : QObject( parent ), mp_agent( new QBluetoothServiceDiscoveryAgent( this ) ),
    mp_scanTimer( new QTimer( this ) ), m_localAddress()
{
  // Classic SDP discovery: only the devices running our RFCOMM service are reported.
  mp_agent->setUuidFilter( StreamDeviceBt::serviceUuid() );

  connect( mp_agent, &QBluetoothServiceDiscoveryAgent::serviceDiscovered, this, &BtDiscoverer::onServiceDiscovered );
  connect( mp_agent, &QBluetoothServiceDiscoveryAgent::finished, this, &BtDiscoverer::onDiscoveryFinished );
  connect( mp_agent, &QBluetoothServiceDiscoveryAgent::errorOccurred, this, &BtDiscoverer::onDiscoveryError );

  mp_scanTimer->setInterval( BLUETOOTH_SCAN_INTERVAL_MS );
  connect( mp_scanTimer, &QTimer::timeout, this, &BtDiscoverer::startScan );
}

void BtDiscoverer::start()
{
  if( mp_scanTimer->isActive() )
    return;

  qDebug() << "BtDiscoverer starts scanning for service" << BLUETOOTH_SERVICE_UUID << "every" << BLUETOOTH_SCAN_INTERVAL_MS << "ms";
  mp_scanTimer->start();
  startScan();
}

void BtDiscoverer::stop()
{
  if( !mp_scanTimer->isActive() )
    return;

  mp_scanTimer->stop();
  if( mp_agent->isActive() )
    mp_agent->stop();
  qDebug() << "BtDiscoverer stopped";
}

void BtDiscoverer::setLocalAddress( const QBluetoothAddress& local_address )
{
  m_localAddress = local_address;
}

void BtDiscoverer::startScan()
{
  if( mp_agent->isActive() )
    return;

  // MinimalDiscovery: the uuid filter already restricts the SDP browse to our service.
  mp_agent->start( QBluetoothServiceDiscoveryAgent::MinimalDiscovery );
#ifdef BEEBEEP_DEBUG
  qDebug() << "BtDiscoverer scan started";
#endif
}

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
