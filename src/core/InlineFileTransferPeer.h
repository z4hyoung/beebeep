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
#include "InlineFileFrame.h"
#include <QCryptographicHash>
#include <QTimer>
#include <QSaveFile>

class Connection;


/*! \brief File transfer over the encrypted message connection.
 * Both peers negotiate InlineFileV2 in the offer and acceptance. V2 uses
 * bounded binary Zstd/raw frames and confirms completion only after SHA-256
 * verification and atomic destination commit. Legacy peers retain the
 * Base64 FileChunk protocol with an eight-chunk sliding window.
 * Pause is unsupported; cancellation uses the existing refused File message.
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

  /*! GO received: selects the negotiated protocol and starts uploading. */
  void onUploadAccepted( bool version2 = false );

  /*! Routes a Message::FileChunk: data chunk when downloading, ACK when uploading. */
  void parseFileChunk( const Message& );

  /*! Cancels the transfer and cleans up. With notify_remote it also tells the
   * remote peer with the refused BEE-FILE message (Message::Refused pattern):
   * it is skipped when the cancellation is the answer to a remote refusal,
   * otherwise the message would bounce back and forth. */
  void cancelInlineTransfer( bool notify_remote = true );

protected:
  bool sendChunkMessage();
  bool sendSingleChunk();
  bool sendChunkAck( quint32 chunk_index, FileSizeType byte_offset, FileSizeType chunk_size );
  void checkUploadAck( quint32 chunk_index, quint32 total_chunks, FileSizeType byte_offset, FileSizeType chunk_size );
  void checkDownloadChunk( quint32 chunk_index, quint32 total_chunks, FileSizeType byte_offset, FileSizeType chunk_size, const QByteArray& chunk_bytes );
  void checkDownloadCompleted();
  void sendRefusedToRemote();
  void sendGoMessage();
  void detachFromConnection();

protected slots:
  void onInlineConnectionClosed();
  void onInlineAcceptTimeout();

protected:
  void startV2();
  void sendV2Data();
  void receiveV2( const QByteArray& );
  bool sendV2( const InlineFileFrame& );
  void failV2( const QString&, bool notify = true );
  void verifyV2();
  bool m_version2 = false;
  bool m_finishSent = false;
  bool m_verifying = false;
  quint64 m_sentOffset = 0;
  quint64 m_ackedOffset = 0;
  QCryptographicHash m_contentHash{ QCryptographicHash::Sha256 };
  QByteArray m_expectedHash;
  QSaveFile m_verifiedFile;
  QTimer m_v2Timeout;
  Connection* mp_connection;
  quint32 m_chunkIndex;   // uploader: next chunk to SEND (window can run ahead)
  quint32 m_ackIndex;     // uploader: next chunk ACK we are waiting for
  quint32 m_totalChunks;

};

#endif // BEEBEEP_INLINEFILETRANSFERPEER_H
