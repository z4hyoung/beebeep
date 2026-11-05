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

#ifndef BEEBEEP_BTWINWORKER_H
#define BEEBEEP_BTWINWORKER_H

#include <QtCore/QObject>
#include <QtCore/QIODevice>
#include <QtCore/QString>
#include <QtCore/QThread>

class QSocketNotifier;
class QByteArray;

#if defined( Q_OS_WIN )

/*! \brief Native Windows RFCOMM socket wrapped as a QIODevice
 *
 * MinGW builds of Qt ship QtBluetooth with a dummy backend only, so the
 * Windows transport talks to the native Bluetooth stack directly through
 * AF_BTH sockets (docs/02-技术设计.md §2.6). This class wraps a connected
 * RFCOMM SOCKET into the QIODevice that ConnectionSocket::readBlock()
 * expects from StreamDevice::ioDevice().
 *
 * The socket is always adopted in the connected state (from the worker
 * thread after accept() or after a completed client connect()) and lives
 * on the GUI thread from then on: readiness is reported through a
 * QSocketNotifier, exactly like the QtBluetooth path did.
 */
class NativeBtSocket : public QIODevice
{
  Q_OBJECT

public:
  enum State { UnconnectedState, ConnectedState };

  explicit NativeBtSocket( QObject* parent = Q_NULLPTR );
  ~NativeBtSocket() override;

  /*! Takes ownership of an already connected native socket. */
  bool adoptConnectedSocket( qintptr socket_handle, quint64 peer_address );

  inline State state() const { return m_state; }
  inline quint64 peerAddress() const { return m_peerAddress; }
  inline qintptr nativeHandle() const { return reinterpret_cast<qintptr>( m_socket ); }
  QString errorString() const;
  qint64 bytesAvailable() const override;

  void closeSocket();

signals:
  void disconnected();
  void errorOccurred( int wsa_error );

protected:
  qint64 readData( char* data, qint64 max_size ) override;
  qint64 writeData( const char* data, qint64 max_size ) override;

private slots:
  void onReadActivated();
  void onWriteActivated();

private:
  void emitNativeError( int wsa_error );
  void flushWriteBuffer();

  void* m_socket;              // SOCKET stored as void* to keep the header clean
  quint64 m_peerAddress;
  State m_state;
  QSocketNotifier* mp_readNotifier;
  QSocketNotifier* mp_writeNotifier;
  QByteArray m_writeBuffer;    // pending bytes drained by the write notifier

};


/*! \brief Worker thread for the native Windows Bluetooth stack
 *
 * All blocking Win32 Bluetooth calls (device inquiry, per device SDP
 * queries, the RFCOMM accept loop) run in a dedicated thread; the GUI
 * thread only receives queued signals. The worker lives in a QThread
 * owned by BtManager and speaks the same events as the QtBluetooth
 * implementation used on macOS (docs/02-技术设计.md §2.2):
 *
 *   localAddressReady()  the radio was found and its address is known
 *   peerFound()          a nearby device advertises the YongBEEP service
 *   incomingConnection() an RFCOMM client connected to our SDP record
 *   connectFinished()    a client side connect attempt completed
 *
 * Server side SDP publishing uses the simple WSASetService mode: the
 * system builds the SDP record (ServiceClassIdList, ServiceName and
 * ProtocolDescriptorList with our RFCOMM channel) from the WSAQUERYSET
 * standard members, and RNRSERVICE_DELETE removes it again. While the
 * transport is active the radio is made discoverable with
 * BluetoothEnableDiscovery() so that macOS peers can find us.
 */
class BtWinWorker : public QObject
{
  Q_OBJECT

public:
  explicit BtWinWorker( QObject* parent = Q_NULLPTR );
  ~BtWinWorker() override;

  /*! Global access for StreamDeviceBt client connects (set by BtManager). */
  inline static BtWinWorker* instance() { return s_instance; }
  static void setInstance( BtWinWorker* worker );

  /*! The fixed RFCOMM service UUID as a native GUID. */
  static void* serviceGuid();  // GUID stored as void* to keep the header clean

  /*! "XX:XX:XX:XX:XX:XX" style context string for WSALookupService. */
  static QString deviceContextString( quint64 bt_address );

public slots:
  void initialize();
  void startServer( const QString& service_name );
  void stopServer();
  void scanForPeers( quint64 local_address );
  void connectToPeer( quint64 bt_address );
  void shutdown();

signals:
  void localAddressReady( quint64 bt_address );
  void radioError( const QString& message );
  void serverStarted( quint16 channel );
  void serverFailed( const QString& message );
  void peerFound( quint64 bt_address, const QString& device_name );
  void scanFinished();
  void incomingConnection( quint64 peer_address, quintptr socket_handle );
  void connectFinished( quint64 bt_address, quintptr socket_handle, int wsa_error );
  void finished();

private:
  void stopAcceptThread();
  bool publishSdpRecord( const QString& service_name );
  void removeSdpRecord();

  void* m_radio;               // HANDLE of the bluetooth radio
  quint64 m_localAddress;
  bool m_radioFound;

  void* m_listenSocket;        // SOCKET of the RFCOMM server
  quint16 m_listenChannel;
  bool m_sdpRegistered;
  bool m_discoveryEnabledByUs;
  void* m_acceptThread;        // std::thread* hidden from this header
  void* m_acceptData;          // AcceptLoopData* hidden from this header
  QString m_serviceName;

  static BtWinWorker* s_instance;

};

#endif // Q_OS_WIN

#endif // BEEBEEP_BTWINWORKER_H
