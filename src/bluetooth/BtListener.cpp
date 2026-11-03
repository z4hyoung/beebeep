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

#include "BtListener.h"
#include "Config.h"
#include "StreamDeviceBt.h"
#include <QtBluetooth/QBluetoothServiceInfo>


BtListener::BtListener( QObject* parent )
  : QObject( parent ), mp_server( new QBluetoothServer( QBluetoothServiceInfo::RfcommProtocol, this ) )
{
  connect( mp_server, &QBluetoothServer::newConnection, this, &BtListener::onServerNewConnection );
}

bool BtListener::listen( const QString& service_name )
{
  if( mp_server->isListening() )
    return true;

  QString name_to_register = service_name.isEmpty() ? QString::fromLatin1( BLUETOOTH_SERVICE_NAME ) : service_name;

  // listen(uuid, name) picks a free RFCOMM channel and registers the SDP record.
  QBluetoothServiceInfo service_info = mp_server->listen( StreamDeviceBt::serviceUuid(), name_to_register );
  if( !service_info.isValid() )
  {
    qWarning() << "BtListener cannot listen on RFCOMM service" << name_to_register << "with error" << static_cast<int>( mp_server->error() );
    return false;
  }

  qDebug() << "BtListener is listening on RFCOMM channel" << mp_server->serverPort() << "for service" << name_to_register;
  return true;
}

void BtListener::close()
{
  if( mp_server->isListening() )
  {
    mp_server->close();
    qDebug() << "BtListener closed";
  }
}

void BtListener::onServerNewConnection()
{
  while( mp_server->hasPendingConnections() )
  {
    QBluetoothSocket* bt_socket = mp_server->nextPendingConnection();
    if( bt_socket )
    {
      qDebug() << "BtListener has a new pending connection from" << bt_socket->peerAddress().toString();
      emit newBtConnection( bt_socket );
    }
  }
}
