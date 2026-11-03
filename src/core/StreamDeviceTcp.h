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
// $Id: StreamDeviceTcp.h 1600 2026-09-18 12:11:12Z mastroddi $
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_STREAMDEVICETCP_H
#define BEEBEEP_STREAMDEVICETCP_H

#include "StreamDevice.h"
#include <QTcpSocket>


/*! \brief TCP implementation of StreamDevice
 *
 * Byte-for-byte port of the socket handling that ConnectionSocket used
 * to inherit from QTcpSocket: socket options (KeepAlive/LowDelay), the
 * NoProxy policy and the QIODevice based I/O. All the QTcpSocket
 * signals are re-emitted with the StreamDevice signature.
 */
class StreamDeviceTcp : public StreamDevice
{
  Q_OBJECT

public:
  explicit StreamDeviceTcp( QObject* parent = Q_NULLPTR );

  void connectToHost( const QHostAddress& host_address, quint16 host_port );
  bool setSocketDescriptor( qintptr socket_descriptor );
  void adoptSocket( QObject* socket_object );

  void abort();
  void close();
  void disconnectFromHost();
  bool flush();

  QIODevice* ioDevice();

  QHostAddress peerAddress() const;
  quint16 peerPort() const;
  QString errorString() const;

  bool isConnectingState() const;
  bool isConnectedState() const;

  bool isOpen() const;
  bool isValid() const;
  qint64 bytesAvailable() const;
  qint64 write( const QByteArray& byte_array );
  qintptr socketDescriptor() const;

private:
  QTcpSocket* mp_socket;

};

#endif // BEEBEEP_STREAMDEVICETCP_H
