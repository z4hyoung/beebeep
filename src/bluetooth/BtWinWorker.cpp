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

#include "BtWinWorker.h"
#include "Config.h"

#if defined( Q_OS_WIN )

#include <winsock2.h>
#include <ws2bth.h>
#include <bthsdpdef.h>
#include <bthdef.h>
#include <bluetoothapis.h>
#include <nspapi.h>

#include <QtCore/QDebug>
#include <QtCore/QSocketNotifier>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>


BtWinWorker* BtWinWorker::s_instance = Q_NULLPTR;

namespace
{
  // Maximum number of discovered devices probed with an SDP query per scan
  // cycle: every query costs 0.5-3 s over the air and the scan runs in the
  // worker thread, so the cycle stays bounded.
  const int c_maxSdpQueriesPerScan = 16;

  // Inquiry length in 1.28 s units (2 x 1.28 = 2.56 s).
  const UCHAR c_inquiryTimeoutUnits = 2;

  // RFCOMM connect timeout: SDP plus L2CAP/RFCOMM setup can take a while.
  const int c_connectTimeoutMs = 12000;

  GUID parseServiceGuid()
  {
    GUID guid = {};
    const QString uuid_string = QString::fromLatin1( BLUETOOTH_SERVICE_UUID );
    bool ok = false;
    guid.Data1 = uuid_string.mid( 0, 8 ).toUInt( &ok, 16 );
    guid.Data2 = ok ? uuid_string.mid( 9, 4 ).toUShort( &ok, 16 ) : 0;
    guid.Data3 = ok ? uuid_string.mid( 14, 4 ).toUShort( &ok, 16 ) : 0;
    if( ok )
    {
      // Layout 8-4-4-4-12: Data4[0..1] sit between the dashes at 18 and 23,
      // Data4[2..7] follow the dash at 23.
      for( int i = 0; i < 8 && ok; i++ )
      {
        int hex_pos = (i < 2) ? (19 + i * 2) : (20 + i * 2);
        guid.Data4[i] = static_cast<BYTE>( uuid_string.mid( hex_pos, 2 ).toUShort( &ok, 16 ) );
      }
    }
    if( !ok )
      qWarning() << "BtWinWorker cannot parse the service UUID" << BLUETOOTH_SERVICE_UUID;
    else
      qDebug() << "BtWinWorker service GUID parsed from" << BLUETOOTH_SERVICE_UUID;
    return guid;
  }

  QString wsaErrorText( int wsa_error )
  {
    wchar_t* msg_buffer = Q_NULLPTR;
    FormatMessageW( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    Q_NULLPTR, static_cast<DWORD>( wsa_error ), MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
                    reinterpret_cast<LPWSTR>( &msg_buffer ), 0, Q_NULLPTR );
    QString message = msg_buffer ? QString::fromWCharArray( msg_buffer ).trimmed()
                                 : QString( "WSA error %1" ).arg( wsa_error );
    if( msg_buffer )
      LocalFree( msg_buffer );
    return message;
  }

