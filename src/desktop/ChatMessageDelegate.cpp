//////////////////////////////////////////////////////////////////////
//
// YongBEEP — chat message delegate (S4e chatview-v2)
//
//////////////////////////////////////////////////////////////////////

#include "ChatMessageDelegate.h"
#include "BeeUtils.h"
#include "ChatMessageModel.h"
#include "Settings.h"
#include "YongTheme.h"

#include <QAbstractTextDocumentLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextDocument>
#include <QtDebug>


ChatMessageDelegate::ChatMessageDelegate( QObject* parent )
  : QStyledItemDelegate( parent )
{
}

ChatMessageItem* ChatMessageDelegate::itemForIndex( const QModelIndex& index ) const
{
  const ChatMessageModel* model = messageModel( index );
  return model ? model->itemForIndex( index ) : Q_NULLPTR;
}

const ChatMessageModel* ChatMessageDelegate::messageModel( const QModelIndex& index ) const
{
  return qobject_cast<const ChatMessageModel*>( index.model() );
}

// ---------------------------------------------------------------------
// Geometry (shared by paint / sizeHint / editorEvent)
// ---------------------------------------------------------------------

QRect ChatMessageDelegate::avatarRect( const ChatMessageItem* item, const QRect& row_rect, qreal content_top ) const
{
  const int avatar_size = YongTheme::avatarSizePx();
  if( item->ownMessage )
    return QRect( row_rect.right() - ChatMessageModel::HorizontalMargin - avatar_size, qRound( content_top ), avatar_size, avatar_size );
  return QRect( row_rect.left() + ChatMessageModel::HorizontalMargin, qRound( content_top ), avatar_size, avatar_size );
}

QRectF ChatMessageDelegate::bubbleRect( const ChatMessageItem* item, const QRect& row_rect, qreal content_top, int row_width ) const
{
  const qreal bubble_w = item->docSize.width() + 2.0 * ChatMessageModel::BubbleHPad;
  const qreal bubble_h = item->docSize.height() + 2.0 * ChatMessageModel::BubbleVPad;
  if( item->ownMessage )
  {
    const qreal bubble_right = row_width - ChatMessageModel::HorizontalMargin - YongTheme::avatarSizePx() - ChatMessageModel::AvatarGap;
    return QRectF( bubble_right - bubble_w, content_top, bubble_w, bubble_h );
  }
  const qreal bubble_x = ChatMessageModel::HorizontalMargin + YongTheme::avatarSizePx() + ChatMessageModel::AvatarGap;
  return QRectF( bubble_x, content_top, bubble_w, bubble_h );
}

QRectF ChatMessageDelegate::documentRect( const ChatMessageItem* item, const QRect& row_rect, qreal content_top, int row_width ) const
{
  Q_UNUSED( row_rect );
  return bubbleRect( item, QRect( 0, 0, row_width, 0 ), content_top, row_width )
           .adjusted( ChatMessageModel::BubbleHPad, ChatMessageModel::BubbleVPad, -ChatMessageModel::BubbleHPad, -ChatMessageModel::BubbleVPad );
}

QRectF ChatMessageDelegate::fileCardRect( const ChatMessageItem* item, const QRect& row_rect, qreal content_top, int row_width ) const
{
  Q_UNUSED( item );
  Q_UNUSED( row_rect );
  const qreal card_x = ( row_width - ChatMessageModel::FileCardWidth ) / 2.0;
  return QRectF( card_x, content_top, ChatMessageModel::FileCardWidth, ChatMessageModel::FileCardHeight );
}

QUrl ChatMessageDelegate::anchorInItem( const ChatMessageItem* item, const QRect& row_rect, int row_width, const QPointF& row_pos ) const
{
  if( !item || !item->doc )
    return QUrl();

  if( item->kind == ChatMessageItem::FileCard )
  {
    qreal content_top = row_rect.top() + ( item->showTimestamp ? ChatMessageModel::TimestampRowHeight : 0 );
    if( fileCardRect( item, row_rect, content_top, row_width ).contains( row_pos ) && !item->fileUrl.isEmpty() )
      return QUrl( item->fileUrl );
    return QUrl();
  }

  if( item->kind == ChatMessageItem::UserMessage )
  {
    qreal y = row_rect.top();
    if( item->showTimestamp )
      y += ChatMessageModel::TimestampRowHeight;
    if( item->showName )
      y += ChatMessageModel::NameRowHeight;
    QRectF doc_rect = documentRect( item, row_rect, y, row_width );
    if( doc_rect.contains( row_pos ) )
    {
      QString anchor = item->doc->documentLayout()->anchorAt( row_pos - doc_rect.topLeft() );
      if( !anchor.isEmpty() )
        return QUrl( anchor );
    }
    return QUrl();
  }

  // system lines / free-form documents are drawn at the horizontal margin
  QPointF doc_pos = row_pos - QPointF( ChatMessageModel::HorizontalMargin, row_rect.top() );
  if( doc_pos.x() >= 0 && doc_pos.y() >= 0 )
  {
    QString anchor = item->doc->documentLayout()->anchorAt( doc_pos );
    if( !anchor.isEmpty() )
      return QUrl( anchor );
  }
  return QUrl();
}

