//////////////////////////////////////////////////////////////////////
//
// BeeBEEP Copyright (C) 2010-2026 Marco Mastroddi
//
// BeeBEEP is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version.
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
// $Id: FileTransfer.cpp 1600 2026-09-18 12:11:12Z mastroddi $
//
//////////////////////////////////////////////////////////////////////

#include "Connection.h"
#include "FileShare.h"
#include "FileTransfer.h"
#include "Random.h"
#include "Settings.h"
#include "Protocol.h"
#include "UserManager.h"


FileTransfer::FileTransfer( QObject *parent )
  : QTcpServer( parent ), m_files(), m_peers()
{
}

bool FileTransfer::startListener()
{
  qDebug() << "Starting File Transfer server";

  if( !listen( Settings::instance().hostAddressToListen(), static_cast<quint16>(Settings::instance().defaultFileTransferPort() ) ) )
  {
    qWarning() << "Unable to bind default file transfer port" << Settings::instance().defaultFileTransferPort();
    if( !listen( Settings::instance().hostAddressToListen() ) )
    {
      qWarning() << "Unable to bind a valid file transfer port";
      return false;
    }
  }

  qDebug() << "File Transfer server listen" << serverAddress().toString() << serverPort();
  resetServerFiles();
  emit listening();

  if( downloadsInQueue() > 0  )
    qDebug() << "File Transfer has" << downloadsInQueue() << "files in queue";

  return true;
}

void FileTransfer::stopListener()
{
  if( isListening() )
  {
    close();
    qDebug() << "File Transfer server closed";
    foreach( FileTransferPeer* transfer_peer, m_peers )
    {
      // Inline transfers have no resume support yet: they can only be canceled
      if( Settings::instance().resumeFileTransfer() && qobject_cast<InlineFileTransferPeer*>( transfer_peer ) == Q_NULLPTR )
        transfer_peer->pauseTransfer( true );
      else
        cancelPeerTransfer( transfer_peer );
    }
  }
  else
    qDebug() << "File Transfer server is not active";
}

void FileTransfer::resetServerFiles()
{
#ifdef BEEBEEP_DEBUG
  qDebug() << "File Transfer reset files to"
           << qPrintable( Settings::instance().localUser().networkAddress().hostAddress().toString() )
           << serverPort();
#endif
  QList<FileInfo>::iterator it = m_files.begin();
  while( it != m_files.end() )
  {
    (*it).setHostAddress( Settings::instance().localUser().networkAddress().hostAddress() );
    (*it).setHostPort( serverPort() );
    ++it;
  }
}

void FileTransfer::removeFilesToUser( VNumber user_id )
{
  int peer_counter = 0;
  foreach( FileTransferPeer* transfer_peer, m_peers )
  {
    if( transfer_peer->isDownload() && transfer_peer->remoteUserId() == user_id )
    {
      cancelPeerTransfer( transfer_peer );
      peer_counter++;
    }
  }

  if( peer_counter > 0 )
    qDebug() << peer_counter << "file transfer peers removed for user" << user_id;
}

/* Inline file transfer peers (encrypted message channel, Bluetooth path):
 * they live in the same m_peers list of the 6476 peers so the progress and
 * cancel routing from Core and GuiFileTransfer needs no changes at all. */

InlineFileTransferPeer* FileTransfer::createInlinePeer( VNumber remote_user_id, FileInfo::TransferType transfer_type, const FileInfo& fi )
{
  InlineFileTransferPeer* inline_peer = new InlineFileTransferPeer( this );
  inline_peer->setTransferType( transfer_type );
  inline_peer->setId( Protocol::instance().newId() );
  inline_peer->setRemoteUserId( remote_user_id );
  inline_peer->setFileInfo( transfer_type, fi );

  connect( inline_peer, SIGNAL( message( VNumber, VNumber, const FileInfo&, const QString&, FileTransferPeer::TransferState ) ), this, SIGNAL( message( VNumber, VNumber, const FileInfo&, const QString&, FileTransferPeer::TransferState ) ) );
  connect( inline_peer, SIGNAL( progress( VNumber, VNumber, const FileInfo&, FileSizeType, qint64 ) ), this, SIGNAL( progress( VNumber, VNumber, const FileInfo&, FileSizeType, qint64 ) ) );
  connect( inline_peer, SIGNAL( operationCompleted() ), this, SLOT( deletePeer() ) );

  m_peers.append( inline_peer );
  return inline_peer;
}

