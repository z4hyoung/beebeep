//////////////////////////////////////////////////////////////////////
//
// YongBEEP Copyright (C) 2010-2026 Marco Mastroddi (original BeeBEEP)
// Bluetooth inline file transfer adaptation for YongBEEP.
//
// This file is part of YongBEEP, a derivative work of BeeBEEP
// (https://sourceforge.net/projects/beebeep).  Same GNU GPL v3 license.
//
//////////////////////////////////////////////////////////////////////

#include "BeeUtils.h"
#include "Connection.h"
#include "InlineFileTransferPeer.h"
#include "Protocol.h"
#include "Settings.h"
#include <QTimer>


InlineFileTransferPeer::InlineFileTransferPeer( QObject* parent )
  : FileTransferPeer( parent ), mp_connection( Q_NULLPTR ), m_chunkIndex( 0 ), m_ackIndex( 0 ), m_totalChunks( 0 )
{
  setObjectName( "InlineFileTransferPeer" );
}

void InlineFileTransferPeer::setupInlineUpload( VNumber to_user_id, const FileInfo& fi, Connection* connection )
{
  setTransferType( FileInfo::Upload );
  setRemoteUserId( to_user_id );
  setFileInfo( FileInfo::Upload, fi );
  mp_connection = connection;
  connect( mp_connection, SIGNAL( disconnected() ), this, SLOT( onInlineConnectionClosed() ) );
  connect( mp_connection, SIGNAL( destroyed( QObject* ) ), this, SLOT( onInlineConnectionClosed() ) );
  // The offer (BEE-FILE + InlineFile) has already been sent by Core::sendFileToUser():
  // stay in queue until the remote peer answers with the GO message. If the
  // request is lost (peer offline when the message connection was rebuilt or
  // dropped) nothing would ever answer it: arm the same confirm timeout used
  // by the 6476 channel so the transfer fails cleanly and can be re-sent
  // (docs/03-测试方案.md M3-4).
  QTimer::singleShot( Settings::instance().fileTransferConfirmTimeout(), this, SLOT( onInlineAcceptTimeout() ) );
  setInQueue();
}

bool InlineFileTransferPeer::startInlineDownload( VNumber from_user_id, const FileInfo& fi, Connection* connection )
{
  setTransferType( FileInfo::Download );
  setRemoteUserId( from_user_id );
  setFileInfo( FileInfo::Download, fi );
  mp_connection = connection;
  connect( mp_connection, SIGNAL( disconnected() ), this, SLOT( onInlineConnectionClosed() ) );
  connect( mp_connection, SIGNAL( destroyed( QObject* ) ), this, SLOT( onInlineConnectionClosed() ) );

  m_chunkIndex = 0;
  m_ackIndex = 0;
  m_totalChunks = fi.size() > 0 ? static_cast<quint32>( ( fi.size() + INLINE_FILE_CHUNK_SIZE - 1 ) / INLINE_FILE_CHUNK_SIZE ) : 0;
  m_bytesTransferred = 0;
  m_totalBytesTransferred = 0;

  if( !m_file.isOpen() )
  {
    if( !m_file.open( QIODevice::ReadWrite | QIODevice::Truncate ) )
    {
      setError( tr( "Unable to create temporary file %1" ).arg( m_file.fileName() ) );
      return false;
    }
  }

  setInQueue();

#ifdef BEEBEEP_DEBUG
  qDebug() << qPrintable( name() ) << "waits" << m_totalChunks << "chunks of" << INLINE_FILE_CHUNK_SIZE << "bytes for" << m_fileInfo.size() << "bytes in total";
#endif

  sendGoMessage();
  return true;
}

void InlineFileTransferPeer::sendGoMessage()
{
  if( !mp_connection )
  {
    setError( tr( "Connection is not available" ) );
    return;
  }

  // GO message: BEE-FILE + InlineFile + Request with the accepted FileInfo.
  // It tells the uploader that this side is ready to receive the chunks
  // (the equivalent of connecting to the 6476 port in the old channel).
  Message go_message = Protocol::instance().fileInfoToMessage( m_fileInfo, mp_connection->protocolVersion() );
  go_message.addFlag( Message::InlineFile );
  go_message.addFlag( Message::Request );
  if( !mp_connection->sendMessage( go_message ) )
    setError( tr( "Unable to send the inline file request" ) );
}