QUrl ChatMessageDelegate::anchorAt( const QModelIndex& index, const QRect& row_rect, const QPoint& viewport_pos ) const
{
  ChatMessageItem* item = itemForIndex( index );
  if( !item )
    return QUrl();
  QPointF row_pos = QPointF( viewport_pos ) - row_rect.topLeft();
  return anchorInItem( item, row_rect, row_rect.width(), row_pos );
}

// ---------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------

void ChatMessageDelegate::drawStatusLine( QPainter* painter, const QString& status_text, const QRectF& line_rect, const QColor& base_color ) const
{
  // file transfer status: keep the base gray but color the check/x marks
  const QString check_mark = QString::fromUtf8( "\xE2\x9C\x93" );
  const QString ballot_x = QString::fromUtf8( "\xE2\x9C\x97" );
  QFont font = painter->font();
  font.setPixelSize( YongTheme::fontTimestampPx() );
  painter->setFont( font );
  QFontMetrics fm( font );
  qreal x = line_rect.left();
  qreal y = line_rect.top();
  QString segment;
  QColor segment_color = base_color;
  auto flush_segment = [ & ]()
  {
    if( !segment.isEmpty() )
    {
      painter->setPen( segment_color );
      painter->drawText( QPointF( x, y + fm.ascent() ), segment );
      x += fm.horizontalAdvance( segment );
      segment.clear();
      segment_color = base_color;
    }
  };
  foreach( const QChar& c, status_text )
  {
    QColor char_color;
    if( c == check_mark.at( 0 ) )
      char_color = YongTheme::brandGreen();
    else if( c == ballot_x.at( 0 ) )
      char_color = YongTheme::unreadRed();
    if( char_color != segment_color )
    {
      flush_segment();
      segment_color = char_color.isValid() ? char_color : base_color;
    }
    segment.append( c );
  }
  flush_segment();
}