void FileTransfer::startInlineUpload( VNumber to_user_id, const FileInfo& fi, Connection* message_connection )
{
  InlineFileTransferPeer* inline_peer = createInlinePeer( to_user_id, FileInfo::Upload, fi );
  // The offer (BEE-FILE + InlineFile) has already been sent on the message
  // connection by Core::sendFileToUser(): the peer queues and waits for GO.
  inline_peer->setupInlineUpload( to_user_id, fi, message_connection );
}

bool FileTransfer::startInlineDownload( VNumber from_user_id, const FileInfo& fi, Connection* message_connection )
{
  InlineFileTransferPeer* inline_peer = createInlinePeer( from_user_id, FileInfo::Download, fi );
  return inline_peer->startInlineDownload( from_user_id, fi, message_connection );
}

void FileTransfer::readyForInlineUpload( VNumber from_user_id, const FileInfo& fi )
{
  InlineFileTransferPeer* inline_peer = inlinePeer( from_user_id, fi.id() );
  if( !inline_peer )
  {
    qWarning() << "File Transfer has no inline upload peer for file id" << fi.id() << "of user" << from_user_id;
    return;
  }
  inline_peer->onUploadAccepted();
}

void FileTransfer::parseInlineFileChunk( VNumber from_user_id, const Message& m )
{
  VNumber file_info_id = ID_INVALID;
  quint32 chunk_index = 0;
  quint32 total_chunks = 0;
  FileSizeType byte_offset = 0;
  FileSizeType chunk_size = 0;
  QByteArray chunk_bytes;

  if( !Protocol::instance().fileChunkFromMessage( m, &file_info_id, &chunk_index, &total_chunks, &byte_offset, &chunk_size, &chunk_bytes ) )
  {
    qWarning() << "File Transfer has received an invalid inline file chunk from user" << from_user_id << "and drops it";
    return;
  }

  InlineFileTransferPeer* inline_peer = inlinePeer( from_user_id, file_info_id );
  if( !inline_peer )
  {
#ifdef BEEBEEP_DEBUG
    qDebug() << "File Transfer has no inline peer for chunk of file id" << file_info_id << "from user" << from_user_id << "(stale or canceled transfer)";
#endif
    return;
  }

  inline_peer->parseFileChunk( m );
}

void FileTransfer::cancelInlineTransfer( VNumber user_id, const FileInfo& fi, bool notify_remote )
{
  InlineFileTransferPeer* inline_peer = inlinePeer( user_id, fi.id() );
  if( !inline_peer )
    return;

  inline_peer->cancelInlineTransfer( notify_remote );
  if( inline_peer->isDownload() )
    inline_peer->removePartiallyDownloadedFile();
}

InlineFileTransferPeer* FileTransfer::inlinePeer( VNumber remote_user_id, VNumber file_info_id ) const
{
  foreach( FileTransferPeer* transfer_peer, m_peers )
  {
    InlineFileTransferPeer* inline_peer = qobject_cast<InlineFileTransferPeer*>( transfer_peer );
    if( inline_peer && inline_peer->remoteUserId() == remote_user_id && inline_peer->fileInfo().id() == file_info_id )
      return inline_peer;
  }
  return Q_NULLPTR;
}

void FileTransfer::cancelPeerTransfer( FileTransferPeer* transfer_peer )
{
  InlineFileTransferPeer* inline_peer = qobject_cast<InlineFileTransferPeer*>( transfer_peer );
  if( inline_peer )
    inline_peer->cancelInlineTransfer();
  else
    transfer_peer->cancelTransfer();
}

FileInfo FileTransfer::fileInfo( VNumber file_id ) const
{
  QList<FileInfo>::const_iterator it = m_files.begin();
  while( it != m_files.end() )
  {
    if( (*it).id() == file_id )
      return *it;
    ++it;
  }
  return FileInfo();
}

FileInfo FileTransfer::fileInfo( const QString& file_absolute_path, const QString chat_private_id ) const
{
  QList<FileInfo>::const_iterator it = m_files.begin();
  while( it != m_files.end() )
  {
    if( (*it).path() == file_absolute_path && (*it).chatPrivateId() == chat_private_id )
      return *it;
    ++it;
  }
  return FileInfo();
}

