#include "InlineFileFrame.h"
#include "InlineFileTransferPeer.h"
#include <QTcpServer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QRandomGenerator>
#include <functional>

static bool waitForTransferTest( const std::function<bool()>& done )
{
  QElapsedTimer timer;
  timer.start();
  while( !done() && timer.elapsed() < 10000 )
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
  return done();
}

class TransferTestServer : public QTcpServer
{
public:
  Connection connection;
protected:
  void incomingConnection( qintptr descriptor ) override
  {
    connection.initSocket( descriptor, serverPort() );
  }
};

class TransferTestPeer : public InlineFileTransferPeer
{
public:
  TransferState state() const { return m_state; }
  bool version2() const { return m_version2; }
  void setDestination( const QString& path ) { m_fileInfo.setPath( path ); }
  void expireSoon() { m_v2Timeout.start( 50 ); }
  void corruptTemporaryFile()
  {
    m_file.seek( 0 );
    m_file.write( "corruption", 10 );
    m_file.flush();
    m_file.seek( 0 );
  }
};

static void testInlineFrames()
{
  QByteArray raw( 65536, 'a' );
  InlineFileFrame frame = InlineFileFrame::data( 42, 65536, raw );
  CHECK( frame.compressed && frame.payload.size() < 100, "v2 compresses repetitive data" );
  InlineFileFrame parsed;
  QByteArray restored;
  CHECK( InlineFileFrame::decode( frame.encode(), &parsed ) && parsed.unpack( &restored )
         && restored == raw && parsed.offset == 65536, "v2 binary frame roundtrip" );
  CHECK( !InlineFileFrame::decode( frame.encode().chopped( 1 ), &parsed ), "v2 rejects truncated frame" );
  CHECK( !InlineFileFrame::decode( frame.encode() + 'x', &parsed ), "v2 rejects invalid padding" );
  frame.rawSize = InlineFileFrame::MaxBlockSize + 1;
  CHECK( !InlineFileFrame::decode( frame.encode(), &parsed ), "v2 bounds decoded size" );
  for( int i = 0; i < raw.size(); ++i ) raw[i] = char( QRandomGenerator::global()->generate() );
  frame = InlineFileFrame::data( 42, 0, raw );
  CHECK( !frame.compressed && frame.payload == raw, "v2 sends incompressible data unchanged" );
  frame = InlineFileFrame::data( 42, 0, QByteArray( 65536, 'z' ) );
  frame.payload[0] = 0;
  CHECK( !frame.unpack( &restored ), "v2 rejects damaged compressed data" );
}