  // "service class on a single remote device" SDP query (Microsoft pattern:
  // lpszContext comes from WSAAddressToString of the device address).
  bool sdpQueryServiceChannel( quint64 bt_address, const GUID& service_guid, quint32* channel, QString* service_name )
  {
    SOCKADDR_BTH sa = {};
    sa.addressFamily = AF_BTH;
    sa.btAddr = bt_address;

    wchar_t context_buffer[64] = {};
    int context_size = sizeof( context_buffer );
    DWORD string_size = static_cast<DWORD>( context_size );
    if( WSAAddressToStringW( reinterpret_cast<SOCKADDR*>( &sa ), sizeof( sa ), Q_NULLPTR,
                             context_buffer, &string_size ) != 0 )
    {
      qWarning() << "BtWinWorker: WSAAddressToString failed for" << Qt::hex << bt_address << "with" << WSAGetLastError();
      return false;
    }

    WSAQUERYSETW query = {};
    query.dwSize = sizeof( WSAQUERYSETW );
    query.lpServiceClassId = const_cast<LPGUID>( &service_guid );
    query.dwNameSpace = NS_BTH;
    query.dwNumberOfCsAddrs = 0;
    query.lpszContext = context_buffer;

    HANDLE lookup = Q_NULLPTR;
    if( WSALookupServiceBeginW( &query, LUP_FLUSHCACHE, &lookup ) != 0 )
      return false; // typically WSASERVICE_NOT_FOUND: the device does not run our service

    // WSALookupServiceNext fills the queryset plus the returned addresses and
    // strings into one buffer: it must be far larger than sizeof(WSAQUERYSETW)
    // or the call fails with WSAEFAULT.
    std::vector<BYTE> result_buffer( 8192, 0 );
    WSAQUERYSETW* result = reinterpret_cast<WSAQUERYSETW*>( result_buffer.data() );
    result->dwSize = sizeof( WSAQUERYSETW );

    bool found = false;
    DWORD result_size = static_cast<DWORD>( result_buffer.size() );
    while( WSALookupServiceNextW( lookup, LUP_RETURN_NAME | LUP_RETURN_ADDR, &result_size, result ) == 0 )
    {
      if( result->dwNumberOfCsAddrs > 0 && result->lpcsaBuffer )
      {
        SOCKADDR_BTH* remote = reinterpret_cast<SOCKADDR_BTH*>( result->lpcsaBuffer[0].RemoteAddr.lpSockaddr );
        if( remote && remote->port > 0 )
        {
          if( channel )
            *channel = remote->port;
          if( service_name )
            *service_name = result->lpszServiceInstanceName ? QString::fromWCharArray( result->lpszServiceInstanceName ) : QString();
          found = true;
          break;
        }
      }
      result_size = static_cast<DWORD>( result_buffer.size() );
    }

    WSALookupServiceEnd( lookup );
    return found;
  }

  struct AcceptLoopData
  {
    BtWinWorker* worker;
    SOCKET listen_socket;
    std::atomic<bool> stopping;
  };

  void acceptLoop( AcceptLoopData* data )
  {
    while( !data->stopping.load() )
    {
      SOCKADDR_BTH peer = {};
      int peer_size = sizeof( peer );
      SOCKET client = ::accept( data->listen_socket, reinterpret_cast<SOCKADDR*>( &peer ), &peer_size );
      if( client == INVALID_SOCKET )
      {
        if( data->stopping.load() )
          break;
        int wsa_error = WSAGetLastError();
        qWarning() << "BtWinWorker: accept failed with" << wsa_error;
        std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        continue;
      }

      qDebug() << "BtWinWorker: incoming RFCOMM connection from" << Qt::hex << peer.btAddr;
      // Cross thread emit: receivers live on the GUI thread, so the auto
      // connection is queued and thread safe.
      emit data->worker->incomingConnection( peer.btAddr, reinterpret_cast<quintptr>( client ) );
    }
  }
}


// ------------------------------------------------------------------
// NativeBtSocket
// ------------------------------------------------------------------

NativeBtSocket::NativeBtSocket( QObject* parent )
  : QIODevice( parent ), m_socket( reinterpret_cast<void*>( INVALID_SOCKET ) ), m_peerAddress( 0 ),
    m_state( UnconnectedState ), mp_readNotifier( Q_NULLPTR )
{
}

NativeBtSocket::~NativeBtSocket()
{
  closeSocket();
}

