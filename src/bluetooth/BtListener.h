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

#ifndef BEEBEEP_BTLISTENER_H
#define BEEBEEP_BTLISTENER_H

#include <QtBluetooth/QBluetoothServer>
#include <QtBluetooth/QBluetoothSocket>


/*! \brief RFCOMM server: listens on the YongBEEP service UUID and hands out connected sockets
 *
 * Mirror of core/Listener for the Bluetooth transport (docs/02-技术设计.md §2.2):
 * a QBluetoothServer with RfcommProtocol registers our fixed service UUID via
 * listen(uuid, name) and emits newBtConnection() with an already connected
 * QBluetoothSocket, that Core adopts through ConnectionSocket::adoptBluetoothSocket().
 */
class BtListener : public QObject
{
  Q_OBJECT

public:
  explicit BtListener( QObject* parent = Q_NULLPTR );

  bool listen( const QString& service_name );
  void close();
  inline bool isListening() const;
  inline quint16 serverPort() const;

signals:
  void newBtConnection( QBluetoothSocket* bt_socket );

private slots:
  void onServerNewConnection();

private:
  QBluetoothServer* mp_server;

};


// Inline Functions
inline bool BtListener::isListening() const { return mp_server->isListening(); }
inline quint16 BtListener::serverPort() const { return mp_server->serverPort(); }

#endif // BEEBEEP_BTLISTENER_H