static void testInlineTransfer( int size, bool legacy = false, int failure = 0, bool existing = false )
{
  QTemporaryDir dir;
  CHECK( dir.isValid(), "transfer fixture directory" );
  QByteArray payload( size, Qt::Uninitialized );
  for( int i = 0; i < size; ++i ) payload[i] = char( i * 31 );
  QFile source( dir.filePath( "source.bin" ) );
  CHECK( source.open( QIODevice::WriteOnly ) && source.write( payload ) == payload.size(), "transfer fixture source" );
  source.close();
  FileInfo source_info( 4242, FileInfo::Upload );
  source_info.setNameAndSuffix( "source.bin" );
  source_info.setPath( source.fileName() );
  source_info.setSize( payload.size() );
  source_info.setLastModified( QFileInfo( source ).lastModified() );
  source_info.setFileHash( QUuid::createUuid().toString( QUuid::WithoutBraces ) );
  QDir().mkpath( Settings::instance().cacheFolder() );
  Message offer = Protocol::instance().fileInfoToMessage( source_info, 96 );
  offer.addFlag( Message::InlineFile );
  if( !legacy ) offer.addFlag( Message::InlineFileV2 );
  FileInfo destination = Protocol::instance().fileInfoFromMessage(
    Protocol::instance().toMessage( Protocol::instance().fromMessage( offer, 96 ), 96 ), 96 );
  destination.setPath( dir.filePath( "received.bin" ) );

  TransferTestServer server;
  Connection client;
  QObject::connect( &client, &Connection::authenticationRequested, &client, [&]() { client.setUserId( 101 ); } );
  QObject::connect( &server.connection, &Connection::authenticationRequested, &server, [&]() { server.connection.setUserId( 102 ); } );
  CHECK( server.listen( QHostAddress::LocalHost, 0 ), "transfer loopback listener" );
  NetworkAddress address;
  address.setHostAddress( QHostAddress::LocalHost );
  address.setHostPort( server.serverPort() );
  client.connectToNetworkAddress( address );
  CHECK( waitForTransferTest( [&]() { return client.userId() == 101 && server.connection.userId() == 102; } ), "transfer real connection handshake" );
  if( client.userId() != 101 || server.connection.userId() != 102 ) return;
  CHECK( client.isEncrypted() && server.connection.isEncrypted(), "transfer tests encrypted transport" );

  const int existing_action = Settings::instance().onExistingFileAction();
  if( existing )
  {
    QFile old_file( destination.path() );
    CHECK( old_file.open( QIODevice::WriteOnly ) && old_file.write( "keep old contents" ) == 17, "existing destination fixture" );
    Settings::instance().setOnExistingFileAction( Settings::OverwriteExistingFile );
  }
  TransferTestPeer upload, download;
  QObject::connect( &client, &Connection::newMessage, &upload, [&]( VNumber, const Message& m ) {
    if( m.type() == Message::File && m.hasFlag( Message::Request ) )
    {
      upload.onUploadAccepted( m.hasFlag( Message::InlineFileV2 ) );
      if( failure == 6 ) upload.expireSoon();
    }
    else if( m.type() == Message::File && m.hasFlag( Message::Refused ) ) upload.cancelInlineTransfer( false );
    else if( m.type() == Message::FileChunk ) upload.parseFileChunk( m );
  } );
  QObject::connect( &server.connection, &Connection::newMessage, &download, [&]( VNumber, const Message& m ) {
    if( m.type() == Message::FileChunk ) download.parseFileChunk( m );
  } );
  bool verifying = false;
  QObject::connect( &download, &FileTransferPeer::message, &download,
    [&]( VNumber, VNumber, const FileInfo&, const QString& text, FileTransferPeer::TransferState ) {
      if( text == QCoreApplication::translate( "InlineFileTransferPeer", "Verifying file contents (SHA-256)" ) )
      {
        verifying = true;
        CHECK( !upload.isTransferCompleted(), "sender waits for verification and save" );
        if( failure == 1 ) download.corruptTemporaryFile();
        if( failure == 2 ) download.setDestination( source.fileName() + "/invalid.bin" );
        if( failure == 3 ) download.cancelInlineTransfer();
        if( failure == 4 ) server.connection.abortConnection();
      }
    } );
  upload.setupInlineUpload( 101, source_info, &client );
  if( failure == 5 || failure == 7 )
  {
    source.open( QIODevice::Append );
    if( failure == 5 ) source.write( "changed" );
    else source.setFileTime( source_info.lastModified().addSecs( 5 ), QFileDevice::FileModificationTime );
    source.close();
  }
  if( failure == 6 )
    QObject::disconnect( &client, &Connection::inlineFileFrameReceived, &upload, nullptr );
  CHECK( download.startInlineDownload( 102, destination, &server.connection ), "transfer accepted" );
  CHECK( waitForTransferTest( [&]() {
    return FileTransferPeer::stateIsStopped( upload.state() ) && FileTransferPeer::stateIsStopped( download.state() );
  } ), "transfer terminates on both sides" );
  CHECK( upload.version2() == !legacy, "transfer negotiates v2 with legacy fallback" );
  if( failure )
  {
    const auto expected = failure == 3 ? FileTransferPeer::Canceled : FileTransferPeer::Error;
    CHECK( upload.state() == expected && download.state() == expected, "failure terminates both endpoints without success" );
    if( existing )
    {
      QFile old_file( destination.path() );
      CHECK( old_file.open( QIODevice::ReadOnly ) && old_file.readAll() == "keep old contents", "failed verification preserves existing destination" );
    }
    else
      CHECK( !QFile::exists( destination.path() ), "failed transfer has no completed destination" );
    CHECK( QDir( dir.path() ).entryList( {".yongbeep-*.part"}, QDir::Files | QDir::Hidden ).isEmpty(), "failed v2 transfer cleans its temporary file" );
  }
  else
  {
    CHECK( upload.isTransferCompleted() && download.isTransferCompleted(), "transfer completes both endpoints" );
    QFile received( download.fileInfo().path() );
    CHECK( received.open( QIODevice::ReadOnly ) && received.readAll() == payload, "saved file is byte-identical" );
    CHECK( verifying == !legacy, "v2 verification runs only after negotiation" );
  }
  Settings::instance().setOnExistingFileAction( existing_action );
  client.abortConnection();
  server.connection.abortConnection();
}

static void testInlineTransferV2()
{
  testInlineFrames();
  for( int size : { 0, 1, 65535, 65536, 65537, 1048576 } ) testInlineTransfer( size );
  testInlineTransfer( 65537, true );
  testInlineTransfer( 65537, false, 0, true );
  testInlineTransfer( 65537, false, 1, true );
  for( int failure = 1; failure <= 7; ++failure ) testInlineTransfer( 65537, false, failure );
}