void InlineFileTransferPeer::onUploadAccepted()
{
  if( m_state != FileTransferPeer::Queue && m_state != FileTransferPeer::Starting )
  {
    qWarning() << qPrintable( name() ) << "receives the remote acceptance but it is in state" << m_state;
    return;
  }

  if( !m_file.isOpen() && !m_file.open( QIODevice::ReadOnly ) )
  {
    setError( tr( "Unable to open file %1" ).arg( m_file.fileName() ) );
    return;
  }

  if( m_file.size() != m_fileInfo.size() )
  {
    qDebug() << qPrintable( name() ) << "has found a size change in file" << qPrintable( m_file.fileName() ) << ":" << m_file.size() << "instead of" << m_fileInfo.size();
    m_fileInfo.setSize( m_file.size() );
  }

  m_chunkIndex = 0;
  m_ackIndex = 0;
  m_totalChunks = m_file.size() > 0 ? static_cast<quint32>( ( m_file.size() + INLINE_FILE_CHUNK_SIZE - 1 ) / INLINE_FILE_CHUNK_SIZE ) : 0;
  if( m_totalChunks == 0 )
  {
    setError( tr( "File %1 is empty" ).arg( m_fileInfo.name() ) );
    return;
  }

#ifdef BEEBEEP_DEBUG
  qDebug() << qPrintable( name() ) << "is accepted by remote peer and starts sending" << m_totalChunks << "chunks of" << m_fileInfo.name();
#endif

  setTransferringState();
  sendChunkMessage();
}

bool InlineFileTransferPeer::sendChunkMessage()
{
  // YongBEEP: fill the sliding window - send every chunk that fits into
  // INLINE_FILE_WINDOW ahead of the last acknowledged one (stop-and-wait
  // spent one RTT per 64KiB chunk; the transport is an ordered stream so
  // ACKs arrive in order and duplicates are ignored in checkUploadAck)
  while( m_state == FileTransferPeer::Transferring &&
         m_chunkIndex < m_totalChunks &&
         static_cast<int>( m_chunkIndex - m_ackIndex ) < INLINE_FILE_WINDOW )
  {
    if( !sendSingleChunk() )
      return false;
  }
  return true;
}

bool InlineFileTransferPeer::sendSingleChunk()
{
  if( m_state != FileTransferPeer::Transferring )
  {
    qWarning() << qPrintable( name() ) << "tries to send a chunk but it is in state" << m_state;
    return false;
  }

  if( !mp_connection )
  {
    setError( tr( "Connection is not available" ) );
    return false;
  }

  FileSizeType byte_offset = static_cast<FileSizeType>( m_chunkIndex ) * INLINE_FILE_CHUNK_SIZE;
  if( !m_file.seek( byte_offset ) )
  {
    setError( tr( "Unable to seek %1 bytes in file %2" ).arg( byte_offset ).arg( m_file.fileName() ) );
    return false;
  }

  QByteArray chunk_bytes = m_file.read( INLINE_FILE_CHUNK_SIZE );
  if( chunk_bytes.isEmpty() )
  {
    setError( tr( "Unexpected end of file %1" ).arg( m_file.fileName() ) );
    return false;
  }

  Message m = Protocol::instance().fileChunkDataToMessage( m_fileInfo.id(), m_chunkIndex, m_totalChunks, byte_offset, chunk_bytes );

#ifdef BEEBEEP_DEBUG
  qDebug() << qPrintable( name() ) << "sends inline chunk" << ( m_chunkIndex + 1 ) << "of" << m_totalChunks
           << "with" << chunk_bytes.size() << "bytes at offset" << byte_offset << "of file" << qPrintable( m_fileInfo.name() );
#endif

  if( !mp_connection->sendMessage( m ) )
  {
    setError( tr( "Unable to send inline chunk %1" ).arg( m_chunkIndex + 1 ) );
    return false;
  }

  return true;
}

void InlineFileTransferPeer::parseFileChunk( const Message& m )
{
  VNumber file_info_id = ID_INVALID;
  quint32 chunk_index = 0;
  quint32 total_chunks = 0;
  FileSizeType byte_offset = 0;
  FileSizeType chunk_size = 0;
  QByteArray chunk_bytes;

  if( !Protocol::instance().fileChunkFromMessage( m, &file_info_id, &chunk_index, &total_chunks, &byte_offset, &chunk_size, &chunk_bytes ) )
  {
    qWarning() << qPrintable( name() ) << "has received an invalid inline file chunk and drops it";
    return;
  }

  if( file_info_id != m_fileInfo.id() )
  {
    qWarning() << qPrintable( name() ) << "has received an inline chunk for file id" << file_info_id << "but it is transferring file id" << m_fileInfo.id();
    return;
  }

  if( m.hasFlag( Message::Request ) )
  {
    // ACK for the uploader side only
    if( isDownload() )
    {
      qWarning() << qPrintable( name() ) << "has received an unexpected chunk confirmation while downloading";
      return;
    }
    checkUploadAck( chunk_index, total_chunks, byte_offset, chunk_size );
  }
  else
  {
    // Data chunk for the downloader side only
    if( !isDownload() )
    {
      qWarning() << qPrintable( name() ) << "has received an unexpected data chunk while uploading";
      return;
    }
    checkDownloadChunk( chunk_index, total_chunks, byte_offset, chunk_size, chunk_bytes );
  }
}