bool NativeBtSocket::adoptConnectedSocket( qintptr socket_handle, quint64 peer_address )
{
  SOCKET s = static_cast<SOCKET>( socket_handle );
  if( s == INVALID_SOCKET )
    return false;

  closeSocket();

  m_socket = reinterpret_cast<void*>( s );
  m_peerAddress = peer_address;
  m_state = ConnectedState;

  // Blocking socket: writes are handed to the stack directly. A large send
  // can block the GUI thread briefly; accepted as a v1 limitation of the
  // native transport (chat sized blocks stay well under 100 ms).
  u_long blocking = 0;
  ioctlsocket( s, FIONBIO, &blocking );

  setOpenMode( QIODevice::ReadWrite | QIODevice::Unbuffered );

  mp_readNotifier = new QSocketNotifier( static_cast<qintptr>( s ), QSocketNotifier::Type::Read, this );
  connect( mp_readNotifier, &QSocketNotifier::activated, this, &NativeBtSocket::onReadActivated );
  mp_readNotifier->setEnabled( true );

  return true;
}

QString NativeBtSocket::errorString() const
{
  return wsaErrorText( WSAGetLastError() );
}

qint64 NativeBtSocket::bytesAvailable() const
{
  SOCKET s = reinterpret_cast<SOCKET>( m_socket );
  if( s == INVALID_SOCKET )
    return 0;
  u_long pending = 0;
  ioctlsocket( s, FIONREAD, &pending );
  return QIODevice::bytesAvailable() + static_cast<qint64>( pending );
}

void NativeBtSocket::closeSocket()
{
  if( mp_readNotifier )
  {
    mp_readNotifier->setEnabled( false );
    mp_readNotifier->deleteLater();
    mp_readNotifier = Q_NULLPTR;
  }

  SOCKET s = reinterpret_cast<SOCKET>( m_socket );
  if( s != INVALID_SOCKET )
  {
    ::shutdown( s, SD_BOTH );
    closesocket( s );
    m_socket = reinterpret_cast<void*>( INVALID_SOCKET );
  }

  if( m_state != UnconnectedState )
  {
    m_state = UnconnectedState;
    setOpenMode( QIODevice::NotOpen );
  }
}

void NativeBtSocket::onReadActivated()
{
  SOCKET s = reinterpret_cast<SOCKET>( m_socket );
  if( s == INVALID_SOCKET )
    return;

  // Peek one byte to distinguish "data arrived" from "peer closed".
  char peek_byte = 0;
  int peek_result = ::recv( s, &peek_byte, 1, MSG_PEEK );

  if( peek_result > 0 )
    emit readyRead();
  else if( peek_result == 0 )
  {
    qDebug() << "NativeBtSocket: peer has closed the connection";
    closeSocket();
    emit disconnected();
  }
  else if( WSAGetLastError() != WSAEWOULDBLOCK )
    emitNativeError( WSAGetLastError() );
}

qint64 NativeBtSocket::readData( char* data, qint64 max_size )
{
  SOCKET s = reinterpret_cast<SOCKET>( m_socket );
  if( s == INVALID_SOCKET )
    return -1;

  int bytes_read = ::recv( s, data, static_cast<int>( qMin<qint64>( max_size, 1 << 20 ) ), 0 );

  if( bytes_read == 0 )
  {
    // Orderly shutdown by the peer: report EOF to QIODevice.
    closeSocket();
    emit disconnected();
    return 0;
  }

  if( bytes_read == SOCKET_ERROR )
  {
    int wsa_error = WSAGetLastError();
    if( wsa_error == WSAEWOULDBLOCK )
      return 0; // transient: no data yet, nothing read
    emitNativeError( wsa_error );
    return -1;
  }

  return bytes_read;
}

qint64 NativeBtSocket::writeData( const char* data, qint64 max_size )
{
  SOCKET s = reinterpret_cast<SOCKET>( m_socket );
  if( s == INVALID_SOCKET )
    return -1;

  qint64 total_sent = 0;
  while( total_sent < max_size )
  {
    int chunk_size = static_cast<int>( qMin<qint64>( max_size - total_sent, 1 << 20 ) );
    int sent = ::send( s, data + total_sent, chunk_size, 0 );
    if( sent == SOCKET_ERROR )
    {
      emitNativeError( WSAGetLastError() );
      return total_sent > 0 ? total_sent : -1;
    }
    total_sent += sent;
  }

  // QIODevice::write() emits bytesWritten() for the amount returned here.
  return total_sent;
}

