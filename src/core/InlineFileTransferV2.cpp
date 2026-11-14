#include "InlineFileTransferPeer.h"
#include "Connection.h"
#include "BeeUtils.h"
#include "Settings.h"
#include <QDir>

void InlineFileTransferPeer::startV2()
{
  if( m_file.size() != m_fileInfo.size()
      || (m_fileInfo.lastModified().isValid() && QFileInfo( m_file ).lastModified() != m_fileInfo.lastModified()) )
  {
    failV2( tr( "The source file changed after it was offered" ) );
    return;
  }
  m_contentHash.reset();
  m_sentOffset = m_ackedOffset = 0;
  m_finishSent = false;
  setTransferringState();
  qDebug() << name() << "inline v2 upload started" << m_fileInfo.size() << "bytes, zstd level 6, window 262144";
  m_v2Timeout.start();
  QTimer::singleShot( 0, this, &InlineFileTransferPeer::sendV2Data );
}

bool InlineFileTransferPeer::sendV2( const InlineFileFrame& frame )
{
  if( !mp_connection || !mp_connection->sendData( frame.encode() ) )
  {
    failV2( tr( "Unable to send the file transfer frame" ), false );
    return false;
  }
  return true;
}

void InlineFileTransferPeer::failV2( const QString& reason, bool notify )
{
  if( stateIsStopped( m_state ) )
    return;
  if( notify && mp_connection )
  {
    InlineFileFrame failure;
    failure.kind = InlineFileFrame::Failure;
    failure.fileId = m_fileInfo.id();
    failure.payload = reason.toUtf8().left( 1024 );
    mp_connection->sendData( failure.encode() );
  }
  m_verifying = false;
  if( m_verifiedFile.isOpen() ) m_verifiedFile.cancelWriting();
  m_v2Timeout.stop();
  setError( reason );
  detachFromConnection();
}

void InlineFileTransferPeer::sendV2Data()
{
  if( m_state != Transferring || isDownload() || m_finishSent )
    return;
  const quint64 file_size = quint64( m_fileInfo.size() );
  // Bound outstanding source bytes even when the payload compresses very well.
  const quint64 window = 4 * InlineFileFrame::MaxBlockSize;
  if( m_sentOffset < file_size && m_sentOffset - m_ackedOffset < window )
  {
    QByteArray raw = m_file.read( qMin<quint64>( InlineFileFrame::MaxBlockSize, file_size - m_sentOffset ) );
    if( quint64( raw.size() ) != qMin<quint64>( InlineFileFrame::MaxBlockSize, file_size - m_sentOffset ) )
    {
      failV2( tr( "Unable to read the source file" ) );
      return;
    }
    InlineFileFrame frame = InlineFileFrame::data( m_fileInfo.id(), m_sentOffset, raw );
    m_contentHash.addData( raw );
    m_sentOffset += raw.size();
    if( sendV2( frame ) )
      QTimer::singleShot( 0, this, &InlineFileTransferPeer::sendV2Data );
    return;
  }
  if( m_sentOffset == file_size && m_ackedOffset == file_size )
  {
    if( m_file.size() != m_fileInfo.size()
      || (m_fileInfo.lastModified().isValid() && QFileInfo( m_file ).lastModified() != m_fileInfo.lastModified()) )
    {
      failV2( tr( "The source file changed during transfer" ) );
      return;
    }
    InlineFileFrame finish;
    finish.kind = InlineFileFrame::Finish;
    finish.fileId = m_fileInfo.id();
    finish.offset = file_size;
    finish.payload = m_contentHash.result();
    m_finishSent = true;
    sendV2( finish );
  }
}

