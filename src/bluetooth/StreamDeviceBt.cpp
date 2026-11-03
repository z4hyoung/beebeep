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

#include "StreamDeviceBt.h"
#include "Config.h"
#include <QtBluetooth/QBluetoothServiceInfo>


QBluetoothUuid StreamDeviceBt::serviceUuid()
{
  return QBluetoothUuid( QString::fromLatin1( BLUETOOTH_SERVICE_UUID ) );
}

QHostAddress StreamDeviceBt::syntheticAddressFromBtAddress( const QBluetoothAddress& bt_address )
{
  quint64 mac = bt_address.toUInt64();
  if( mac == 0 )
    return QHostAddress();

  quint8 mac_bytes[6];
  for( int i = 0; i < 6; i++ )
    mac_bytes[i] = static_cast<quint8>( (mac >> ((5 - i) * 8)) & 0xFF );

  Q_IPV6ADDR ipv6_bytes;
  ipv6_bytes[0] = 0xFE;
  ipv6_bytes[1] = 0x80;
  for( int i = 2; i < 8; i++ )
    ipv6_bytes[i] = 0;
  ipv6_bytes[8] = static_cast<quint8>( mac_bytes[0] ^ 0x02 ); // flip the U/L bit
  ipv6_bytes[9] = mac_bytes[1];
  ipv6_bytes[10] = mac_bytes[2];
  ipv6_bytes[11] = 0xFF; // modified EUI-64: FF:FE inserted in the middle
  ipv6_bytes[12] = 0xFE;
  ipv6_bytes[13] = mac_bytes[3];
  ipv6_bytes[14] = mac_bytes[4];
  ipv6_bytes[15] = mac_bytes[5];

  return QHostAddress( ipv6_bytes );
}

StreamDeviceBt::StreamDeviceBt( QObject* parent )
  : StreamDevice( parent ), mp_socket( new QBluetoothSocket( QBluetoothServiceInfo::RfcommProtocol, this ) )
{
  connectSocketSignals();
}

void StreamDeviceBt::connectSocketSignals()
{
  connect( mp_socket, &QBluetoothSocket::connected, this, &StreamDeviceBt::connected );
  connect( mp_socket, &QBluetoothSocket::readyRead, this, &StreamDeviceBt::readyRead );
  connect( mp_socket, &QBluetoothSocket::bytesWritten, this, &StreamDeviceBt::bytesWritten );
  connect( mp_socket, &QBluetoothSocket::errorOccurred, this, &StreamDeviceBt::onSocketError );
  connect( mp_socket, &QBluetoothSocket::disconnected, this, &StreamDeviceBt::disconnected );
}

void StreamDeviceBt::connectToService( const QBluetoothAddress& bt_address )
{
  // QBluetoothSocket resolves the RFCOMM channel of our service UUID with an
  // SDP query (ServiceLookupState) and then connects; both phases are covered
  // by the relaxed connection timeout of ConnectionSocket.
  mp_socket->connectToService( bt_address, serviceUuid() );
}

void StreamDeviceBt::connectToHost( const QHostAddress&, quint16 )
{
  qWarning() << "StreamDeviceBt cannot connect to a host address: use connectToService() with a bluetooth address";
}

bool StreamDeviceBt::setSocketDescriptor( qintptr )
{
  qWarning() << "StreamDeviceBt does not support socket descriptors: use adoptSocket()";
  return false;
}

void StreamDeviceBt::adoptSocket( QObject* socket_object )
{
  QBluetoothSocket* bt_socket = qobject_cast<QBluetoothSocket*>( socket_object );
  if( bt_socket == Q_NULLPTR )
  {
    qWarning() << "StreamDeviceBt cannot adopt an object that is not a QBluetoothSocket";
    return;
  }

  if( mp_socket )
    mp_socket->deleteLater(); // pristine client side socket, never used

  mp_socket = bt_socket;
  mp_socket->setParent( this );
  connectSocketSignals();

  // Asymmetric handshake semantics copied from the TCP server side
  // (StreamDeviceTcp::setSocketDescriptor, core-transport.md section C.4
  // item 1): adopting an already connected socket does NOT emit connected(),
  // so ConnectionSocket does not send a question HELLO but waits for the
  // remote peer and answers with sendAnswerHello().
}