void NativeBtSocket::emitNativeError( int wsa_error )
{
  qWarning() << "NativeBtSocket error" << wsa_error << ":" << wsaErrorText( wsa_error );
  emit errorOccurred( wsa_error );
}


// ------------------------------------------------------------------
// BtWinWorker
// ------------------------------------------------------------------

void* BtWinWorker::serviceGuid()
{
  static GUID service_guid = parseServiceGuid();
  return &service_guid;
}

QString BtWinWorker::deviceContextString( quint64 )
{
  // Kept for completeness: contexts are built inside sdpQueryServiceChannel.
  return QString();
}

void BtWinWorker::setInstance( BtWinWorker* worker )
{
  s_instance = worker;
}

BtWinWorker::BtWinWorker( QObject* parent )
  : QObject( parent ), m_radio( Q_NULLPTR ), m_localAddress( 0 ), m_radioFound( false ),
    m_listenSocket( reinterpret_cast<void*>( INVALID_SOCKET ) ), m_listenChannel( 0 ),
    m_sdpRegistered( false ), m_discoveryEnabledByUs( false ),
    m_acceptThread( Q_NULLPTR ), m_acceptData( Q_NULLPTR )
{
}

BtWinWorker::~BtWinWorker()
{
  stopServer();
}

void BtWinWorker::initialize()
{
  BLUETOOTH_FIND_RADIO_PARAMS radio_params = {};
  radio_params.dwSize = sizeof( BLUETOOTH_FIND_RADIO_PARAMS );

  HANDLE radio = Q_NULLPTR;
  HBLUETOOTH_RADIO_FIND radio_find = BluetoothFindFirstRadio( &radio_params, &radio );
  if( radio_find == Q_NULLPTR )
  {
    qWarning() << "BtWinWorker: no bluetooth radio found, the bluetooth transport stays inactive";
    emit radioError( tr( "No bluetooth radio found" ) );
    return;
  }
  BluetoothFindRadioClose( radio_find );

  BLUETOOTH_RADIO_INFO radio_info = {};
  radio_info.dwSize = sizeof( BLUETOOTH_RADIO_INFO );
  if( BluetoothGetRadioInfo( radio, &radio_info ) != ERROR_SUCCESS )
  {
    qWarning() << "BtWinWorker: BluetoothGetRadioInfo failed";
    CloseHandle( radio );
    emit radioError( tr( "Unable to query the bluetooth radio" ) );
    return;
  }

  m_radio = radio;
  m_localAddress = radio_info.address.ullLong;
  m_radioFound = true;

  qDebug() << "BtWinWorker: local radio" << Qt::hex << m_localAddress
           << "name" << QString::fromWCharArray( radio_info.szName );

  emit localAddressReady( m_localAddress );
}

bool BtWinWorker::publishSdpRecord( const QString& service_name )
{
  SOCKADDR_BTH local_sa = {};
  local_sa.addressFamily = AF_BTH;
  local_sa.port = m_listenChannel;

  SOCKADDR_BTH remote_sa = local_sa; // remote part of CSADDR_INFO is unused but must be present

  CSADDR_INFO cs_addr = {};
  cs_addr.LocalAddr.lpSockaddr = reinterpret_cast<SOCKADDR*>( &local_sa );
  cs_addr.LocalAddr.iSockaddrLength = sizeof( local_sa );
  cs_addr.RemoteAddr.lpSockaddr = reinterpret_cast<SOCKADDR*>( &remote_sa );
  cs_addr.RemoteAddr.iSockaddrLength = sizeof( remote_sa );
  cs_addr.iSocketType = SOCK_STREAM;
  cs_addr.iProtocol = BTHPROTO_RFCOMM;

  const std::wstring service_name_w = service_name.toStdWString();

  WSAQUERYSETW service = {};
  service.dwSize = sizeof( WSAQUERYSETW );
  service.lpszServiceInstanceName = const_cast<LPWSTR>( service_name_w.c_str() );
  service.lpServiceClassId = static_cast<LPGUID>( serviceGuid() );
  service.dwNameSpace = NS_BTH;
  service.dwNumberOfCsAddrs = 1;
  service.lpcsaBuffer = &cs_addr;

  if( WSASetServiceW( &service, RNRSERVICE_REGISTER, 0 ) != 0 )
  {
    qWarning() << "BtWinWorker: WSASetService REGISTER failed with" << WSAGetLastError();
    return false;
  }

  return true;
}

