//////////////////////////////////////////////////////////////////////
//
// BeeBEEP Copyright (C) 2010-2026 Marco Mastroddi
//
// BeeBEEP is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// BeeBEEP is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with BeeBEEP. If not, see <http://www.gnu.org/licenses/>.
//
// Author: Marco Mastroddi <marco.mastroddi(AT)gmail.com>
//
// $Id: StreamDeviceTcp.cpp 1600 2026-09-18 12:11:12Z mastroddi $
//
//////////////////////////////////////////////////////////////////////

#include "StreamDeviceTcp.h"
#include "Settings.h"


StreamDeviceTcp::StreamDeviceTcp( QObject* parent )
  : StreamDevice( parent ), mp_socket( new QTcpSocket( this ) )
{
  // Socket options and proxy policy moved here from the old ConnectionSocket constructor
  if( Settings::instance().useKeepAliveOptionInSocket() )
    mp_socket->setSocketOption( QAbstractSocket::KeepAliveOption, 1 );
  else
    mp_socket->setSocketOption( QAbstractSocket::KeepAliveOption, 0 );
  if( Settings::instance().useLowDelayOptionOnSocket() )
    mp_socket->setSocketOption( QAbstractSocket::LowDelayOption, 1 );
  else
    mp_socket->setSocketOption( QAbstractSocket::LowDelayOption, 0 );
  if( Settings::instance().disableSystemProxyForConnections() )
    mp_socket->setProxy( QNetworkProxy::NoProxy );

  connect( mp_socket, SIGNAL( connected() ), this, SIGNAL( connected() ) );
  connect( mp_socket, SIGNAL( readyRead() ), this, SIGNAL( readyRead() ) );
  connect( mp_socket, SIGNAL( bytesWritten(qint64) ), this, SIGNAL( bytesWritten(qint64) ) );
  connect( mp_socket, SIGNAL( errorOccurred(QAbstractSocket::SocketError) ), this, SIGNAL( errorOccurred(QAbstractSocket::SocketError) ) );
  connect( mp_socket, SIGNAL( disconnected() ), this, SIGNAL( disconnected() ) );
}

void StreamDeviceTcp::connectToHost( const QHostAddress& host_address, quint16 host_port )
{
  mp_socket->connectToHost( host_address, host_port );
}

bool StreamDeviceTcp::setSocketDescriptor( qintptr socket_descriptor )
{
  return mp_socket->setSocketDescriptor( socket_descriptor );
}

void StreamDeviceTcp::adoptSocket( QObject* )
{
  // Not supported by the TCP transport: server sockets are adopted through setSocketDescriptor().
  // Reserved for the Bluetooth implementation (step 2).
}

void StreamDeviceTcp::abort()
{
  mp_socket->abort();
}

void StreamDeviceTcp::close()
{
  mp_socket->close();
}

void StreamDeviceTcp::disconnectFromHost()
{
  mp_socket->disconnectFromHost();
}

bool StreamDeviceTcp::flush()
{
  return mp_socket->flush();
}

QIODevice* StreamDeviceTcp::ioDevice()
{
  return mp_socket;
}

QHostAddress StreamDeviceTcp::peerAddress() const
{
  return mp_socket->peerAddress();
}

quint16 StreamDeviceTcp::peerPort() const
{
  return mp_socket->peerPort();
}

QString StreamDeviceTcp::errorString() const
{
  return mp_socket->errorString();
}

bool StreamDeviceTcp::isConnectingState() const
{
  QAbstractSocket::SocketState socket_state = mp_socket->state();
  return socket_state == QAbstractSocket::HostLookupState || socket_state == QAbstractSocket::ConnectingState;
}

bool StreamDeviceTcp::isConnectedState() const
{
  QAbstractSocket::SocketState socket_state = mp_socket->state();
  return socket_state == QAbstractSocket::ConnectedState || socket_state == QAbstractSocket::BoundState;
}

bool StreamDeviceTcp::isOpen() const
{
  return mp_socket->isOpen();
}

bool StreamDeviceTcp::isValid() const
{
  return mp_socket->isValid();
}

qint64 StreamDeviceTcp::bytesAvailable() const
{
  return mp_socket->bytesAvailable();
}

qint64 StreamDeviceTcp::write( const QByteArray& byte_array )
{
  return mp_socket->write( byte_array );
}

qintptr StreamDeviceTcp::socketDescriptor() const
{
  return mp_socket->socketDescriptor();
}