void StreamDeviceBt::abort()
{
  mp_socket->abort();
}

void StreamDeviceBt::close()
{
  mp_socket->close();
}

void StreamDeviceBt::disconnectFromHost()
{
  mp_socket->disconnectFromService();
}

bool StreamDeviceBt::flush()
{
  // QBluetoothSocket has no flush(): data is handed over by write() itself.
  return true;
}

QIODevice* StreamDeviceBt::ioDevice()
{
  return mp_socket;
}

QHostAddress StreamDeviceBt::peerAddress() const
{
  return syntheticAddressFromBtAddress( mp_socket->peerAddress() );
}

quint16 StreamDeviceBt::peerPort() const
{
  return BLUETOOTH_FAKE_PEER_PORT;
}

QString StreamDeviceBt::errorString() const
{
  return mp_socket->errorString();
}

bool StreamDeviceBt::isConnectingState() const
{
  QBluetoothSocket::SocketState socket_state = mp_socket->state();
  // ServiceLookupState is the SDP phase: the BT equivalent of HostLookupState.
  return socket_state == QBluetoothSocket::SocketState::ServiceLookupState || socket_state == QBluetoothSocket::SocketState::ConnectingState;
}

bool StreamDeviceBt::isConnectedState() const
{
  QBluetoothSocket::SocketState socket_state = mp_socket->state();
  return socket_state == QBluetoothSocket::SocketState::ConnectedState || socket_state == QBluetoothSocket::SocketState::BoundState;
}

bool StreamDeviceBt::isOpen() const
{
  return mp_socket->isOpen();
}

bool StreamDeviceBt::isValid() const
{
  // QBluetoothSocket does not provide isValid(): a socket not in the
  // unconnected state is the closest approximation.
  return mp_socket->state() != QBluetoothSocket::SocketState::UnconnectedState;
}

qint64 StreamDeviceBt::bytesAvailable() const
{
  return mp_socket->bytesAvailable();
}

qint64 StreamDeviceBt::write( const QByteArray& byte_array )
{
  return mp_socket->write( byte_array );
}

qintptr StreamDeviceBt::socketDescriptor() const
{
  return mp_socket->socketDescriptor();
}

void StreamDeviceBt::onSocketError( QBluetoothSocket::SocketError socket_error )
{
  QAbstractSocket::SocketError mapped_error = QAbstractSocket::UnknownSocketError;

  switch( socket_error )
  {
  // Straightforward mappings first.
  case QBluetoothSocket::SocketError::RemoteHostClosedError:
    mapped_error = QAbstractSocket::RemoteHostClosedError;
    break;
  case QBluetoothSocket::SocketError::HostNotFoundError:
    mapped_error = QAbstractSocket::HostNotFoundError;
    break;
  case QBluetoothSocket::SocketError::NetworkError:
    mapped_error = QAbstractSocket::NetworkError;
    break;
  case QBluetoothSocket::SocketError::UnsupportedProtocolError:
    mapped_error = QAbstractSocket::UnsupportedSocketOperationError;
    break;
  case QBluetoothSocket::SocketError::OperationError:
    mapped_error = QAbstractSocket::OperationError;
    break;
  case QBluetoothSocket::SocketError::UnknownSocketError:
    mapped_error = QAbstractSocket::UnknownSocketError;
    break;
  // Approximations: QAbstractSocket has no service/permission specific errors.
  case QBluetoothSocket::SocketError::ServiceNotFoundError:
    mapped_error = QAbstractSocket::ConnectionRefusedError;
    break;
  case QBluetoothSocket::SocketError::MissingPermissionsError:
    mapped_error = QAbstractSocket::SocketAccessError;
    break;
  case QBluetoothSocket::SocketError::NoSocketError:
    // Nothing to report: ConnectionSocket would log and close the connection.
    return;
  }

  qDebug() << "StreamDeviceBt socket error" << static_cast<int>( socket_error ) << "mapped to" << static_cast<int>( mapped_error )
           << ":" << mp_socket->errorString();

  emit errorOccurred( mapped_error );
}