void InlineFileTransferPeer::checkUploadAck( quint32 chunk_index, quint32 total_chunks, FileSizeType byte_offset, FileSizeType chunk_size )
{
  if( m_state != FileTransferPeer::Transferring )
  {
#ifdef BEEBEEP_DEBUG
    qDebug() << qPrintable( name() ) << "discards a stale inline chunk confirmation received in state" << m_state;
#endif
    return;
  }

  if( total_chunks != m_totalChunks )
  {
    setError( tr( "Invalid inline chunk confirmation received (%1 of %2)" ).arg( chunk_index + 1 ).arg( total_chunks ) );
    return;
  }

  // YongBEEP sliding window: ACKs arrive in order on the reliable stream;
  // anything below m_ackIndex is a duplicate, anything at/above the send
  // cursor would be an ACK for a chunk we never sent
  if( chunk_index < m_ackIndex )
    return;

  if( chunk_index >= m_chunkIndex )
  {
    setError( tr( "Invalid inline chunk confirmation received (%1 of %2)" ).arg( chunk_index + 1 ).arg( total_chunks ) );
    return;
  }

  m_ackIndex = chunk_index + 1;
  m_totalBytesTransferred = byte_offset + chunk_size;
  showProgress();

  if( m_ackIndex >= m_totalChunks )
  {
    setTransferCompleted();
    detachFromConnection();
    return;
  }

  sendChunkMessage();
}

void InlineFileTransferPeer::checkDownloadChunk( quint32 chunk_index, quint32 total_chunks, FileSizeType byte_offset, FileSizeType chunk_size, const QByteArray& chunk_bytes )
{
  if( m_state == FileTransferPeer::Queue )
    setTransferringState();

  if( m_state != FileTransferPeer::Transferring )
  {
    qWarning() << qPrintable( name() ) << "discards inline chunk" << ( chunk_index + 1 ) << "because it is in state" << m_state;
    return;
  }

  if( total_chunks != m_totalChunks )
  {
    setError( tr( "Invalid number of chunks received (%1 instead of %2)" ).arg( total_chunks ).arg( m_totalChunks ) );
    return;
  }

  // YongBEEP: tolerate duplicate/stale chunks. The sender may resend a chunk
  // (peer timeout retry, a retried offer on another transport, or two transfer
  // paths racing). Treating any out-of-order chunk as fatal killed transfers
  // that were actually progressing - e.g. "out of order inline chunk received
  // (1 instead of 2)" was a resent chunk 0 after chunk 0 was already stored.
  if( chunk_index < m_chunkIndex )
  {
    qWarning() << qPrintable( name() ) << "ignores duplicate inline chunk" << ( chunk_index + 1 )
               << "already received up to" << m_chunkIndex;
    // Re-ACK it so a stop-and-wait peer waiting on this chunk can move on.
    if( isDownload() )
      sendChunkAck( chunk_index, byte_offset, chunk_size );
    return;
  }

  if( chunk_index > m_chunkIndex )
  {
    setError( tr( "Out of order inline chunk received (%1 instead of %2)" ).arg( chunk_index + 1 ).arg( m_chunkIndex + 1 ) );
    return;
  }

  if( byte_offset != m_totalBytesTransferred )
  {
    setError( tr( "Invalid offset %1 in inline chunk %2" ).arg( byte_offset ).arg( chunk_index + 1 ) );
    return;
  }

  if( chunk_bytes.size() != static_cast<int>( chunk_size ) )
  {
    setError( tr( "Corrupted inline chunk received (%1 bytes instead of %2)" ).arg( chunk_bytes.size() ).arg( chunk_size ) );
    return;
  }

  if( !m_file.isOpen() )
  {
    setError( tr( "Temporary file %1 is not open" ).arg( m_file.fileName() ) );
    return;
  }

  if( !m_file.seek( byte_offset ) || m_file.write( chunk_bytes ) != chunk_bytes.size() )
  {
    setError( tr( "Unable to write %1 bytes to temporary file %2" ).arg( chunk_bytes.size() ).arg( m_file.fileName() ) );
    return;
  }

#ifdef BEEBEEP_DEBUG
  qDebug() << qPrintable( name() ) << "has received inline chunk" << ( chunk_index + 1 ) << "of" << total_chunks << "with" << chunk_bytes.size() << "bytes of file" << qPrintable( m_fileInfo.name() );
#endif

  m_totalBytesTransferred += chunk_bytes.size();
  m_chunkIndex++;

  // Stop-and-wait: acknowledge this chunk so the sender can send the next one
  sendChunkAck( chunk_index, byte_offset, chunk_bytes.size() );

  showProgress();

  if( m_chunkIndex >= total_chunks )
    checkDownloadCompleted();
}