void BtWinWorker::removeSdpRecord()
{
  if( !m_sdpRegistered || m_listenChannel == 0 )
    return;

  SOCKADDR_BTH local_sa = {};
  local_sa.addressFamily = AF_BTH;
  local_sa.port = m_listenChannel;

  SOCKADDR_BTH remote_sa = local_sa;

  CSADDR_INFO cs_addr = {};
  cs_addr.LocalAddr.lpSockaddr = reinterpret_cast<SOCKADDR*>( &local_sa );
  cs_addr.LocalAddr.iSockaddrLength = sizeof( local_sa );
  cs_addr.RemoteAddr.lpSockaddr = reinterpret_cast<SOCKADDR*>( &remote_sa );
  cs_addr.RemoteAddr.iSockaddrLength = sizeof( remote_sa );
  cs_addr.iSocketType = SOCK_STREAM;
  cs_addr.iProtocol = BTHPROTO_RFCOMM;

  const std::wstring service_name_w = m_serviceName.toStdWString();

  WSAQUERYSETW service = {};
  service.dwSize = sizeof( WSAQUERYSETW );
  service.lpszServiceInstanceName = const_cast<LPWSTR>( service_name_w.c_str() );
  service.lpServiceClassId = static_cast<LPGUID>( serviceGuid() );
  service.dwNameSpace = NS_BTH;
  service.dwNumberOfCsAddrs = 1;
  service.lpcsaBuffer = &cs_addr;

  if( WSASetServiceW( &service, RNRSERVICE_DELETE, 0 ) != 0 )
    qWarning() << "BtWinWorker: WSASetService DELETE failed with" << WSAGetLastError();

  m_sdpRegistered = false;
}

void BtWinWorker::startServer( const QString& service_name )
{
  SOCKET listen_socket = reinterpret_cast<SOCKET>( m_listenSocket );
  if( listen_socket != INVALID_SOCKET )
    return; // already listening

  if( !m_radioFound )
  {
    emit serverFailed( tr( "No bluetooth radio found" ) );
    return;
  }

  listen_socket = ::socket( AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM );
  if( listen_socket == INVALID_SOCKET )
  {
    emit serverFailed( QString( "socket() failed: %1" ).arg( WSAGetLastError() ) );
    return;
  }

  SOCKADDR_BTH server_sa = {};
  server_sa.addressFamily = AF_BTH;
  server_sa.port = BT_PORT_ANY; // let the stack pick a free RFCOMM channel

  if( ::bind( listen_socket, reinterpret_cast<SOCKADDR*>( &server_sa ), sizeof( server_sa ) ) == SOCKET_ERROR )
  {
    emit serverFailed( QString( "bind() failed: %1" ).arg( wsaErrorText( WSAGetLastError() ) ) );
    closesocket( listen_socket );
    return;
  }

  SOCKADDR_BTH assigned_sa = {};
  int assigned_size = sizeof( assigned_sa );
  if( ::getsockname( listen_socket, reinterpret_cast<SOCKADDR*>( &assigned_sa ), &assigned_size ) == SOCKET_ERROR
      || assigned_sa.port == 0 || assigned_sa.port == BT_PORT_ANY )
  {
    emit serverFailed( QString( "getsockname() failed: %1" ).arg( WSAGetLastError() ) );
    closesocket( listen_socket );
    return;
  }

  m_listenChannel = static_cast<quint16>( assigned_sa.port );
  m_serviceName = service_name.isEmpty() ? QString::fromLatin1( BLUETOOTH_SERVICE_NAME ) : service_name;

  if( !publishSdpRecord( m_serviceName ) )
  {
    closesocket( listen_socket );
    emit serverFailed( tr( "Unable to publish the SDP service record" ) );
    return;
  }
  m_sdpRegistered = true;

  if( ::listen( listen_socket, 4 ) == SOCKET_ERROR )
  {
    emit serverFailed( QString( "listen() failed: %1" ).arg( WSAGetLastError() ) );
    removeSdpRecord();
    closesocket( listen_socket );
    return;
  }

  m_discoveryEnabledByUs = BluetoothEnableDiscovery( m_radio, TRUE );
  if( !m_discoveryEnabledByUs )
    qDebug() << "BtWinWorker: the radio was already discoverable (or discovery could not be enabled)";

  m_listenSocket = reinterpret_cast<void*>( listen_socket );

  AcceptLoopData* accept_data = new AcceptLoopData();
  accept_data->worker = this;
  accept_data->listen_socket = listen_socket;
  accept_data->stopping.store( false );
  m_acceptData = accept_data;
  m_acceptThread = new std::thread( acceptLoop, accept_data );

  qDebug() << "BtWinWorker: RFCOMM server listening on channel" << m_listenChannel << "for service" << m_serviceName;
  emit serverStarted( m_listenChannel );
}

