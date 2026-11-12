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
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QFileInfo>
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
  Q_UNUSED( row_rect );
  const qreal width = qMin<qreal>( ChatMessageModel::FileCardWidth,
      row_width - 2 * ChatMessageModel::HorizontalMargin - YongTheme::avatarSizePx() - ChatMessageModel::AvatarGap );
  const qreal edge = ChatMessageModel::HorizontalMargin + YongTheme::avatarSizePx() + ChatMessageModel::AvatarGap;
  const qreal x = item->ownMessage ? row_width - edge - width : edge;
  return QRectF( x, content_top, qMax<qreal>( 60, width ), ChatMessageModel::FileCardHeight );
}

QUrl ChatMessageDelegate::anchorInItem( const ChatMessageItem* item, const QRect& row_rect, int row_width, const QPointF& row_pos ) const
{
  if( !item )
    return QUrl();

  if( item->kind == ChatMessageItem::FileCard )
  {
    qreal content_top = item->showTimestamp ? ChatMessageModel::TimestampRowHeight : 0;
    const QRectF card = fileCardRect( item, row_rect, content_top, row_width );
    if( item->fileAvailable && card.contains( row_pos ) )
    {
      if( row_pos.y() >= card.bottom() - 30 && row_pos.x() >= card.center().x() )
        return QUrl( item->fileFolderUrl );
      return QUrl( item->fileUrl );
    }
    return QUrl();
  }

  if( item->kind == ChatMessageItem::UserMessage )
  {
    qreal y = 0;
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
  QPointF doc_pos = row_pos - QPointF( ChatMessageModel::HorizontalMargin, 0 );
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
  if( item->kind != ChatMessageItem::FileCard )
    messageModel( index )->ensureLayout( item );
  QPointF row_pos = QPointF( viewport_pos ) - row_rect.topLeft();
  return anchorInItem( item, row_rect, row_rect.width(), row_pos );
}

int ChatMessageDelegate::textPositionAt( const QModelIndex& index, const QRect& row_rect, const QPoint& viewport_pos ) const
{
  ChatMessageItem* item = itemForIndex( index );
  const ChatMessageModel* model = messageModel( index );
  if( !item || !model )
    return -1;

  model->ensureLayout( item );
  if( !item->doc )
    return -1;

  QPointF row_pos = QPointF( viewport_pos ) - QPointF( row_rect.topLeft() );

  if( item->kind == ChatMessageItem::UserMessage )
  {
    qreal y = 0;
    if( item->showTimestamp )
      y += ChatMessageModel::TimestampRowHeight;
    if( item->showName )
      y += ChatMessageModel::NameRowHeight;
    QRectF doc_rect = documentRect( item, row_rect, y, row_rect.width() );
    if( !doc_rect.contains( row_pos ) )
      return -1;
    // FuzzyHit snaps to the nearest character - friendlier for a finger-sized
    // drag than ExactHit, which misses the gaps between lines.
    return static_cast<int>( item->doc->documentLayout()->hitTest( row_pos - doc_rect.topLeft(), Qt::FuzzyHit ) );
  }

  if( item->kind == ChatMessageItem::SystemLine || item->kind == ChatMessageItem::FreeFormDoc )
  {
    QPointF doc_pos = row_pos - QPointF( ChatMessageModel::HorizontalMargin, 0 );
    if( doc_pos.x() < 0 || doc_pos.y() < 0 )
      return -1;
    return static_cast<int>( item->doc->documentLayout()->hitTest( doc_pos, Qt::FuzzyHit ) );
  }

  return -1;
}

// ---------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------

void ChatMessageDelegate::drawDocumentWithSelection( QPainter* painter, ChatMessageItem* item, const QStyleOptionViewItem& option, const QRectF& clip_rect ) const
{
  // YongBEEP partial selection: draw through the document layout with a
  // Selection context so the highlighted range gets a background (the plain
  // QTextDocument::drawContents has no selection support).
  if( item->doc && item->selectionAnchor >= 0 && item->selectionPosition != item->selectionAnchor )
  {
    int start = qMin( item->selectionAnchor, item->selectionPosition );
    int end = qMax( item->selectionAnchor, item->selectionPosition );

    QAbstractTextDocumentLayout::Selection selection;
    selection.cursor = QTextCursor( item->doc );
    selection.cursor.setPosition( start );
    selection.cursor.setPosition( end, QTextCursor::KeepAnchor );
    // WeChat-like soft blue highlight with dark text, readable on both the
    // green own bubble and the white peer bubble
    selection.format.setBackground( QColor( 0x8E, 0xC7, 0xF5 ) );
    selection.format.setForeground( QColor( 0x10, 0x2A, 0x43 ) );

    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.clip = clip_rect;
    ctx.selections.append( selection );
    ctx.palette.setColor( QPalette::Text, option.palette.color( QPalette::Text ) );
    item->doc->documentLayout()->draw( painter, ctx );
    return;
  }

  item->doc->drawContents( painter, clip_rect );
}

void ChatMessageDelegate::drawStatusLine( QPainter* painter, const QString& status_text, const QRectF& line_rect, const QColor& base_color ) const
{
  QFont font = painter->font();
  font.setPixelSize( YongTheme::fontAuxPx() );
  painter->setFont( font );
  painter->setPen( base_color );
  painter->drawText( line_rect, Qt::AlignLeft | Qt::AlignVCenter,
      QFontMetrics( font ).elidedText( status_text, Qt::ElideRight, qMax( 0, int( line_rect.width() ) ) ) );
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
  painter->translate( option.rect.topLeft() );
  const QRect row_rect( 0, 0, row_width, option.rect.height() );

  // --- centered timestamp separator (WeChat gray) ---
  if( item->showTimestamp )
  {
    QFont ts_font = option.font;
    ts_font.setPixelSize( YongTheme::fontTimestampPx() );
    painter->setFont( ts_font );
    painter->setPen( dark ? QColor( Settings::instance().chatSystemTextColor() ) : YongTheme::textSecondary() );
    painter->drawText( QRect( 0, 0, row_width, ChatMessageModel::TimestampRowHeight ),
                       Qt::AlignHCenter | Qt::AlignVCenter, item->timestampText );
  }

  if( item->kind == ChatMessageItem::FileCard )
  {
    const qreal top = item->showTimestamp ? ChatMessageModel::TimestampRowHeight : 0;
    const QRectF card = fileCardRect( item, row_rect, top, row_width );
    if( !item->avatar.isNull() )
      painter->drawPixmap( avatarRect( item, row_rect, top ), item->avatar );
    painter->setPen( QPen( dark ? QColor( "#4c5058" ) : QColor( "#E8E8E8" ), 1 ) );
    painter->setBrush( dark ? QColor( "#3c4048" ) : Qt::white );
    painter->drawRoundedRect( card.adjusted( 0.5, 0.5, -0.5, -0.5 ), 8, 8 );

    const qreal text_x = card.left() + 14;
    const qreal text_width = qMax<qreal>( 20, card.width() - 76 );
    QFont font = option.font;
    font.setPixelSize( 13 );
    painter->setFont( font );
    painter->setPen( dark ? Qt::white : YongTheme::textPrimary() );
    QTextLayout name_layout( item->fileName, font );
    QTextOption wrap;
    wrap.setWrapMode( QTextOption::WrapAtWordBoundaryOrAnywhere );
    name_layout.setTextOption( wrap );
    name_layout.beginLayout();
    for( int n = 0; n < 2; ++n )
    {
      QTextLine line = name_layout.createLine();
      if( !line.isValid() ) break;
      line.setLineWidth( text_width );
      const QPointF origin( text_x, card.top() + 12 + n * 19 );
      if( n == 1 )
        painter->drawText( QRectF( origin, QSizeF( text_width, 19 ) ), Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics( font ).elidedText( item->fileName.mid( line.textStart() ), Qt::ElideMiddle, int( text_width ) ) );
      else
        line.draw( painter, origin );
    }
    name_layout.endLayout();
    if( !item->fileIcon.isNull() )
      item->fileIcon.paint( painter, QRect( int( card.right() - 51 ), int( card.top() + 16 ), 36, 42 ) );

    QString status = item->fileStatus;
    const bool transferring = item->fileProgressTotal > 0 && item->fileProgressBytes < item->fileProgressTotal;
    if( transferring )
      status = QStringLiteral( "%1 / %2" ).arg( ChatMessageModel::fileSizeText( item->fileProgressBytes ), ChatMessageModel::fileSizeText( item->fileProgressTotal ) );
    drawStatusLine( painter, status, QRectF( text_x, card.top() + 57, card.width() - 28, 18 ),
        dark ? QColor( "#aaaaaa" ) : QColor( "#808080" ) );
    const qreal footer_y = card.bottom() - 30;
    painter->setPen( dark ? QColor( "#51555d" ) : QColor( "#EEEEEE" ) );
    painter->drawLine( QPointF( card.left() + 1, footer_y ), QPointF( card.right() - 1, footer_y ) );
    font.setPixelSize( 11 );
    painter->setFont( font );
    painter->setPen( item->fileAvailable ? YongTheme::linkBlue() : QColor( "#9A9A9A" ) );
    const QString open_text = item->fileAvailable ? tr( "Open file" ) : tr( "File transfer" );
    painter->drawText( QRectF( text_x, footer_y, card.width() / 2 - 14, 30 ), Qt::AlignLeft | Qt::AlignVCenter, open_text );
    if( item->fileAvailable )
      painter->drawText( QRectF( card.center().x(), footer_y, card.width() / 2 - 14, 30 ), Qt::AlignRight | Qt::AlignVCenter, tr( "Show in folder" ) );
    if( transferring )
    {
      QRectF track( card.left() + 14, footer_y - 5, card.width() - 28, 2 );
      painter->setPen( Qt::NoPen );
      painter->setBrush( YongTheme::separator() );
      painter->drawRoundedRect( track, 1, 1 );
      track.setWidth( track.width() * qBound<qreal>( 0, qreal( item->fileProgressBytes ) / item->fileProgressTotal, 1 ) );
      painter->setBrush( YongTheme::brandGreen() );
      painter->drawRoundedRect( track, 1, 1 );
    }
    painter->restore();
    return;
  }

  if( item->kind == ChatMessageItem::SystemLine || item->kind == ChatMessageItem::FreeFormDoc )
  {
    // centered gray system line / wide free-form document (saved history,
    // image previews): drawn directly on the chat background
    painter->translate( ChatMessageModel::HorizontalMargin, 0 );
    drawDocumentWithSelection( painter, item, option, QRectF( 0, 0, model->viewportWidth() - 2 * ChatMessageModel::HorizontalMargin, item->docSize.height() ) );
    painter->restore();
    return;
  }

  // --- user message bubble ---
  qreal y = 0;
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
    QRectF bubble_top_rect = bubbleRect( item, row_rect, y, row_width );
    painter->drawText( QRectF( bubble_top_rect.left(), y, bubble_top_rect.width(), ChatMessageModel::NameRowHeight ),
                       Qt::AlignLeft | Qt::AlignVCenter, item->nameText );
    y += ChatMessageModel::NameRowHeight;
  }

  // avatar (40px rounded-6 square, outer edge)
  QRect avatar_rect = avatarRect( item, row_rect, y );
  if( !item->avatar.isNull() )
    painter->drawPixmap( avatar_rect, item->avatar );

  // true rounded bubble (r=8): own green right, peer white left - fill only,
  // NO outline (WeChat separates by contrast; borders read as gray lines)
  QRectF bubble_rect = bubbleRect( item, row_rect, y, row_width );
  QPainterPath bubble_path;
  bubble_path.addRoundedRect( bubble_rect, YongTheme::bubbleRadiusPx(), YongTheme::bubbleRadiusPx() );
  QColor bubble_color = item->ownMessage ? ( dark ? QColor( 0x3e, 0xb5, 0x75 ) : YongTheme::bubbleGreen() )
                                         : ( dark ? QColor( 0x3c, 0x40, 0x48 ) : YongTheme::bubbleWhite() );
  painter->setPen( Qt::NoPen );
  painter->setBrush( bubble_color );
  painter->drawPath( bubble_path );
  QPainterPath tail;
  const qreal edge = item->ownMessage ? bubble_rect.right() : bubble_rect.left();
  const qreal direction = item->ownMessage ? 1 : -1;
  tail.moveTo( edge - direction, bubble_rect.top() + 12 );
  tail.lineTo( edge + direction * 5, bubble_rect.top() + 17 );
  tail.lineTo( edge - direction, bubble_rect.top() + 22 );
  tail.closeSubpath();
  painter->drawPath( tail );

  // shrink-to-fit document (emoticons / links / markdown / code)
  QRectF doc_rect = bubbleRect( item, row_rect, y, row_width )
                      .adjusted( ChatMessageModel::BubbleHPad, ChatMessageModel::BubbleVPad,
                                 -ChatMessageModel::BubbleHPad, -ChatMessageModel::BubbleVPad );
  painter->translate( doc_rect.topLeft() );
  drawDocumentWithSelection( painter, item, option, QRectF( 0, 0, doc_rect.width(), doc_rect.height() ) );

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
  if( mouse_event->button() != Qt::LeftButton )
    return false;
  if( item->kind != ChatMessageItem::FileCard )
    messageModel( index )->ensureLayout( item );
  QPointF row_pos = mouse_event->position() - option.rect.topLeft();
  QUrl anchor = anchorInItem( item, option.rect, option.rect.width(), row_pos );
  if( anchor.isValid() && !anchor.toString().isEmpty() )
  {
    emit urlActivated( anchor );
    return true;
  }
  return false;
}
