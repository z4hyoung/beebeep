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

#ifndef BEEBEEP_STREAMDEVICEBT_H
#define BEEBEEP_STREAMDEVICEBT_H

#include "StreamDevice.h"
#include <QtBluetooth/QBluetoothAddress>
#include <QtBluetooth/QBluetoothSocket>
#include <QtBluetooth/QBluetoothUuid>


/*! \brief Bluetooth RFCOMM implementation of StreamDevice
 *
 * Wraps a QBluetoothSocket created with RfcommProtocol and adapts it to the
 * StreamDevice contract used by ConnectionSocket (docs/02-技术设计.md §2.2).
 *
 * Synthetic peer address mapping (docs/analysis/core-transport.md §C.4 item 4
 * and §G.2 item 5): the 48-bit Bluetooth MAC is mapped into an IPv6
 * link-local address with a modified EUI-64 interface identifier, so that
 * User.networkAddress() and Core::hasConnection() keep working with the
 * existing QHostAddress based code:
 *
 *   fe80::m0^2 : m1 m2 : 00FF : FE m3 : m4 m5
 *
 * where m0..m5 are the MAC bytes in transmission order. The mapping is
 * bijective and stable for the whole session, which is what the duplicate
 * connection checks require. The U/L bit (bit 1 of the first byte) is
 * flipped like in the standard EUI-64 derivation, so a BT MAC can never
 * collide with the fe80:: address a real NIC would derive from its own MAC
 * (a WLAN MAC and a BT MAC on the same machine always differ in more than
 * that bit, and the NIC-derived address is not used by YongBEEP anyway).
 *
 * peerPort() returns a fixed pseudo port (BLUETOOTH_FAKE_PEER_PORT): the real
 * RFCOMM server channel is assigned by the OS and is not stable across SDP
 * queries and platforms, so it must not be part of the identity key.
 *
 * API deviations from the TCP transport (Qt 6.12 headers are the source):
 *   - QBluetoothSocket has no flush(): writes are handed to the stack
 *     directly, so flush() is a successful no-op;
 *   - isValid() is not available either: it is approximated with
 *     "state is not UnconnectedState";
 *   - errorOccurred(QBluetoothSocket::SocketError) is mapped to
 *     QAbstractSocket::SocketError in onSocketError() (mapping table below).
 */
class StreamDeviceBt : public StreamDevice
{
  Q_OBJECT

public:
  explicit StreamDeviceBt( QObject* parent = Q_NULLPTR );

  /*! The fixed RFCOMM service UUID used by every YongBEEP instance. */
  static QBluetoothUuid serviceUuid();

  /*! Stable synthetic IPv6 link-local address for a Bluetooth MAC. */
  static QHostAddress syntheticAddressFromBtAddress( const QBluetoothAddress& bt_address );

  /*! Client side connection entry point: SDP lookup + RFCOMM connect to our service UUID. */
  void connectToService( const QBluetoothAddress& bt_address );

  /*! StreamDevice interface (TCP entries are unsupported and warn). */
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

  bool isBluetoothTransport() const { return true; }

private slots:
  void onSocketError( QBluetoothSocket::SocketError socket_error );

private:
  void connectSocketSignals();

  QBluetoothSocket* mp_socket;

};


#endif // BEEBEEP_STREAMDEVICEBT_H