void FileTransfer::removeFile( const QString& file_path )
{
  QList<FileInfo>::iterator it = m_files.begin();
  while( it != m_files.end() )
  {
    if( (*it).path() == file_path )
      it = m_files.erase( it );
    else
      ++it;
  }
}

FileInfo FileTransfer::addFile( const QFileInfo& fi, const QString& share_folder, bool to_share_box, const QString& chat_private_id, FileInfo::ContentType content_type, qint64 message_duration )
{
  FileInfo file_info = fileInfo( fi.absoluteFilePath(), chat_private_id );
  if( file_info.isValid() )
  {
    QString file_hash = Protocol::instance().fileInfoHash( fi );
    if( file_info.fileHash() == file_hash )
      return file_info;
    else
      removeFile( fi.absoluteFilePath() );
  }

  file_info = Protocol::instance().fileInfo( fi, share_folder, to_share_box, chat_private_id, content_type );
  file_info.setHostAddress( Settings::instance().localUser().networkAddress().hostAddress() );
  file_info.setHostPort( serverPort() );
  file_info.setDuration( message_duration );
  m_files.append( file_info );
  return file_info;
}

void FileTransfer::addFileInfoList( const QList<FileInfo>& file_info_list )
{
  foreach( FileInfo fi, file_info_list )
  {
    if( !fileInfo( fi.id() ).isValid() )
      m_files.append( fi );
  }
}

void FileTransfer::incomingConnection( qintptr socket_descriptor )
{
  FileTransferPeer *upload_peer = new FileTransferPeer( this );
  upload_peer->setTransferType( FileInfo::Upload );
  upload_peer->setId( Protocol::instance().newId() );
  m_peers.append( upload_peer );
  setupPeer( upload_peer, socket_descriptor, serverPort() );
}

void FileTransfer::setupPeer( FileTransferPeer* transfer_peer, qintptr socket_descriptor, quint16 server_port )
{
  if( transfer_peer->isInQueue() )
  {
#ifdef BEEBEEP_DEBUG
    qDebug() << qPrintable( transfer_peer->name() ) << "is removed from queue";
#endif
    transfer_peer->removeFromQueue();
  }

#ifdef BEEBEEP_DEBUG
  qDebug() << qPrintable( transfer_peer->name() ) << "starts its connection. Active downloads:" << activeDownloads();
#endif
  if( !transfer_peer->isDownload() )
  {
    connect( transfer_peer, SIGNAL( fileUploadRequest( const FileInfo& ) ), this, SLOT( checkUploadRequest( const FileInfo& ) ) );
    connect( transfer_peer, SIGNAL( message( VNumber, VNumber, const FileInfo&, const QString&, FileTransferPeer::TransferState ) ), this, SIGNAL( message( VNumber, VNumber, const FileInfo&, const QString&, FileTransferPeer::TransferState ) ) );
  }

  connect( transfer_peer, SIGNAL( progress( VNumber, VNumber, const FileInfo&, FileSizeType, qint64 ) ), this, SIGNAL( progress( VNumber, VNumber, const FileInfo&, FileSizeType, qint64 ) ) );
  connect( transfer_peer, SIGNAL( operationCompleted() ), this, SLOT( deletePeer() ) );

  transfer_peer->setConnectionDescriptor( socket_descriptor, server_port );
  int delay = Random::number32( 1, 9 ) * 100;
#ifdef BEEBEEP_DEBUG
  qDebug() << qPrintable( transfer_peer->name() ) << "starts in" << delay << "ms";
#endif
  QTimer::singleShot( delay, transfer_peer, SLOT( startConnection() ) );
}


