#include "InlineFileFrame.h"
#include "../thirdparty/zstd/zstd.h"
#include <QDataStream>

QByteArray InlineFileFrame::magic()
{
  return QByteArray::fromHex( "ff594246320d0a00" );
}

QByteArray InlineFileFrame::encode() const
{
  QByteArray bytes = magic();
  QDataStream stream( &bytes, QIODevice::Append );
  stream.setVersion( QDataStream::Qt_5_0 );
  stream << quint8( kind ) << quint8( compressed ) << quint64( fileId )
         << offset << rawSize << quint32( payload.size() );
  stream.writeRawData( payload.constData(), payload.size() );
  return bytes;
}

bool InlineFileFrame::decode( const QByteArray& bytes, InlineFileFrame* frame )
{
  if( !bytes.startsWith( magic() ) || bytes.size() < 34 || bytes.size() > MaxBlockSize + 50 )
    return false;
  QDataStream stream( bytes.mid( 8 ) );
  stream.setVersion( QDataStream::Qt_5_0 );
  quint8 kind, compressed;
  quint64 file_id;
  quint32 payload_size;
  stream >> kind >> compressed >> file_id >> frame->offset >> frame->rawSize >> payload_size;
  if( stream.status() != QDataStream::Ok || kind < Data || kind > Failure || compressed > 1
      || file_id == ID_INVALID || frame->rawSize > MaxBlockSize || payload_size > MaxBlockSize
      || bytes.size() < 34 + int( payload_size ) || bytes.size() - 34 - int( payload_size ) > 15 )
    return false;
  const QByteArray padding = bytes.mid( 34 + payload_size );
  if( padding != QByteArray( padding.size(), ' ' ) )
    return false;
  frame->kind = Kind( kind );
  frame->compressed = compressed;
  frame->fileId = file_id;
  frame->payload = bytes.mid( 34, payload_size );
  if( kind == Data )
    return frame->rawSize > 0 && payload_size > 0;
  if( compressed || frame->rawSize != 0 )
    return false;
  if( kind == Finish || kind == Complete )
    return payload_size == 32;
  return kind == Failure ? payload_size <= 1024 : payload_size == 0;
}

InlineFileFrame InlineFileFrame::data( VNumber file_id, quint64 offset, const QByteArray& raw, int level )
{
  InlineFileFrame frame;
  frame.fileId = file_id;
  frame.offset = offset;
  frame.rawSize = raw.size();
  frame.payload = raw;
  if( raw.isEmpty() || raw.size() > MaxBlockSize )
    return frame;
  QByteArray packed( int( ZSTD_compressBound( raw.size() ) ), Qt::Uninitialized );
  size_t size = ZSTD_compress( packed.data(), packed.size(), raw.constData(), raw.size(), level );
  if( !ZSTD_isError( size ) && size + 32 < size_t( raw.size() ) )
  {
    packed.resize( int( size ) );
    frame.payload = packed;
    frame.compressed = true;
  }
  return frame;
}

bool InlineFileFrame::unpack( QByteArray* raw ) const
{
  if( kind != Data || rawSize == 0 || rawSize > MaxBlockSize )
    return false;
  if( !compressed )
  {
    *raw = payload;
    return raw->size() == int( rawSize );
  }
  raw->resize( rawSize );
  size_t size = ZSTD_decompress( raw->data(), raw->size(), payload.constData(), payload.size() );
  return !ZSTD_isError( size ) && size == rawSize;
}