void InlineFileTransferPeer::receiveV2( const QByteArray& bytes )
{
  if( !m_version2 || stateIsStopped( m_state ) )
    return;
  InlineFileFrame frame;
  if( !InlineFileFrame::decode( bytes, &frame ) )
  {
    // Malformed frames cannot be reliably attributed to one transfer. Its
    // watchdog will report failure if the sender cannot make progress.
    return;
  }
  if( frame.fileId != m_fileInfo.id() )
    return;
  m_v2Timeout.start();
  if( frame.kind == InlineFileFrame::Failure )
  {
    failV2( tr( "Remote file transfer failed: %1" ).arg( QString::fromUtf8( frame.payload ) ), false );
    return;
  }
  if( !isDownload() )
  {
    if( frame.kind == InlineFileFrame::Ack && frame.offset == m_ackedOffset )
      return;
    if( frame.kind == InlineFileFrame::Ack && frame.offset > m_ackedOffset
        && frame.offset <= m_sentOffset
        && (frame.offset % InlineFileFrame::MaxBlockSize == 0 || frame.offset == quint64( m_fileInfo.size() )) )
    {
      m_ackedOffset = frame.offset;
      m_totalBytesTransferred = frame.offset;
      showProgress();
      QTimer::singleShot( 0, this, &InlineFileTransferPeer::sendV2Data );
    }
    else if( frame.kind == InlineFileFrame::Complete && m_finishSent
             && frame.offset == quint64( m_fileInfo.size() ) && frame.payload == m_contentHash.result() )
    {
      qDebug() << name() << "SHA-256 verified by receiver" << frame.payload.toHex();
      setTransferCompleted();
      detachFromConnection();
    }
    else
      failV2( tr( "Invalid file transfer confirmation" ) );
    return;
  }
  if( m_state == Queue )
  {
    setTransferringState();
    qDebug() << name() << "inline v2 download started" << m_fileInfo.size() << "bytes";
  }
  if( m_verifying || frame.offset != quint64( m_totalBytesTransferred ) )
  {
    failV2( tr( "Invalid file transfer offset" ) );
    return;
  }
  if( frame.kind == InlineFileFrame::Data )
  {
    QByteArray raw;
    if( frame.offset > quint64( m_fileInfo.size() )
        || frame.rawSize > quint64( m_fileInfo.size() ) - frame.offset || !frame.unpack( &raw ) )
    {
      failV2( tr( "Invalid compressed file data" ) );
      return;
    }
    if( m_file.write( raw ) != raw.size() )
    {
      failV2( tr( "Unable to write the received file" ) );
      return;
    }
    m_totalBytesTransferred += raw.size();
    InlineFileFrame ack;
    ack.kind = InlineFileFrame::Ack;
    ack.fileId = m_fileInfo.id();
    ack.offset = m_totalBytesTransferred;
    if( sendV2( ack ) )
      showProgress();
    return;
  }
  if( frame.kind != InlineFileFrame::Finish || frame.offset != quint64( m_fileInfo.size() )
      || m_file.size() != m_fileInfo.size() || !m_file.flush() || !m_file.seek( 0 ) )
  {
    failV2( tr( "The received file is incomplete or could not be saved" ) );
    return;
  }
  m_expectedHash = frame.payload;
  m_contentHash.reset();
  m_verifying = true;
  emit message( id(), remoteUserId(), m_fileInfo, tr( "Verifying file contents (SHA-256)" ), m_state );
  QTimer::singleShot( 0, this, &InlineFileTransferPeer::verifyV2 );
}

void InlineFileTransferPeer::verifyV2()
{
  if( !m_verifying || m_state != Transferring )
    return;
  if( !m_verifiedFile.isOpen() )
  {
    QString destination = m_fileInfo.path();
    if( QFile::exists( destination ) && !Settings::instance().removeExistingFileOnDownloadCompleted() )
      destination = Bee::uniqueFilePath( destination, true );
    m_verifiedFile.setFileName( destination );
    m_verifiedFile.setDirectWriteFallback( false );
    if( !QDir().mkpath( QFileInfo( destination ).absolutePath() ) || !m_verifiedFile.open( QIODevice::WriteOnly ) )
    {
      failV2( tr( "Unable to save the verified file" ) );
      return;
    }
  }
  QByteArray raw = m_file.read( 1024 * 1024 );
  if( m_file.error() != QFileDevice::NoError || (raw.isEmpty() && !m_file.atEnd()) )
  {
    failV2( tr( "Unable to read the received file for verification" ) );
    return;
  }
  if( m_verifiedFile.write( raw ) != raw.size() )
  {
    failV2( tr( "Unable to save the verified file" ) );
    return;
  }
  m_contentHash.addData( raw );
  m_v2Timeout.start();
  if( !m_file.atEnd() )
  {
    QTimer::singleShot( 0, this, &InlineFileTransferPeer::verifyV2 );
    return;
  }
  if( m_file.size() != m_fileInfo.size() || m_contentHash.result() != m_expectedHash )
  {
    failV2( tr( "File content verification failed (SHA-256 mismatch)" ) );
    return;
  }
  m_file.close();
  if( !m_verifiedFile.commit() )
  {
    failV2( tr( "Unable to save the verified file" ) );
    return;
  }
  m_file.remove();
  m_fileInfo.setPath( m_verifiedFile.fileName() );
  m_file.setFileName( m_fileInfo.path() );
  m_verifying = false;
  InlineFileFrame complete;
  complete.kind = InlineFileFrame::Complete;
  complete.fileId = m_fileInfo.id();
  complete.offset = m_fileInfo.size();
  complete.payload = m_expectedHash;
  computeElapsedTime();
  qDebug() << name() << "SHA-256 verified" << m_expectedHash.toHex()
           << "bytes" << m_fileInfo.size() << "elapsed milliseconds" << m_elapsedTime;
  // The destination is committed before acknowledging success. A failed
  // acknowledgement must not delete the successfully saved destination.
  setTransferCompleted();
  if( mp_connection )
    mp_connection->sendData( complete.encode() );
  detachFromConnection();
}