void BtWinWorker::stopAcceptThread()
{
  AcceptLoopData* accept_data = static_cast<AcceptLoopData*>( m_acceptData );
  std::thread* accept_thread = static_cast<std::thread*>( m_acceptThread );

  if( accept_data )
    accept_data->stopping.store( true );

  // Closing the listen socket wakes up the blocked accept() call.
  SOCKET listen_socket = reinterpret_cast<SOCKET>( m_listenSocket );
  if( listen_socket != INVALID_SOCKET )
  {
    closesocket( listen_socket );
    m_listenSocket = reinterpret_cast<void*>( INVALID_SOCKET );
  }

  if( accept_thread )
  {
    accept_thread->join();
    delete accept_thread;
    m_acceptThread = Q_NULLPTR;
  }

  if( accept_data )
  {
    delete accept_data;
    m_acceptData = Q_NULLPTR;
  }
}

void BtWinWorker::stopServer()
{
  if( reinterpret_cast<SOCKET>( m_listenSocket ) == INVALID_SOCKET && m_acceptThread == Q_NULLPTR )
    return;

  stopAcceptThread();
  removeSdpRecord();

  if( m_discoveryEnabledByUs )
  {
    BluetoothEnableDiscovery( m_radio, FALSE );
    m_discoveryEnabledByUs = false;
  }

  m_listenChannel = 0;
  qDebug() << "BtWinWorker: RFCOMM server stopped";
}