void ChatMessageDelegate::paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
  ChatMessageItem* item = itemForIndex( index );
  const ChatMessageModel* model = messageModel( index );
  if( !item || !model )
    return;
  if( item->kind == ChatMessageItem::UserMessage || item->kind == ChatMessageItem::SystemLine || item->kind == ChatMessageItem::FreeFormDoc )
    model->ensureLayout( item );

  const bool dark = Settings::instance().useDarkStyle();
  const int row_width = option.rect.width();
  painter->save();
  painter->setRenderHint( QPainter::Antialiasing, true );
  painter->setClipRect( option.rect );

  // --- centered timestamp separator (WeChat gray) ---
  if( item->showTimestamp )
  {
    QFont ts_font = option.font;
    ts_font.setPixelSize( YongTheme::fontTimestampPx() );
    painter->setFont( ts_font );
    painter->setPen( dark ? QColor( Settings::instance().chatSystemTextColor() ) : YongTheme::textSecondary() );
    painter->drawText( QRect( 0, option.rect.top(), row_width, ChatMessageModel::TimestampRowHeight ),
                       Qt::AlignHCenter | Qt::AlignVCenter, item->timestampText );
  }

  if( item->kind == ChatMessageItem::FileCard )
  {
    const qreal content_top = option.rect.top() + ( item->showTimestamp ? ChatMessageModel::TimestampRowHeight : 0 );
    QRectF card_rect = fileCardRect( item, option.rect, content_top, row_width );

    // WeChat file card: white rounded block + 1px border
    QPainterPath card_path;
    card_path.addRoundedRect( card_rect, YongTheme::bubbleRadiusPx(), YongTheme::bubbleRadiusPx() );
    painter->setPen( QPen( dark ? QColor( 0x2a, 0x2a, 0x2a ) : YongTheme::separator(), 1.0 ) );
    painter->setBrush( dark ? QColor( 0x3c, 0x40, 0x48 ) : YongTheme::bubbleWhite() );
    painter->drawPath( card_path );

    if( !item->fileIcon.isNull() )
      painter->drawPixmap( QRectF( card_rect.left() + 14, card_rect.center().y() - 16, 32, 32 ).toRect(),
                           item->fileIcon.pixmap( QSize( 32, 32 ) ) );

    QFont name_font = option.font;
    name_font.setPixelSize( 13 );
    QFontMetrics name_fm( name_font );
    painter->setFont( name_font );
    painter->setPen( dark ? QColor( 0xff, 0xff, 0xff ) : YongTheme::textPrimary() );
    QString file_name = name_fm.elidedText( item->fileName, Qt::ElideMiddle,
                                            card_rect.width() - 14 - 32 - 12 - 10 );
    painter->drawText( QPointF( card_rect.left() + 60, card_rect.top() + 25 ), file_name );

    drawStatusLine( painter, item->fileStatus,
                    QRectF( card_rect.left() + 60, card_rect.top() + 29, card_rect.width() - 70, 14 ),
                    dark ? QColor( 0xaa, 0xaa, 0xaa ) : YongTheme::textSecondary() );

    painter->restore();
    return;
  }

  if( item->kind == ChatMessageItem::SystemLine || item->kind == ChatMessageItem::FreeFormDoc )
  {
    // centered gray system line / wide free-form document (saved history,
    // image previews): drawn directly on the chat background
    painter->translate( ChatMessageModel::HorizontalMargin, option.rect.top() );
    item->doc->drawContents( painter, QRectF( 0, 0, model->viewportWidth() - 2 * ChatMessageModel::HorizontalMargin, item->docSize.height() ) );
    painter->restore();
    return;
  }

  // --- user message bubble ---
  qreal y = option.rect.top();
  if( item->showTimestamp )
    y += ChatMessageModel::TimestampRowHeight;
  if( item->showName )
  {
    QFont name_font = option.font;
    name_font.setPixelSize( YongTheme::fontAuxPx() );
    painter->setFont( name_font );
    QColor name_color = Settings::instance().chatSystemTextColor();
    if( !dark )
      name_color = YongTheme::textSecondary();
    if( Settings::instance().chatUseColoredUserNames() && Bee::isColorVisibleInChat( QColor( item->user.color() ) ) )
      name_color = QColor( item->user.color() );
    painter->setPen( name_color );
    QRectF bubble_top_rect = bubbleRect( item, option.rect, y, row_width );
    painter->drawText( QRectF( bubble_top_rect.left(), y, bubble_top_rect.width(), ChatMessageModel::NameRowHeight ),
                       Qt::AlignLeft | Qt::AlignVCenter, item->nameText );
    y += ChatMessageModel::NameRowHeight;
  }

  // avatar (40px rounded-6 square, outer edge)
  QRect avatar_rect = avatarRect( item, option.rect, y );
  if( !item->avatar.isNull() )
    painter->drawPixmap( avatar_rect, item->avatar );

  // true rounded bubble (r=8): own green on the right, peer white + 1px
  // border on the left - painted with QPainter, no HTML tables
  QRectF bubble_rect = bubbleRect( item, option.rect, y, row_width );
  QPainterPath bubble_path;
  bubble_path.addRoundedRect( bubble_rect, YongTheme::bubbleRadiusPx(), YongTheme::bubbleRadiusPx() );
  QColor bubble_color = item->ownMessage ? ( dark ? QColor( 0x3e, 0xb5, 0x75 ) : YongTheme::bubbleGreen() )
                                         : ( dark ? QColor( 0x3c, 0x40, 0x48 ) : YongTheme::bubbleWhite() );
  if( !item->ownMessage && !dark )
    painter->setPen( QPen( YongTheme::separator(), 1.0 ) );
  else
    painter->setPen( Qt::NoPen );
  painter->setBrush( bubble_color );
  painter->drawPath( bubble_path );

  // shrink-to-fit document (emoticons / links / markdown / code)
  QRectF doc_rect = bubbleRect( item, option.rect, y, row_width )
                      .adjusted( ChatMessageModel::BubbleHPad, ChatMessageModel::BubbleVPad,
                                 -ChatMessageModel::BubbleHPad, -ChatMessageModel::BubbleVPad );
  painter->translate( doc_rect.topLeft() );
  item->doc->drawContents( painter, QRectF( 0, 0, doc_rect.width(), doc_rect.height() ) );

  painter->restore();
}

QSize ChatMessageDelegate::sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
  ChatMessageItem* item = itemForIndex( index );
  const ChatMessageModel* model = messageModel( index );
  if( !item || !model )
    return QSize( 100, 40 );
  if( item->kind != ChatMessageItem::FileCard )
    model->ensureLayout( item );

  const int row_width = model->viewportWidth();
  qreal height = 0.0;
  if( item->showTimestamp )
    height += ChatMessageModel::TimestampRowHeight;
  if( item->showName )
    height += ChatMessageModel::NameRowHeight;

  if( item->kind == ChatMessageItem::FileCard )
    height += ChatMessageModel::FileCardHeight;
  else if( item->kind == ChatMessageItem::UserMessage )
  {
    qreal bubble_h = item->docSize.height() + 2.0 * ChatMessageModel::BubbleVPad;
    height += qMax<qreal>( YongTheme::avatarSizePx(), bubble_h );
  }
  else
    height += item->docSize.height();

  height += ChatMessageModel::RowBottomSpacing;
  return QSize( row_width, qMax( 20, qRound( height ) ) );
}

bool ChatMessageDelegate::editorEvent( QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index )
{
  if( event->type() != QEvent::MouseButtonRelease )
    return QStyledItemDelegate::editorEvent( event, model, option, index );

  ChatMessageItem* item = itemForIndex( index );
  if( !item )
    return false;
  QMouseEvent* mouse_event = static_cast<QMouseEvent*>( event );
  QPointF row_pos = mouse_event->position() - option.rect.topLeft();
  QUrl anchor = anchorInItem( item, option.rect, option.rect.width(), row_pos );
  if( anchor.isValid() && !anchor.toString().isEmpty() )
  {
    emit urlActivated( anchor );
    return true;
  }
  return false;
}
