//////////////////////////////////////////////////////////////////////
//
// YongBEEP Copyright (C) 2010-2026 Marco Mastroddi (original BeeBEEP)
// Bluetooth inline file transfer adaptation for YongBEEP.
//
// This file is part of YongBEEP, a derivative work of BeeBEEP
// (https://sourceforge.net/projects/beebeep).  Same GNU GPL v3 license.
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_INLINEFILETRANSFERPEER_H
#define BEEBEEP_INLINEFILETRANSFERPEER_H

#include "FileTransferPeer.h"

class Connection;


/*! \brief File transfer peer over the encrypted message channel (Bluetooth path)
 *
 * Implements the inline file transfer of docs/02-技术设计.md section 2.3 and
 * docs/analysis/core-transport.md section E.2: files up to
 * Settings::maxInlineFileSizeBytes() (default 5 MB) are split in 64 KiB
 * base64 chunks carried by Message::FileChunk over the existing (already
 * encrypted) message connection, so a pure Bluetooth RFCOMM link without any
 * IP network can send and receive files. The LAN TCP 6476 channel keeps its
 * original code path.
 *
 * It is a FileTransferPeer (kept in FileTransfer::m_peers unified list) so
 * GuiFileTransfer progress bars, cancel and the Core::fileTransferProgress /
 * fileTransferMessage signals work with zero GUI changes.
 *
 * Flow control is stop-and-wait, mirroring the 6476 channel: the receiver
 * answers every chunk with Message::FileChunk + Message::Request (see
 * Protocol::fileChunkAckToMessage) and only then the sender sends the next
 * chunk. Pause is not implemented yet (a GUI pause request falls back to
 * cancel, see FileTransfer::pauseTransfer); cancellation uses the existing
 * Message::File + Message::Refused pattern carrying the FileInfo back.
 */
class InlineFileTransferPeer : public FileTransferPeer
{
  Q_OBJECT

public:
  explicit InlineFileTransferPeer( QObject* parent = Q_NULLPTR );

  /*! Creates an upload peer waiting in queue for the remote acceptance (GO). */
  void setupInlineUpload( VNumber to_user_id, const FileInfo&, Connection* );

  /*! Opens the temporary .part file and sends the GO message (BEE-FILE + InlineFile + Request). */
  bool startInlineDownload( VNumber from_user_id, const FileInfo&, Connection* );

  /*! GO received: starts the stop-and-wait chunk loop. */
  void onUploadAccepted();

  /*! Routes a Message::FileChunk: data chunk when downloading, ACK when uploading. */
  void parseFileChunk( const Message& );

  /*! Cancels the transfer and cleans up. With notify_remote it also tells the
   * remote peer with the refused BEE-FILE message (Message::Refused pattern):
   * it is skipped when the cancellation is the answer to a remote refusal,
   * otherwise the message would bounce back and forth. */
  void cancelInlineTransfer( bool notify_remote = true );

protected:
  bool sendChunkMessage();
  bool sendChunkAck( quint32 chunk_index, FileSizeType byte_offset, FileSizeType chunk_size );
  void checkUploadAck( quint32 chunk_index, quint32 total_chunks, FileSizeType byte_offset, FileSizeType chunk_size );
  void checkDownloadChunk( quint32 chunk_index, quint32 total_chunks, FileSizeType byte_offset, FileSizeType chunk_size, const QByteArray& chunk_bytes );
  void checkDownloadCompleted();
  void sendRefusedToRemote();
  void sendGoMessage();
  void detachFromConnection();

protected slots:
  void onInlineConnectionClosed();

protected:
  Connection* mp_connection;
  quint32 m_chunkIndex;
  quint32 m_totalChunks;

};

#endif // BEEBEEP_INLINEFILETRANSFERPEER_H