void BtWinWorker::scanForPeers( quint64 local_address )
{
  if( !m_radioFound )
  {
    emit scanFinished();
    return;
  }

  BLUETOOTH_DEVICE_SEARCH_PARAMS search_params = {};
  search_params.dwSize = sizeof( BLUETOOTH_DEVICE_SEARCH_PARAMS );
  search_params.fReturnUnknown = TRUE;
  search_params.fReturnRemembered = TRUE; // covers peers paired with this PC
  search_params.fIssueInquiry = TRUE;
  search_params.cTimeoutMultiplier = c_inquiryTimeoutUnits;
  search_params.hRadio = m_radio;

  BLUETOOTH_DEVICE_INFO device_info = {};
  device_info.dwSize = sizeof( BLUETOOTH_DEVICE_INFO );

  HBLUETOOTH_DEVICE_FIND device_find = BluetoothFindFirstDevice( &search_params, &device_info );
  if( device_find == Q_NULLPTR )
  {
    qDebug() << "BtWinWorker: inquiry found no devices";
    emit scanFinished();
    return;
  }

  int probed = 0;
  do
  {
    quint64 peer_address = device_info.Address.ullLong;
    if( peer_address == 0 || peer_address == local_address )
      continue;
    if( probed >= c_maxSdpQueriesPerScan )
      break;

    probed++;
    quint32 channel = 0;
    QString service_name;
    if( sdpQueryServiceChannel( peer_address, *static_cast<GUID*>( serviceGuid() ), &channel, &service_name ) )
    {
      QString device_name = QString::fromWCharArray( device_info.szName );
      if( device_name.isEmpty() )
        device_name = service_name.isEmpty() ? QString( "Bluetooth device" ) : service_name;
      qDebug() << "BtWinWorker: found YongBEEP service on channel" << channel << "of" << device_name << "(" << Qt::hex << peer_address << ")";
      emit peerFound( peer_address, device_name );
    }
  }
  while( BluetoothFindNextDevice( device_find, &device_info ) );

  BluetoothFindDeviceClose( device_find );
  qDebug() << "BtWinWorker: scan cycle finished," << probed << "devices probed";
  emit scanFinished();
}

void BtWinWorker::connectToPeer( quint64 bt_address )
{
  SOCKET s = ::socket( AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM );
  if( s == INVALID_SOCKET )
  {
    emit connectFinished( bt_address, 0, WSAGetLastError() );
    return;
  }

  GUID* service_guid = static_cast<GUID*>( serviceGuid() );

  SOCKADDR_BTH peer_sa = {};
  peer_sa.addressFamily = AF_BTH;
  peer_sa.btAddr = bt_address;
  peer_sa.serviceClassId = *service_guid; // the stack resolves the channel with an SDP query

  u_long non_blocking = 1;
  ioctlsocket( s, FIONBIO, &non_blocking );

  int connect_result = ::connect( s, reinterpret_cast<SOCKADDR*>( &peer_sa ), sizeof( peer_sa ) );

  int wsa_error = 0;
  if( connect_result == SOCKET_ERROR )
  {
    wsa_error = WSAGetLastError();
    if( wsa_error == WSAEWOULDBLOCK )
    {
      fd_set write_set, error_set;
      FD_ZERO( &write_set );
      FD_ZERO( &error_set );
      FD_SET( s, &write_set );
      FD_SET( s, &error_set );

      struct timeval timeout = {};
      timeout.tv_sec = c_connectTimeoutMs / 1000;
      timeout.tv_usec = ( c_connectTimeoutMs % 1000 ) * 1000;

      int select_result = ::select( static_cast<int>( s ) + 1, Q_NULLPTR, &write_set, &error_set, &timeout );
      if( select_result <= 0 )
        wsa_error = WSAETIMEDOUT;
      else
      {
        int socket_error = 0;
        int socket_error_size = sizeof( socket_error );
        getsockopt( s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>( &socket_error ), &socket_error_size );
        wsa_error = socket_error;
      }
    }
  }

  if( wsa_error != 0 )
  {
    qWarning() << "BtWinWorker: connect to" << Qt::hex << bt_address << "failed with" << wsa_error << wsaErrorText( wsa_error );
    closesocket( s );
    emit connectFinished( bt_address, 0, wsa_error );
    return;
  }

  // Hand the connected socket to the GUI thread in blocking mode.
  u_long blocking = 0;
  ioctlsocket( s, FIONBIO, &blocking );

  qDebug() << "BtWinWorker: connected to" << Qt::hex << bt_address;
  emit connectFinished( bt_address, reinterpret_cast<quintptr>( s ), 0 );
}

void BtWinWorker::shutdown()
{
  stopServer();
  if( m_radio )
  {
    CloseHandle( m_radio );
    m_radio = Q_NULLPTR;
  }
  emit finished();
}

#endif // Q_OS_WIN