void FileTransfer::checkUploadRequest( const FileInfo& file_info_to_check )
{
#ifdef BEEBEEP_DEBUG
  qDebug() << "Checking upload request:" << file_info_to_check.id() << file_info_to_check.password();
  qDebug() << "FileInfo name:" << file_info_to_check.name();
  qDebug() << "FileInfo path:" << file_info_to_check.path();
  qDebug() << "FileInfo share folder:" << file_info_to_check.shareFolder();
  qDebug() << "FileInfo is share box:" << file_info_to_check.isInShareBox();
  qDebug() << "FileInfo is folder:" << file_info_to_check.isFolder();
#endif

  FileTransferPeer *upload_peer = qobject_cast<FileTransferPeer*>( sender() );
  if( !upload_peer )
  {
    qWarning() << "File Transfer server received a signal from invalid upload peer for file" << qPrintable( file_info_to_check.name() );
    return;
  }

  if( !Settings::instance().enableFileTransfer() )
  {
    qWarning() << "File Transfer is disabled: unable to upload file" << qPrintable( file_info_to_check.name() );
    upload_peer->cancelTransfer();
    return;
  }

  if( !Settings::instance().isFileExtensionAllowedInFileTransfer( file_info_to_check.suffix() ) )
  {
    qWarning() << "File Transfer is not allowed to upload file" << qPrintable( file_info_to_check.name() ) << "with extension" << file_info_to_check.suffix();
    upload_peer->cancelTransfer();
    return;
  }

  FileInfo file_info;

  if( file_info_to_check.isInShareBox() )
  {
    if( !Settings::instance().enableFileSharing() )
    {
      qWarning() << "BeeBOX file upload request refused (file sharing disabled):" << file_info_to_check.name();
      upload_peer->cancelTransfer();
      return;
    }

    if( !Settings::instance().useShareBox() )
    {
      qWarning() << "BeeBOX file upload request refused (sharebox disabled):" << file_info_to_check.name();
      upload_peer->cancelTransfer();
      return;
    }

    file_info = fileInfo( file_info_to_check.id() );
    if( !file_info.isValid() )
    {
      QString file_path;
      if( file_info_to_check.shareFolder().isEmpty() )
        file_path = QString( "%1/%2" ).arg( Settings::instance().shareBoxPath() )
                                      .arg( file_info_to_check.name() );
      else
        file_path = QString( "%1/%2/%3" ).arg( Settings::instance().shareBoxPath() )
                                         .arg( file_info_to_check.shareFolder() )
                                         .arg( file_info_to_check.name() );

      QFileInfo share_box_file_info( file_path );
      if( !share_box_file_info.exists() )
      {
        qWarning() << "BeeBOX file upload request refused (not exists):" << file_path;
        upload_peer->cancelTransfer();
      }

      if( !share_box_file_info.isReadable() )
      {
        qWarning() << "BeeBOX file upload request refused (not readable):" << file_path;
        upload_peer->cancelTransfer();
      }

      file_info = Protocol::instance().fileInfo( share_box_file_info, "", true, "", FileInfo::File );
    }
  }
  else
  {
    file_info = fileInfo( file_info_to_check.id() );

    if( !file_info.isValid() && !Settings::instance().disableFileSharing() )
    {
      // Now check file sharing
      file_info = FileShare::instance().localFileInfo( file_info_to_check.id() );
    }

    if( !file_info.isValid() )
    {
      qWarning() << "File Transfer server received a request of a file not in list (or file sharing is disabled)";
      upload_peer->cancelTransfer();
      return;
    }

    if( file_info.password() != file_info_to_check.password() )
    {
      qWarning() << "File Transfer server received a request for the file" << file_info.name() << "but with the wrong password";
      upload_peer->cancelTransfer();
      return;
    }
  }

  if( file_info_to_check.startingPosition() == file_info.size() )
  {
    upload_peer->skipTransfer();
    return;
  }

  // A new generation downloader always resumes, and its pause/resume must not
  // depend on a setting of this user (LEGACY_BRIDGE.md section 9, item 9).
  // Toward any other peer the setting keeps exactly its current meaning.
  if( Settings::instance().resumeFileTransfer() || upload_peer->isNewGenerationPeer() )
    file_info.setStartingPosition( file_info_to_check.startingPosition() );
  else
    file_info.setStartingPosition( 0 );
  upload_peer->startUpload( file_info );
}