bool InlineFileTransferPeer::sendChunkAck( quint32 chunk_index, FileSizeType byte_offset, FileSizeType chunk_size )
{
  if( !mp_connection )
    return false;

  Message m = Protocol::instance().fileChunkAckToMessage( m_fileInfo.id(), chunk_index, m_totalChunks, byte_offset, chunk_size );

#ifdef BEEBEEP_DEBUG
  qDebug() << qPrintable( name() ) << "sends the confirmation of inline chunk" << ( chunk_index + 1 ) << "of" << m_totalChunks;
#endif

  if( !mp_connection->sendMessage( m ) )
  {
    setError( tr( "Unable to send the inline chunk confirmation" ) );
    return false;
  }
  return true;
}

void InlineFileTransferPeer::checkDownloadCompleted()
{
  const FileSizeType downloaded_size = m_file.size();
  m_file.flush();
  m_file.close();

  if( downloaded_size != m_fileInfo.size() )
  {
    setError( tr( "Downloaded file has %1 bytes instead of %2" ).arg( downloaded_size ).arg( m_fileInfo.size() ) );
    return;
  }

  // setTransferCompleted() renames the .part temporary file to the destination
  // path, applies the last modified date and emits the GUI messages.
  setTransferCompleted();

  // Best effort metadata verification: Protocol::fileInfoHash() hashes
  // name + size + last modified (not the content), so a mismatch is only
  // warned and it can legitimately differ between hosts in different time
  // zones. The strict integrity gate is the size check above.
  QFileInfo downloaded_file( m_fileInfo.path() );
  if( downloaded_file.exists() && Protocol::instance().fileInfoHash( downloaded_file ) != m_fileInfo.fileHash() )
    qWarning() << qPrintable( name() ) << "has completed the transfer of" << qPrintable( m_fileInfo.name() )
               << "but its metadata hash does not match the one announced by the sender";

  detachFromConnection();
}

void InlineFileTransferPeer::cancelInlineTransfer( bool notify_remote )
{
  if( m_state == FileTransferPeer::Canceled || m_state == FileTransferPeer::Completed || m_state == FileTransferPeer::Error )
    return;

  // Cancel semantics: notify the remote peer with the refused BEE-FILE
  // message (same Message::Refused pattern of Core::refuseToDownloadFile)
  // carrying the FileInfo, so it can stop its side of the transfer.
  if( notify_remote )
    sendRefusedToRemote();

  qDebug() << qPrintable( name() ) << "cancels the inline file transfer of" << qPrintable( m_fileInfo.name() );

  m_state = FileTransferPeer::Canceled;
  closeAll();

  if( m_fileInfo.isValid() && remoteUserId() != ID_INVALID )
    emit message( id(), remoteUserId(), m_fileInfo, tr( "Transfer canceled" ), m_state );

  emit operationCompleted();
  detachFromConnection();
}

void InlineFileTransferPeer::sendRefusedToRemote()
{
  if( !mp_connection || !m_fileInfo.isValid() )
    return;

  Message m = Protocol::instance().fileInfoRefusedToMessage( m_fileInfo, mp_connection->protocolVersion(), mp_connection->isNewGenerationPeer() );
  m.addFlag( Message::InlineFile );
  if( !mp_connection->sendMessage( m ) )
    qWarning() << qPrintable( name() ) << "is unable to send the cancel message of file" << qPrintable( m_fileInfo.name() );
}

void InlineFileTransferPeer::onInlineConnectionClosed()
{
  if( m_state == FileTransferPeer::Completed || m_state == FileTransferPeer::Error
      || m_state == FileTransferPeer::Canceled || m_state == FileTransferPeer::Paused )
    return;

  // The message connection is going away: never touch it again.
  mp_connection = Q_NULLPTR;
  setError( tr( "Connection closed" ) );
}

void InlineFileTransferPeer::onInlineAcceptTimeout()
{
  if( m_state != FileTransferPeer::Queue )
    return; // accepted and transferring (or already finished) long ago

  // The offer message was lost, typically because the peer was offline when
  // the file was sent: fail cleanly so the chat shows a "send it again" card
  // instead of an upload card stuck on "Sending..." forever.
  mp_connection = Q_NULLPTR;
  setError( tr( "No answer from the remote peer (offline?): file can be sent again" ) );
}

void InlineFileTransferPeer::detachFromConnection()
{
  if( mp_connection )
  {
    disconnect( mp_connection, Q_NULLPTR, this, Q_NULLPTR );
    mp_connection = Q_NULLPTR;
  }
}
