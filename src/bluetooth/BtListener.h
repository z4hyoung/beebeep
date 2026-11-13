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

#include <QObject>

class QBluetoothServer;
class QBluetoothSocket;
class BtWinWorker;

#if !defined( Q_OS_WIN )
  #include <QtBluetooth/QBluetoothServer>
  #include <QtBluetooth/QBluetoothSocket>
#endif


/*! \brief RFCOMM server: listens on the YongBEEP service UUID and hands out connected sockets
 *
 * Mirror of core/Listener for the Bluetooth transport (docs/02-技术设计.md §2.2):
 * on macOS a QBluetoothServer with RfcommProtocol registers our fixed service
 * UUID via listen(uuid, name); on Windows the native BtWinWorker does the same
 * with an AF_BTH socket plus a WSASetService published SDP record (MinGW
 * builds of Qt have no functional bluetooth backend, docs/02-技术设计.md §2.6).
 *
 * In both cases newBtConnection() is emitted with an already connected socket
 * object (QBluetoothSocket on Qt, NativeBtSocket on Windows), that Core
 * adopts through ConnectionSocket::adoptBluetoothSocket().
 */
class BtListener : public QObject
{
  Q_OBJECT

public:
  explicit BtListener( QObject* parent = Q_NULLPTR );
  ~BtListener() override;

  bool listen( const QString& service_name );
  void close();
  inline bool isListening() const;
  inline quint16 serverPort() const;

#if defined( Q_OS_WIN )
  /*! The native worker doing the actual socket work (owned by BtManager). */
  void setWinWorker( BtWinWorker* win_worker );
#endif

signals:
  void newBtConnection( QObject* bt_socket );

private slots:
#if !defined( Q_OS_WIN )
  void onServerNewConnection();
#else
  void onWinIncomingConnection( quint64 peer_address, quintptr socket_handle );
  void onWinServerStarted( quint16 channel );
  void onWinServerFailed( const QString& message );
#endif

private:
#if !defined( Q_OS_WIN )
  QBluetoothServer* mp_server;
  QBluetoothServiceInfo m_serviceInfo;
#else
  BtWinWorker* mp_winWorker;
  bool m_listening;
  quint16 m_channel;
#endif

};


// Inline Functions
#if !defined( Q_OS_WIN )
inline bool BtListener::isListening() const { return mp_server->isListening() && m_serviceInfo.isRegistered(); }
inline quint16 BtListener::serverPort() const { return mp_server->serverPort(); }
#else
inline bool BtListener::isListening() const { return m_listening; }
inline quint16 BtListener::serverPort() const { return m_channel; }
#endif

#endif // BEEBEEP_BTLISTENER_H