void FileTransfer::downloadFile( VNumber from_user_id, const FileInfo& fi )
{
  FileTransferPeer *download_peer = new FileTransferPeer( this );
  download_peer->setTransferType( FileInfo::Download );
  download_peer->setId( Protocol::instance().newId() );
  download_peer->setFileInfo( FileInfo::Download, fi );
  download_peer->setRemoteUserId( from_user_id );

  connect( download_peer, SIGNAL( message( VNumber, VNumber, const FileInfo&, const QString&, FileTransferPeer::TransferState ) ), this, SIGNAL( message( VNumber, VNumber, const FileInfo&, const QString&, FileTransferPeer::TransferState ) ) );
  // connect before setInQueue to send message to GUI
  download_peer->setInQueue();

  m_peers.append( download_peer );

  if( activeDownloads() < Settings::instance().maxSimultaneousDownloads() )
    setupPeer( download_peer, 0 );
}

FileTransferPeer* FileTransfer::peer( VNumber peer_id ) const
{
  QList<FileTransferPeer*>::const_iterator it = m_peers.begin();
  while( it != m_peers.end() )
  {
    if( (*it)->id() == peer_id )
      return *it;
    ++it;
  }
  return Q_NULLPTR;
}

void FileTransfer::deletePeer()
{
  if( !sender() )
  {
    qWarning() << "File Transfer is unable to find peer sender of signal destroyed(). List become invalid";
    return;
  }

  FileTransferPeer* sender_peer = dynamic_cast<FileTransferPeer*>( sender() );
  if( m_peers.removeOne( sender_peer ) )
  {
    qDebug() << "Removed peer from list." << m_peers.size() << "peers remained";
    sender_peer->deleteLater();
  }

  if( isListening() && downloadsInQueue() > 0 )
    QTimer::singleShot( 0, this, SLOT( startNewDownload() ) ); // to next main loop avoiding a crash
}

bool FileTransfer::cancelTransfer( VNumber peer_id )
{
  FileTransferPeer* transfer_peer = peer( peer_id );
  if( transfer_peer )
  {
    cancelPeerTransfer( transfer_peer );
    if( transfer_peer->isDownload() )
      transfer_peer->removePartiallyDownloadedFile();
    return true;
  }
  qWarning() << "File Transfer server cannot cancel the file transfer because it has not found the peer" << peer_id;
  return false;
}

bool FileTransfer::pauseTransfer( VNumber peer_id )
{
  FileTransferPeer* transfer_peer = peer( peer_id );
  if( transfer_peer )
  {
    if( transfer_peer->canPauseTransfer() )
    {
      transfer_peer->pauseTransfer( false );
      return true;
    }
    else
      return false;
  }
  qWarning() << "File Transfer server cannot pause the file transfer because it has not found the peer" << peer_id;
  return false;
}

int FileTransfer::activeDownloads() const
{
  int active_downloads = 0;
  foreach( FileTransferPeer* transfer_peer, m_peers )
  {
    if( transfer_peer->isDownload() && transfer_peer->isActive() )
      active_downloads++;
  }
  return active_downloads;
}

int FileTransfer::downloadsInQueue() const
{
  int downloads_in_queue = 0;
  foreach( FileTransferPeer* transfer_peer, m_peers )
  {
    if( transfer_peer->isDownload() && transfer_peer->isInQueue() )
      downloads_in_queue++;
  }
  return downloads_in_queue;
}

FileTransferPeer* FileTransfer::nextDownloadInQueue() const
{
  foreach( FileTransferPeer* transfer_peer, m_peers )
  {
    if( transfer_peer->isDownload() && transfer_peer->isInQueue() )
    {
      if( transfer_peer->remoteUserId() != ID_INVALID )
      {
        User remote_user = UserManager::instance().findUser( transfer_peer->remoteUserId() );
        if( remote_user.isValid() && remote_user.isStatusConnected() )
          return transfer_peer;
      }
    }
  }
  return Q_NULLPTR;
}

void FileTransfer::startNewDownload()
{
  if( activeDownloads() >= Settings::instance().maxSimultaneousDownloads() )
    return;

  FileTransferPeer* download_peer = nextDownloadInQueue();
  if( !download_peer )
    return;

#ifdef BEEBEEP_DEBUG
  qDebug() << download_peer->name() << "is removed from queue and started";
#endif

  setupPeer( download_peer, 0 );
}

void FileTransfer::onTickEvent( int ticks )
{
  if( !isListening() )
    return;

  foreach( FileTransferPeer* transfer_peer, m_peers )
    transfer_peer->onTickEvent( ticks );
}
