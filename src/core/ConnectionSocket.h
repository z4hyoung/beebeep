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
// $Id: ConnectionSocket.h 1600 2026-09-18 12:11:12Z mastroddi $
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_CONNECTIONSOCKET_H
#define BEEBEEP_CONNECTIONSOCKET_H

#include "ECDH.h"
#include "NetworkAddress.h"
#include "StreamDevice.h"

class Message;
class QBluetoothAddress;
class QObject;
class StreamDeviceBt;


/*! \brief Protocol socket (HELLO handshake, ECDH, compression, framing)
 *
 * Formerly a QTcpSocket subclass, now it composes a StreamDevice
 * (StreamDeviceTcp in this step) and keeps the exact public API and
 * signal contract that Core, Connection, FileTransferPeer and
 * GuiNetworkTest rely on. The TCP behavior is unchanged byte for byte.
 */
class ConnectionSocket : public QObject
{
  Q_OBJECT

public:
  explicit ConnectionSocket( QObject* parent = Q_NULLPTR );

  // Transport forwards (same names of the old QTcpSocket base)
  inline QHostAddress peerAddress() const;
  inline quint16 peerPort() const;
  inline QString errorString() const;
  inline bool isOpen() const;
  inline bool isValid() const;
  inline qintptr socketDescriptor() const;

  void connectToNetworkAddress( const NetworkAddress& );
  void initSocket( qintptr, quint16 server_port );

  // Bluetooth RFCOMM transport injection (docs/02-技术设计.md §2.1/§2.2).
  // Both entry points must be used before the handshake starts: they replace
  // the default TCP stream and keep its exact semantics (client = question
  // HELLO on connected(), server = wait for the peer and sendAnswerHello()).
  void connectToBluetoothService( StreamDeviceBt* bt_stream, const QBluetoothAddress& bt_peer_address );
  void adoptBluetoothSocket( StreamDeviceBt* bt_stream, QObject* bt_socket, quint16 bt_server_channel );

  /*! Generic injection point: replaces the default TCP stream before the handshake (the old one is deleted). */
  void setStreamDevice( StreamDevice* new_stream );

  bool sendData( const QByteArray& );

  void flushAll();
  virtual void closeConnection();
  virtual void abortConnection();

  inline VNumber userId() const;
  inline void setUserId( VNumber );

  inline int protocolVersion() const;
  inline bool isNewGenerationPeer() const;
  inline bool isBluetoothTransport() const;
  int fileTransferBufferSize() const;

  inline bool isConnected() const;
  inline bool isConnecting() const;

  inline const QDateTime& latestActivityDateTime() const;
  int activityIdle() const; // ms idle
  inline const NetworkAddress& networkAddress() const;

  void onTickEvent( int );

  inline bool isTestConnection() const;
  inline void setTestConnection( bool );

  inline bool isServerSocket() const;
  inline bool isEncrypted() const;
  inline bool isCompressed() const;

signals:
  // Transport signals forwarded from the StreamDevice: external code
  // (CoreConnection.cpp, FileTransferPeer.cpp, GuiNetworkTest.cpp)
  // connects to them by signature and must keep resolving.
  void connected();
  void readyRead();
  void bytesWritten( qint64 bytes );
  void error( QAbstractSocket::SocketError socket_error );
  void disconnected();

  void dataReceived( const QByteArray& );
  void authenticationRequested( const QByteArray& );
  void abortRequest();
  void pingRequest();
  void connectionTestCompleted( const QString& );

protected slots:
  qint64 readBlock();
  void sendQuestionHello();
  void onBytesWritten( qint64 );

protected:
  inline bool isHelloSent() const;
  void sendAnswerHello( bool encryption_enabled, bool compression_enabled );
  void checkHelloMessage( const QByteArray& );
  QByteArray serializeData( const QByteArray& );
  const QByteArray& cipherKey() const;
  bool createCipherKey( const QString& other_public_key );

  bool checkConnectionTimeout( int );
  bool checkTestMessage( const Message& );

  inline bool isKeysHandshakeCompleted() const;
  void useEncryption( bool );
  void useCompression( bool );

private:
  void connectStreamSignals();

  StreamDevice* mp_stream;
  bool m_isBluetoothTransport;

  // max block size contains lowers
  DATA_BLOCK_SIZE_32 m_blockSize;
  bool m_isHelloSent;
  VNumber m_userId;
  int m_protocolVersion;
  QString m_publicKey1;
  QString m_publicKey2;
  ECDH::Keys m_ecdhKeys;
  QByteArray m_cipherKey;

  NetworkAddress m_networkAddress;

  QDateTime m_latestActivityDateTime;
  bool m_checkConnectionTimeout;
  int m_tickCounter;
  bool m_isAborted;

  int m_datastreamVersion;
  int m_pingByteArraySize;

  bool m_isTestConnection;

  quint16 m_serverPort;

  bool m_isEncrypted;
  bool m_isCompressed;
  bool m_isNewGenerationPeer;

};


// Inline Functions
inline QHostAddress ConnectionSocket::peerAddress() const { return mp_stream->peerAddress(); }
inline quint16 ConnectionSocket::peerPort() const { return mp_stream->peerPort(); }
inline QString ConnectionSocket::errorString() const { return mp_stream->errorString(); }
inline bool ConnectionSocket::isOpen() const { return mp_stream->isOpen(); }
inline bool ConnectionSocket::isValid() const { return mp_stream->isValid(); }
inline qintptr ConnectionSocket::socketDescriptor() const { return mp_stream->socketDescriptor(); }
inline VNumber ConnectionSocket::userId() const { return m_userId; }
inline void ConnectionSocket::setUserId( VNumber new_value ) { m_userId = new_value; }
inline int ConnectionSocket::protocolVersion() const { return m_protocolVersion; }
inline bool ConnectionSocket::isNewGenerationPeer() const { return m_isNewGenerationPeer; }
inline bool ConnectionSocket::isBluetoothTransport() const { return m_isBluetoothTransport; }
inline bool ConnectionSocket::isConnected() const { return mp_stream->isOpen() && mp_stream->isConnectedState(); }
inline bool ConnectionSocket::isConnecting() const { return mp_stream->isOpen() && mp_stream->isConnectingState(); }
inline const QDateTime& ConnectionSocket::latestActivityDateTime() const { return m_latestActivityDateTime; }
inline const NetworkAddress& ConnectionSocket::networkAddress() const { return m_networkAddress; }
inline bool ConnectionSocket::isTestConnection() const { return m_isTestConnection; }
inline void ConnectionSocket::setTestConnection( bool new_value ) { m_isTestConnection = new_value; }
inline bool ConnectionSocket::isServerSocket() const { return m_serverPort > 0; }
inline bool ConnectionSocket::isKeysHandshakeCompleted() const { return !m_cipherKey.isEmpty(); }
inline bool ConnectionSocket::isEncrypted() const { return m_isEncrypted; }
inline bool ConnectionSocket::isCompressed() const { return m_isCompressed; }
inline bool ConnectionSocket::isHelloSent() const { return m_isHelloSent; }

#endif // BEEBEEP_CONNECTIONSOCKET_H
