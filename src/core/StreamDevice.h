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
// $Id: StreamDevice.h 1600 2026-09-18 12:11:12Z mastroddi $
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_STREAMDEVICE_H
#define BEEBEEP_STREAMDEVICE_H

#include <QObject>
#include <QHostAddress>
#include <QAbstractSocket>


/*! \brief Transport abstraction used by ConnectionSocket
 *
 * ConnectionSocket no longer inherits QTcpSocket: it composes a
 * StreamDevice and delegates all the stream I/O to it. StreamDeviceTcp
 * is the TCP implementation (byte-for-byte port of the old code) and a
 * Bluetooth RFCOMM implementation will follow in the next step.
 *
 * Signal contract (must stay stable, Core and FileTransferPeer connect
 * to the ConnectionSocket signals forwarded from here):
 *   - connected()                       -> starts the HELLO handshake
 *   - readyRead()                       -> data available for readBlock()
 *   - bytesWritten( qint64 )            -> activity refresh on large blocks
 *   - errorOccurred( SocketError )      -> mapped to ConnectionSocket::error()
 *   - disconnected()                    -> connection closed by the transport
 */
class StreamDevice : public QObject
{
  Q_OBJECT

public:
  explicit StreamDevice( QObject* parent = Q_NULLPTR )
    : QObject( parent )
  {
  }

  /*! Client side connection entry point (TCP: QHostAddress + port). */
  virtual void connectToHost( const QHostAddress& host_address, quint16 host_port ) = 0;

  /*! Server side entry point (TCP): adopt a native socket descriptor. */
  virtual bool setSocketDescriptor( qintptr socket_descriptor ) = 0;

  /*! Server side entry point reserved for the Bluetooth transport (step 2):
   * adopt an already connected socket object. The TCP implementation is a no-op. */
  virtual void adoptSocket( QObject* socket_object ) = 0;

  virtual void abort() = 0;
  virtual void close() = 0;
  virtual void disconnectFromHost() = 0;
  virtual bool flush() = 0;

  /*! The QIODevice behind the transport: readBlock() mounts its QDataStream here. */
  virtual QIODevice* ioDevice() = 0;

  virtual QHostAddress peerAddress() const = 0;
  virtual quint16 peerPort() const = 0;
  virtual QString errorString() const = 0;

  /*! True while the transport is resolving or connecting (QAbstractSocket::HostLookupState or ConnectingState). */
  virtual bool isConnectingState() const = 0;

  /*! True when the transport is connected or bound (QAbstractSocket::ConnectedState or BoundState). */
  virtual bool isConnectedState() const = 0;

  virtual bool isOpen() const = 0;
  /*! QAbstractSocket::isValid() equivalent: true if the socket is valid and ready for use. */
  virtual bool isValid() const = 0;
  virtual qint64 bytesAvailable() const = 0;
  virtual qint64 write( const QByteArray& byte_array ) = 0;
  virtual qintptr socketDescriptor() const = 0;

signals:
  void connected();
  void readyRead();
  void bytesWritten( qint64 bytes );
  void errorOccurred( QAbstractSocket::SocketError socket_error );
  void disconnected();

};

#endif // BEEBEEP_STREAMDEVICE_H
