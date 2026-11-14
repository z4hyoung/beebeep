#ifndef YONGBEEP_INLINEFILEFRAME_H
#define YONGBEEP_INLINEFILEFRAME_H

#include "Config.h"

// Binary frames inside the existing encrypted, length-delimited connection.
// Compression is per frame. No Base64 or outer zlib compression is needed.
struct InlineFileFrame
{
  enum Kind { Data = 1, Ack, Finish, Complete, Failure };
  static const int MaxBlockSize = 65536;
  Kind kind = Data;
  VNumber fileId = ID_INVALID;
  quint64 offset = 0;
  quint32 rawSize = 0;
  bool compressed = false;
  QByteArray payload;

  static QByteArray magic();
  QByteArray encode() const;
  static bool decode( const QByteArray&, InlineFileFrame* );
  static InlineFileFrame data( VNumber, quint64, const QByteArray&, int level = 6 );
  bool unpack( QByteArray* ) const;
};

#endif
