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

#if !defined( Q_OS_WIN )
  #include <QtBluetooth/QBluetoothServiceInfo>
#else
  #include "BtWinWorker.h"
  #include <QtCore/QMetaObject>
#endif


BtListener::BtListener( QObject* parent )
  : QObject( parent )
#if !defined( Q_OS_WIN )
  , mp_server( new QBluetoothServer( QBluetoothServiceInfo::RfcommProtocol, this ) )
#else
  , mp_winWorker( Q_NULLPTR ), m_listening( false ), m_channel( 0 )
#endif
{
#if !defined( Q_OS_WIN )
  connect( mp_server, &QBluetoothServer::newConnection, this, &BtListener::onServerNewConnection );
#endif
}

BtListener::~BtListener()
{
#if !defined( Q_OS_WIN )
  close();
#endif
}

#if defined( Q_OS_WIN )
void BtListener::setWinWorker( BtWinWorker* win_worker )
{
  if( mp_winWorker )
    disconnect( mp_winWorker, Q_NULLPTR, this, Q_NULLPTR );

  mp_winWorker = win_worker;

  if( mp_winWorker )
  {
    connect( mp_winWorker, &BtWinWorker::incomingConnection, this, &BtListener::onWinIncomingConnection );
    connect( mp_winWorker, &BtWinWorker::serverStarted, this, &BtListener::onWinServerStarted );
    connect( mp_winWorker, &BtWinWorker::serverFailed, this, &BtListener::onWinServerFailed );
  }
}
#endif

bool BtListener::listen( const QString& service_name )
{
  if( isListening() )
    return true;

  QString name_to_register = service_name.isEmpty() ? QString::fromLatin1( BLUETOOTH_SERVICE_NAME ) : service_name;

#if !defined( Q_OS_WIN )
  // listen(uuid, name) picks a free RFCOMM channel and registers the SDP record.
  // Keep the native SDP record alive for the listener's whole lifetime and
  // unregister it before closing. macOS binds its real channel to this record.
  m_serviceInfo = mp_server->listen( StreamDeviceBt::serviceUuid(), name_to_register );
  if( !m_serviceInfo.isRegistered() )
  {
    qWarning() << "BtListener cannot listen on RFCOMM service" << name_to_register << "with error" << static_cast<int>( mp_server->error() );
    return false;
  }

  qDebug() << "BtListener is listening on RFCOMM channel" << mp_server->serverPort() << "for service" << name_to_register;
  return true;
#else
  if( mp_winWorker == Q_NULLPTR )
  {
    qWarning() << "BtListener has no native worker: the bluetooth transport is unavailable";
    return false;
  }

  // The socket work happens in the worker thread: serverStarted() completes
  // the handshake and updates isListening()/serverPort().
  QMetaObject::invokeMethod( mp_winWorker, "startServer", Qt::QueuedConnection, Q_ARG( QString, name_to_register ) );
  return true;
#endif
}

void BtListener::close()
{
#if !defined( Q_OS_WIN )
  if( m_serviceInfo.isRegistered() )
    m_serviceInfo.unregisterService();
  m_serviceInfo = QBluetoothServiceInfo();
  if( mp_server->isListening() )
  {
    mp_server->close();
    qDebug() << "BtListener closed";
  }
#else
  if( mp_winWorker && m_listening )
    QMetaObject::invokeMethod( mp_winWorker, "stopServer", Qt::QueuedConnection );
  m_listening = false;
  m_channel = 0;
#endif
}

#if !defined( Q_OS_WIN )
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
#else
void BtListener::onWinIncomingConnection( quint64 peer_address, quintptr socket_handle )
{
  // The socket object must live on the GUI thread (QSocketNotifier based
  // reads), so it is created here and only the native handle crosses over.
  NativeBtSocket* bt_socket = new NativeBtSocket( this );
  if( !bt_socket->adoptConnectedSocket( static_cast<qintptr>( socket_handle ), peer_address ) )
  {
    bt_socket->deleteLater();
    return;
  }

  qDebug() << "BtListener has a new pending connection from" << Qt::hex << peer_address;
  emit newBtConnection( bt_socket );
}

void BtListener::onWinServerStarted( quint16 channel )
{
  m_listening = true;
  m_channel = channel;
  qDebug() << "BtListener is listening on RFCOMM channel" << channel;
}

void BtListener::onWinServerFailed( const QString& message )
{
  qWarning() << "BtListener cannot listen on RFCOMM service:" << message;
}
#endif
